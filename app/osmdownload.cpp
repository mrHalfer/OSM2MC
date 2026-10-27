#include "app/osmdownload.h"
#include "app/httpclient.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <QPainterPath>
#include <QPolygonF>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace
{
const char* const kOverpassMirrors[] = { "https://overpass-api.de/api/interpreter", "https://overpass.kumi.systems/api/interpreter" };
}

QString tmcOverpassQuery(const QVector<QPointF>& ll)
{
    QStringList pts;
    for (const QPointF& p : ll) pts << QStringLiteral("%1 %2").arg(p.y(), 0, 'f', 7).arg(p.x(), 0, 'f', 7);       // Overpass: "latitude longitude"
    const QString poly = pts.join(QLatin1Char(' '));
    return QStringLiteral("[out:xml][timeout:300];\n(node(poly:\"%1\");way(poly:\"%1\");rel(poly:\"%1\"););\n(._;>;);\nout body;\n").arg(poly);
}

namespace
{
bool saveOsmFile(const QByteArray& data, const QString& outFile, QString* error)
{
    QDir().mkpath(QFileInfo(outFile).absolutePath());
    const QString tmp = outFile + QStringLiteral(".part");
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) { *error = QObject::tr("Could not write the file: %1").arg(QDir::toNativeSeparators(outFile)); return false; }
    f.close();
    QFile::remove(outFile);
    if (!QFile::rename(tmp, outFile)) { *error = QObject::tr("Could not save the file: %1").arg(QDir::toNativeSeparators(outFile)); return false; }
    return true;
}
}

namespace
{
//! Merges several OSM API answers into one .osm: the elements are written to three temporary files (nodes, ways, relations — in the order the OSM driver expects)
//! and an element with an id that was already written (a way crossing the border of two tiles) is skipped, so the exporter never sees an object twice.
class OsmMerger
{
public:
    explicit OsmMerger(const QString& outFile) : mOut(outFile)
    {
        QDir().mkpath(QFileInfo(outFile).absolutePath());                   // the folder (for example Documents/tMC) may not exist yet
        const char* ext[3] = { ".nodes.part", ".ways.part", ".rels.part" };
        for (int i = 0; i < 3; ++i)
        {
            mFile[i].setFileName(outFile + QLatin1String(ext[i]));
            if (!mFile[i].open(QIODevice::ReadWrite | QIODevice::Truncate)) mOk = false;
        }
    }
    ~OsmMerger() { for (int i = 0; i < 3; ++i) { mFile[i].close(); mFile[i].remove(); } }
    bool opened() const { return mOk; }
    qint64 nodeCount() const { return mIndex[0].size(); }

    bool add(const QByteArray& xml)
    {
        QXmlStreamReader r(xml);
        while (!r.atEnd())
        {
            if (r.readNext() != QXmlStreamReader::StartElement) continue;
            const QStringRef name = r.name();
            const int k = name == QLatin1String("node") ? 0 : name == QLatin1String("way") ? 1 : name == QLatin1String("relation") ? 2 : -1;
            if (k < 0) continue;                                              // <osm>, <bounds>
            const qint64 id = r.attributes().value(QLatin1String("id")).toLongLong();
            if (mSeen[k].contains(id)) { r.skipCurrentElement(); continue; }
            mSeen[k].insert(id);
            QByteArray chunk;
            {
                QXmlStreamWriter w(&chunk);
                w.setAutoFormatting(false);
                w.writeCurrentToken(r);
                int depth = 1;
                while (depth > 0 && !r.atEnd())
                {
                    const QXmlStreamReader::TokenType t = r.readNext();
                    if (t == QXmlStreamReader::StartElement) ++depth;
                    else if (t == QXmlStreamReader::EndElement) --depth;
                    else if (t == QXmlStreamReader::Characters && r.isWhitespace()) continue;
                    w.writeCurrentToken(r);
                }
            }
            mIndex[k].append({ id, mFile[k].pos(), int(chunk.size()) });
            mFile[k].write(chunk);
        }
        return !r.hasError();
    }

