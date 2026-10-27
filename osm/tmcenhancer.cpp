#include "osm/tmcenhancer.h"
#include "ogrsf_frmts.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int kHeaderSize = 32, kIndexSize = 20, kRecSize = 16;

template <typename T> T rd(const char* p) { T v; memcpy(&v, p, sizeof(T)); return v; }   // little-endian, as on Windows

qint64 cellKey(int ix, int iy) { return (qint64(ix) << 32) | qint64(quint32(iy)); }
int floorDiv(qint64 a, int b) { return int(a >= 0 ? a / b : -((-a + b - 1) / b)); }
}

bool TmcEnhancer::load(const QString& path, QString* err)
{
    mLoaded = false;
    mCells.clear();
    mCache.clear();
    if (mFile.isOpen()) mFile.close();
    mFile.setFileName(path);
    if (!mFile.open(QIODevice::ReadOnly)) { if (err) *err = QObject::tr("Could not open the enhancer file: %1").arg(path); return false; }
    const QByteArray h = mFile.read(kHeaderSize);
    if (h.size() < kHeaderSize || h.left(4) != QByteArrayLiteral("TMCB") || rd<quint32>(h.constData() + 4) != 1u)
    { if (err) *err = QObject::tr("%1: not an enhancer file TMCB version 1").arg(path); mFile.close(); return false; }
    mCellMicro = int(rd<quint32>(h.constData() + 8));
    const quint32 nCells = rd<quint32>(h.constData() + 12);
    mRecords = qint64(rd<quint64>(h.constData() + 16));
    const QByteArray idx = mFile.read(qint64(nCells) * kIndexSize);
    if (mCellMicro <= 0 || idx.size() != qint64(nCells) * kIndexSize) { if (err) *err = QObject::tr("%1: the index is damaged").arg(path); mFile.close(); return false; }
    mCells.reserve(int(nCells));
    for (quint32 i = 0; i < nCells; ++i)
    {
        const char* p = idx.constData() + qint64(i) * kIndexSize;
        Cell c;
        c.first = qint64(rd<quint64>(p + 8));
        c.count = rd<quint32>(p + 16);
        mCells.insert(rd<qint64>(p), c);
    }
    mDataOffset = kHeaderSize + qint64(nCells) * kIndexSize;
    mPath = path;
    mLoaded = true;
    return true;
}

QString TmcEnhancer::findDefault()
{
    for (const QString& d : { QCoreApplication::applicationDirPath(), QDir::currentPath() })
    {
        const QString p = QDir(d).filePath(QStringLiteral("tmc_buildings.dat"));
        if (QFileInfo::exists(p)) return p;
    }
    return QString();
}

int TmcEnhancer::normalizeCode(int code)
{
    if (code == 44400100 || code == 44400110) return 44400002;
    if (code == 44400001) return 44400000;
    return code;
}

QVector<TmcEnhancer::Rec> TmcEnhancer::query(double lon0, double lat0, double lon1, double lat1) const
{
    QVector<Rec> out;
    if (!mLoaded) return out;
    const int ix0 = floorDiv(qint64(std::floor(lon0 * 1e6)), mCellMicro), ix1 = floorDiv(qint64(std::floor(lon1 * 1e6)), mCellMicro);
    const int iy0 = floorDiv(qint64(std::floor(lat0 * 1e6)), mCellMicro), iy1 = floorDiv(qint64(std::floor(lat1 * 1e6)), mCellMicro);
    for (int ix = ix0; ix <= ix1; ++ix)
        for (int iy = iy0; iy <= iy1; ++iy)
        {
            const qint64 key = cellKey(ix, iy);
            auto cit = mCache.constFind(key);
            if (cit == mCache.constEnd())
            {
                QVector<Rec> recs;
                const auto it = mCells.constFind(key);
                if (it != mCells.constEnd())
                {
                    mFile.seek(mDataOffset + it->first * kRecSize);
                    const QByteArray raw = mFile.read(qint64(it->count) * kRecSize);
                    const int n = int(raw.size() / kRecSize);
                    recs.reserve(n);
                    for (int i = 0; i < n; ++i)
                    {
                        const char* p = raw.constData() + qint64(i) * kRecSize;
                        Rec r;
                        r.id = it->first + i;
                        r.lat = rd<qint32>(p) * 1e-6;
                        r.lon = rd<qint32>(p + 4) * 1e-6;
                        r.code = int(rd<quint32>(p + 8));
                        r.rel = rd<quint16>(p + 12) * 0.1;
                        r.floor = int(rd<quint16>(p + 14));
                        recs << r;
                    }
                }
                cit = mCache.insert(key, recs);
            }
            for (const Rec& r : cit.value())
                if (r.lon >= lon0 && r.lon <= lon1 && r.lat >= lat0 && r.lat <= lat1) out << r;
        }
    return out;
}

