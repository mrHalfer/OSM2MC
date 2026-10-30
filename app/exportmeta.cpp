#include "app/exportmeta.h"
#include "app/tmcrun.h"                    // TmcRunOptions, tmcProjectToLonLat
#include "export/climate.h"
#include <QDateTime>

#ifndef TMC_VERSION_STR
#define TMC_VERSION_STR "dev"
#endif

namespace TmcExportMeta
{
QString version() { return QStringLiteral(TMC_VERSION_STR); }

QJsonObject build(const Params& p)
{
    QJsonObject root;
    root.insert(QStringLiteral("tmcExport"), 1);
    root.insert(QStringLiteral("exporterVersion"), version());
    root.insert(QStringLiteral("created"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!p.dataBbox.isEmpty()) root.insert(QStringLiteral("dataBboxLonLat"), p.dataBbox);
    if (!p.clip.isEmpty()) root.insert(QStringLiteral("clipLonLat"), p.clip);
    if (!p.spawn.isEmpty()) root.insert(QStringLiteral("spawnLonLat"), p.spawn);
    if (!p.marks.isEmpty()) root.insert(QStringLiteral("marks"), p.marks);
    QJsonObject opt;
    opt.insert(QStringLiteral("format"), p.format);
    opt.insert(QStringLiteral("noiseWorld"), p.noiseWorld);
    opt.insert(QStringLiteral("caves"), p.caves);
    opt.insert(QStringLiteral("seed"), QString::number(p.seed));       // a string: a u64 loses precision through JSON doubles
    opt.insert(QStringLiteral("datapack"), p.datapack);
    opt.insert(QStringLiteral("realMeters"), p.realMeters);
    opt.insert(QStringLiteral("greenery"), p.greenery);
    opt.insert(QStringLiteral("enhancer"), p.enhancer);
    opt.insert(QStringLiteral("climate"), p.climate);
    if (p.climateResolved >= 0) opt.insert(QStringLiteral("climateResolved"), QLatin1String(Climate::key(p.climateResolved)));
    root.insert(QStringLiteral("options"), opt);
    return root;
}

QByteArray toJson(const Params& p)
{
    return QJsonDocument(build(p)).toJson(QJsonDocument::Indented);
}

QJsonArray bboxLonLat(const QString& wkt, double x0, double y0, double x1, double y1)
{
    if (wkt.isEmpty() || x1 <= x0 || y1 <= y0) return QJsonArray();
    double w = 1e300, e = -1e300, s = 1e300, n = -1e300;
    const double xs[4] = { x0, x1, x0, x1 }, ys[4] = { y0, y0, y1, y1 };
    for (int i = 0; i < 4; ++i)
    {
        double lon = 0, lat = 0;
        if (!tmcProjectToLonLat(wkt, xs[i], ys[i], lon, lat)) continue;
        w = qMin(w, lon); e = qMax(e, lon); s = qMin(s, lat); n = qMax(n, lat);
    }
    if (e <= w || s >= n) return QJsonArray();
    return QJsonArray{ w, s, e, n };
}

Params fromRun(const TmcRunOptions& o, const QString& wkt,
               double x0, double y0, double x1, double y1,
               quint64 seed, int climateResolved, bool hasSpawn, double spawnX, double spawnY)
{
    Params p;
    p.format = o.format == McWorldExporter::Format::Legacy121x ? QStringLiteral("1.21.x") : QStringLiteral("26.x");
    p.noiseWorld = o.noiseWorld;
    p.caves = o.caves;
    p.datapack = o.locateDatapack;
    p.realMeters = o.realMeters;
    p.greenery = !o.noGreenery;
    p.enhancer = !o.noEnhancer;
    p.seed = seed;
    p.climate = o.climate < 0 ? QStringLiteral("auto") : QString::fromLatin1(Climate::key(o.climate));
    p.climateResolved = climateResolved;
    p.dataBbox = bboxLonLat(wkt, x0, y0, x1, y1);
    auto toLL = [&wkt](double x, double y, QJsonArray* out)
    {
        double lon = 0, lat = 0;
        if (!tmcProjectToLonLat(wkt, x, y, lon, lat)) return false;
        *out = QJsonArray{ lon, lat };
        return true;
    };
    for (const QPointF& v : o.clipPolygon) { QJsonArray a; if (toLL(v.x(), v.y(), &a)) p.clip.append(a); }
    if (hasSpawn) { QJsonArray a; if (toLL(spawnX, spawnY, &a)) p.spawn = a; }
    struct Mark { const char* name; bool hasPt; QPointF pt; QString chunk; };
    const Mark kMarks[3] = { { "stronghold", o.hasStrongholdPt, o.strongholdPt, o.strongholdChunk },
                             { "city", o.hasCityPt, o.cityPt, o.cityChunk },
                             { "paleForest", o.hasPalePt, o.palePt, o.paleForestChunk } };
    for (const Mark& m : kMarks)
    {
        QJsonObject mo;
        if (!m.chunk.isEmpty()) mo.insert(QStringLiteral("chunk"), m.chunk);
        if (m.hasPt) { QJsonArray a; if (toLL(m.pt.x(), m.pt.y(), &a)) mo.insert(QStringLiteral("lonLat"), a); }
        if (!mo.isEmpty()) p.marks.insert(QLatin1String(m.name), mo);
    }
    return p;
}
}