    //! The output is sorted by id inside nodes, ways and relations: the GDAL OSM driver relies on it when it reads an XML file
    bool finish(QString* error)
    {
        QDir().mkpath(QFileInfo(mOut).absolutePath());
        const QString tmp = mOut + QStringLiteral(".part");
        QFile out(tmp);
        if (!out.open(QIODevice::WriteOnly)) { *error = QObject::tr("Could not write the file: %1").arg(QDir::toNativeSeparators(mOut)); return false; }
        out.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<osm version=\"0.6\" generator=\"tMC\">\n");
        for (int k = 0; k < 3; ++k)
        {
            std::sort(mIndex[k].begin(), mIndex[k].end(), [](const Entry& a, const Entry& b) { return a.id < b.id; });
            mFile[k].flush();
            for (const Entry& e : mIndex[k])
            {
                mFile[k].seek(e.off);
                out.write(mFile[k].read(e.len));
                out.write("\n");
            }
        }
        out.write("</osm>\n");
        out.close();
        QFile::remove(mOut);
        if (!QFile::rename(tmp, mOut)) { *error = QObject::tr("Could not save the file: %1").arg(QDir::toNativeSeparators(mOut)); return false; }
        return true;
    }

private:
    struct Entry { qint64 id; qint64 off; int len; };
    QString mOut;
    QFile mFile[3];
    QVector<Entry> mIndex[3];
    QSet<qint64> mSeen[3];
    bool mOk = true;
};

enum class TileResult { Ok, Failed, Cancelled };

//! Loads the area of the official OSM API tile by tile: a rectangle is requested; if the server refuses it as too big (more than 50,000 nodes / the
//! area limit) it is split into four, and so on. Rectangles that do not touch the boundary polygon are skipped. The number of requests is limited.
class TileLoader
{
public:
    TileLoader(const QPainterPath& area, OsmMerger& merger, const std::atomic_bool* cancel, const std::function<void(const QString&)>& progress)
        : mArea(area), mMerger(merger), mCancel(cancel), mProgress(progress) {}
    int requests() const { return mRequests; }
    QString error() const { return mError; }

    TileResult load(const QRectF& rect, int depth)
    {
        if (mCancel && mCancel->load()) return TileResult::Cancelled;
        if (!mArea.intersects(rect) && !mArea.contains(rect)) return TileResult::Ok;
        if (mRequests >= kMaxRequests) { mError = QObject::tr("the area is too dense for the OpenStreetMap server: too many requests are needed"); return TileResult::Failed; }
        ++mRequests;
        if (mProgress) mProgress(QObject::tr("Downloading from the official OpenStreetMap API: request %1 …").arg(mRequests));
        const QString url = QStringLiteral("https://api.openstreetmap.org/api/0.6/map?bbox=%1,%2,%3,%4")
                                .arg(rect.left(), 0, 'f', 7).arg(rect.top(), 0, 'f', 7).arg(rect.right(), 0, 'f', 7).arg(rect.bottom(), 0, 'f', 7);
        HttpResult r;
        for (int attempt = 0; attempt < 6; ++attempt)
        {
            r = httpRequest(url, QByteArray(), QByteArray(), 120000, mCancel);
            if (mCancel && mCancel->load()) return TileResult::Cancelled;
            // 509 "You have downloaded too much data. Please try again in N seconds" and the usual "busy" codes: wait as the server asks and repeat
            const bool busy = r.status == 509 || r.status == 429 || r.status == 502 || r.status == 503 || r.status == 504;
            if (!busy || attempt == 5) break;
            int waitMs = 5000 * (attempt + 1);
            const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("in (\\d+) seconds")).match(QString::fromUtf8(r.body.left(300)));
            if (m.hasMatch()) waitMs = std::min(60000, m.captured(1).toInt() * 1000 + 1000 * (attempt + 1));
            for (int ms = 0; ms < waitMs; ms += 100)
            {
                if (mCancel && mCancel->load()) return TileResult::Cancelled;
                QThread::msleep(100);
            }
        }
        if (!r.error.isEmpty()) { mError = QObject::tr("no connection to the OpenStreetMap server (%1)").arg(r.error); return TileResult::Failed; }
        if (r.ok())
        {
            if (!mMerger.add(r.body)) { mError = QObject::tr("the OpenStreetMap server returned a broken answer"); return TileResult::Failed; }
            return TileResult::Ok;
        }
        const QString text = QString::fromUtf8(r.body.left(200)).trimmed();
        const bool tooBig = r.status == 400 && (text.contains(QLatin1String("nodes")) || text.contains(QLatin1String("bbox")) || text.contains(QLatin1String("limit")));
        if (!tooBig) { mError = QObject::tr("the OpenStreetMap server answered with code %1 %2").arg(r.status).arg(text); return TileResult::Failed; }
        if (depth >= kMaxDepth) { mError = QObject::tr("the area is too dense for the OpenStreetMap server"); return TileResult::Failed; }
        const double mx = rect.center().x(), my = rect.center().y();
        const QRectF quads[4] = { QRectF(QPointF(rect.left(), rect.top()), QPointF(mx, my)), QRectF(QPointF(mx, rect.top()), QPointF(rect.right(), my)),
                                  QRectF(QPointF(rect.left(), my), QPointF(mx, rect.bottom())), QRectF(QPointF(mx, my), QPointF(rect.right(), rect.bottom())) };
        for (const QRectF& q : quads)
        {
            const TileResult t = load(q, depth + 1);
            if (t != TileResult::Ok) return t;
        }
        return TileResult::Ok;
    }