namespace
{
// Distance (m) from a point to a polygon outline; 0 — inside. Coordinates are degrees, converted to meters around the latitude of the point.
double polyDistM(const OGRPolygon* poly, double lon, double lat, double kx, double ky)
{
    bool inside = false;
    double best = 1e18;
    auto ring = [&](const OGRLinearRing* r)
    {
        const int n = r->getNumPoints();
        for (int i = 0; i + 1 < n; ++i)
        {
            const double ax = (r->getX(i) - lon) * kx, ay = (r->getY(i) - lat) * ky;
            const double bx = (r->getX(i + 1) - lon) * kx, by = (r->getY(i + 1) - lat) * ky;
            if ((ay > 0) != (by > 0) && ax + (0 - ay) / (by - ay) * (bx - ax) > 0) inside = !inside;     // a ray to the right of the point (parity over all rings)
            const double vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
            double t = l2 > 0 ? (-ax * vx - ay * vy) / l2 : 0.0;
            t = std::max(0.0, std::min(1.0, t));
            best = std::min(best, std::hypot(ax + t * vx, ay + t * vy));
        }
    };
    if (poly->getExteriorRing()) ring(poly->getExteriorRing());
    for (int i = 0; i < poly->getNumInteriorRings(); ++i) ring(poly->getInteriorRing(i));
    return inside ? 0.0 : best;
}
}

bool TmcEnhancer::match(const OGRGeometry* g, QSet<qint64>& claimed, Rec* out, double maxDistM) const
{
    if (!mLoaded || !g || g->IsEmpty()) return false;
    OGREnvelope e;
    g->getEnvelope(&e);
    const double midLat = 0.5 * (e.MinY + e.MaxY);
    const double ky = 111320.0, kx = 111320.0 * std::max(0.05, std::cos(midLat * 3.14159265358979 / 180.0));
    const double padLat = maxDistM / ky, padLon = maxDistM / kx;
    const QVector<Rec> cand = query(e.MinX - padLon, e.MinY - padLat, e.MaxX + padLon, e.MaxY + padLat);
    if (cand.isEmpty()) return false;
    const OGRwkbGeometryType t = wkbFlatten(g->getGeometryType());
    QVector<const OGRPolygon*> parts;
    if (t == wkbPolygon) parts << static_cast<const OGRPolygon*>(g);
    else if (t == wkbMultiPolygon)
    {
        const OGRMultiPolygon* mp = static_cast<const OGRMultiPolygon*>(g);
        for (int i = 0; i < mp->getNumGeometries(); ++i) parts << static_cast<const OGRPolygon*>(mp->getGeometryRef(i));
    }
    if (parts.isEmpty()) return false;
    const double cx = 0.5 * (e.MinX + e.MaxX), cy = 0.5 * (e.MinY + e.MaxY);
    const Rec* best = nullptr;
    double bestD = 1e18, bestC = 1e18;
    for (const Rec& r : cand)
    {
        if (claimed.contains(r.id)) continue;
        double d = 1e18;
        for (const OGRPolygon* p : parts) d = std::min(d, polyDistM(p, r.lon, r.lat, kx, ky));
        if (d > maxDistM) continue;
        const double c = std::hypot((r.lon - cx) * kx, (r.lat - cy) * ky);      // on a tie — closer to the outline center
        if (d < bestD - 1e-9 || (std::fabs(d - bestD) <= 1e-9 && c < bestC)) { best = &r; bestD = d; bestC = c; }
    }
    if (!best) return false;
    claimed.insert(best->id);
    if (out) *out = *best;
    return true;
}