private:
    static constexpr int kMaxRequests = 80, kMaxDepth = 6;
    QPainterPath mArea;
    OsmMerger& mMerger;
    const std::atomic_bool* mCancel;
    std::function<void(const QString&)> mProgress;
    int mRequests = 0;
    QString mError;
};
}

OsmDownloadResult tmcDownloadOsm(const QVector<QPointF>& lonLat, const QString& outFile, const std::atomic_bool* cancel,
                                 const std::function<void(const QString&)>& progress)
{
    OsmDownloadResult R;
    if (lonLat.size() < 3) { R.error = QObject::tr("The boundary is not set."); return R; }

    // 1. The official OSM API (bbox), tile by tile: it is reliable and does not load Overpass. A request is limited to 50,000 nodes and 0.25 square degrees,
    //    so a bigger or denser area is split recursively (the boundary is at most 25 km², so these are a few requests); the answers are merged without duplicates.
    QString officialNote;
    {
        double x0 = lonLat[0].x(), x1 = x0, y0 = lonLat[0].y(), y1 = y0;
        for (const QPointF& p : lonLat) { x0 = std::min(x0, p.x()); x1 = std::max(x1, p.x()); y0 = std::min(y0, p.y()); y1 = std::max(y1, p.y()); }
        QPainterPath area;
        area.addPolygon(QPolygonF(lonLat));
        area.closeSubpath();
        OsmMerger merger(outFile);
        if (!merger.opened()) { R.error = QObject::tr("Could not write the file: %1").arg(QDir::toNativeSeparators(outFile)); return R; }
        TileLoader loader(area, merger, cancel, progress);
        const TileResult t = loader.load(QRectF(QPointF(x0, y0), QPointF(x1, y1)), 0);
        if (t == TileResult::Cancelled) { R.cancelled = true; return R; }
        if (t == TileResult::Ok)
        {
            if (merger.nodeCount() == 0) { R.error = QObject::tr("There is no data in OpenStreetMap for this area."); return R; }
            if (!merger.finish(&R.error)) return R;
            R.ok = true; R.bytes = QFileInfo(outFile).size();
            R.source = QObject::tr("OSM API, requests: %1").arg(loader.requests());
            return R;
        }
        officialNote = loader.error();
    }
    R.source = QStringLiteral("Overpass");
    const int nMirrors = int(sizeof(kOverpassMirrors) / sizeof(kOverpassMirrors[0]));
    int mirrorNo = 0;
    // 2. Overpass
    const QByteArray body = "data=" + QUrl::toPercentEncoding(tmcOverpassQuery(lonLat));
    QString lastErr;
    for (const char* url : kOverpassMirrors)
    {
        if (cancel && cancel->load()) { R.cancelled = true; return R; }
        ++mirrorNo;
        // the server is "at capacity" (429/502/503/504): retry with a growing pause of 5 and 15 seconds, as Overpass itself asks
        HttpResult r;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            if (progress)
                progress(QObject::tr("The official OSM API did not work (%1) — switched to the Overpass fallback: server %2 of %3, attempt %4 of 3. It can take several minutes.")
                             .arg(officialNote.isEmpty() ? QObject::tr("unknown reason") : officialNote).arg(mirrorNo).arg(nMirrors).arg(attempt + 1));
            r = httpRequest(QString::fromLatin1(url), body, QByteArray("application/x-www-form-urlencoded"), 330000, cancel);
            if (cancel && cancel->load()) { R.cancelled = true; return R; }
            const bool busy = r.status == 429 || r.status == 502 || r.status == 503 || r.status == 504;
            if (!busy || attempt == 2) break;
            for (int ms = 0; ms < (attempt == 0 ? 5000 : 15000); ms += 100)
            {
                if (cancel && cancel->load()) { R.cancelled = true; return R; }
                QThread::msleep(100);
            }
        }
        if (!r.error.isEmpty()) { lastErr = QObject::tr("no connection to the Overpass server (%1)").arg(r.error); continue; }
        if (r.status == 429 || r.status >= 500) { lastErr = QObject::tr("the Overpass server is busy or overloaded (code %1), try again later").arg(r.status); continue; }
        if (!r.ok()) { lastErr = QObject::tr("the Overpass server answered with code %1").arg(r.status); continue; }
        // an execution error comes as 200 with <remark> inside an empty osm
        if (!r.body.contains("<osm")) { lastErr = QObject::tr("the Overpass server returned something that is not an OSM extract"); continue; }
        const int remark = r.body.indexOf("<remark>");
        if (remark >= 0) { lastErr = QObject::tr("Overpass: %1").arg(QString::fromUtf8(r.body.mid(remark + 8, 160)).section(QLatin1Char('<'), 0, 0).trimmed()); continue; }
        if (!r.body.contains("<node") && !r.body.contains("<way")) { R.error = QObject::tr("There is no data in OpenStreetMap for this area."); return R; }
        if (!saveOsmFile(r.body, outFile, &R.error)) return R;
        R.ok = true;
        R.bytes = r.body.size();
        return R;
    }
    R.error = (lastErr.isEmpty() ? QObject::tr("Could not download the data.") : lastErr)
              + (officialNote.isEmpty() ? QString() : QObject::tr(" (the official API had answered before that: %1)").arg(officialNote));
    return R;
}

QVector<OsmPlace> tmcSearchPlaces(const QString& query, QString* error)
{
    QVector<OsmPlace> out;
    const QString url = QStringLiteral("https://nominatim.openstreetmap.org/search?format=jsonv2&limit=6&accept-language=ru&q=")
                        + QString::fromLatin1(QUrl::toPercentEncoding(query));
    const HttpResult r = httpRequest(url, QByteArray(), QByteArray(), 15000);
    if (!r.error.isEmpty()) { if (error) *error = QObject::tr("no connection to the search server (%1)").arg(r.error); return out; }
    if (!r.ok()) { if (error) *error = QObject::tr("the search server answered with code %1").arg(r.status); return out; }
    const QJsonArray arr = QJsonDocument::fromJson(r.body).array();
    for (const QJsonValue& v : arr)
    {
        const QJsonObject o = v.toObject();
        const QJsonArray bb = o.value(QStringLiteral("boundingbox")).toArray();          // [south, north, west, east] as strings
        OsmPlace p;
        p.name = o.value(QStringLiteral("display_name")).toString();
        p.lat = o.value(QStringLiteral("lat")).toString().toDouble();
        p.lon = o.value(QStringLiteral("lon")).toString().toDouble();
        if (bb.size() == 4) { p.lat0 = bb[0].toString().toDouble(); p.lat1 = bb[1].toString().toDouble(); p.lon0 = bb[2].toString().toDouble(); p.lon1 = bb[3].toString().toDouble(); }
        else { p.lat0 = p.lat1 = p.lat; p.lon0 = p.lon1 = p.lon; }
        if (!p.name.isEmpty()) out << p;
    }
    return out;
}
