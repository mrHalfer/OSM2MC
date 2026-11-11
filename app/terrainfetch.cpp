#include "app/terrainfetch.h"
#include "app/httpclient.h"
#include "core/dtmgrid.h"
#include "core/dtmbuild.h"
#include "gdal_priv.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureSynchronizer>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
//! The open terrain tiles: 256x256 PNG in the terrarium encoding, no API key (the AWS Open Data registry).
const char* const kTileUrl = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%1/%2/%3.png";
constexpr int kTilePx = 256;
constexpr int kMaxThreads = 8;
constexpr int kRetries = 3;
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthR = 6378137.0;
constexpr double kTargetMeters = 30.0;      // the wanted ground resolution of the tiles (the source is ~30 m anyway)
constexpr double kMarginMeters = 1000.0;    // the matrix goes this far beyond the boundary (mountains do not stop at it)
constexpr int kMinZoom = 10, kMaxZoom = 12;

//! The attribution of the tiles (the required text lives in the tilezen/joerd attribution document)
const char* const kTerrainNote =
    "Terrain (elevation) data: AWS Terrain Tiles (Mapzen/Tilezen joerd: SRTM, GMTED2010, 3DEP, EU-DEM, ETOPO1 and national DEMs), "
    "https://github.com/tilezen/joerd/blob/master/docs/attribution.md";

struct TileKey
{
    quint32 x = 0, y = 0;
    bool operator==(const TileKey& o) const { return x == o.x && y == o.y; }
};
inline uint qHash(const TileKey& k, uint seed = 0) { return ::qHash((quint64(k.x) << 32) | k.y, seed); }

using TileHeights = QVector<float>;         // kTilePx*kTilePx meters, row-major

//! The zoom whose pixels are at most kTargetMeters of ground, capped to what the source really holds.
int chooseZoom(double lat)
{
    const double cosLat = std::max(0.05, std::cos(lat * kPi / 180.0));
    const double z = std::log2(156543.03392 * cosLat / kTargetMeters);
    return std::max(kMinZoom, std::min(kMaxZoom, int(std::ceil(z))));
}

quint32 tileX(double lon, int z) { return quint32(std::floor((lon + 180.0) / 360.0 * double(1 << z))); }
quint32 tileY(double lat, int z)
{
    const double r = lat * kPi / 180.0;
    return quint32(std::floor((1.0 - std::asinh(std::tan(r)) / kPi) / 2.0 * double(1 << z)));
}

QString cacheDir(int z)
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    return QDir(base).filePath(QStringLiteral("terrain/z%1").arg(z));
}

//! The overall limit of the elevation download: a hung or unreachable tile server must not hold the export hostage —
//! after it the terrain is simply left out (the caller warns).
constexpr int kDownloadDeadlineMs = 90000;
constexpr int kTileRequestMs = 15000;

//! true + the tile PNG bytes (from the disk cache or the network); the deadline is checked before every attempt
bool loadTileBytes(const TileKey& k, int z, const std::atomic_bool* cancel, QElapsedTimer& clock, QByteArray& out, QString* err)
{
    const QString dir = cacheDir(z);
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("%1_%2.png").arg(k.x).arg(k.y));
    if (QFileInfo(path).size() > 0)
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) { out = f.readAll(); return true; }
    }
    const QString url = QString::fromLatin1(kTileUrl).arg(z).arg(k.x).arg(k.y);
    QString last;
    for (int attempt = 0; attempt < kRetries; ++attempt)
    {
        if (cancel && cancel->load()) { *err = QObject::tr("cancelled"); return false; }
        const qint64 left = kDownloadDeadlineMs - clock.elapsed();
        if (left <= 0)
        {
            *err = QObject::tr("the elevation tile server did not answer within %1 s").arg(kDownloadDeadlineMs / 1000);
            return false;
        }
        if (attempt) QThread::msleep(500u * (1u << (attempt - 1)));
        const HttpResult r = httpRequest(url, QByteArray(), QByteArray(), int(std::min<qint64>(kTileRequestMs, left)), cancel);
        if (r.ok() && !r.body.isEmpty())
        {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) f.write(r.body);
            out = r.body;
            return true;
        }
        last = r.error.isEmpty() ? QStringLiteral("HTTP %1").arg(r.status) : r.error;
    }
    *err = last;
    return false;
}

//! Decode one terrarium tile into meters (NaN for a broken pixel)
bool decodeTile(const QByteArray& bytes, TileHeights& out)
{
    QImage img = QImage::fromData(bytes, "PNG");
    if (img.isNull()) return false;
    img = img.convertToFormat(QImage::Format_RGB32);
    if (img.width() != kTilePx || img.height() != kTilePx) return false;
    out.resize(kTilePx * kTilePx);
    for (int j = 0; j < kTilePx; ++j)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(j));
        for (int i = 0; i < kTilePx; ++i)
        {
            const QRgb c = line[i];
            out[j * kTilePx + i] = float(tmcTerrariumHeight(qRed(c), qGreen(c), qBlue(c)));
        }
    }
    return true;
}

//! One pixel of the tile mosaic (the tile index may point outside the downloaded set — false then)
bool mosaicPixel(const QHash<TileKey, TileHeights>& tiles, qint32 tx, qint32 ty, qint32 i, qint32 j, float& v)
{
    while (i < 0) { i += kTilePx; --tx; }
    while (i >= kTilePx) { i -= kTilePx; ++tx; }
    while (j < 0) { j += kTilePx; --ty; }
    while (j >= kTilePx) { j -= kTilePx; ++ty; }
    const auto it = tiles.constFind(TileKey{ quint32(tx), quint32(ty) });
    if (it == tiles.constEnd()) return false;
    v = it.value()[j * kTilePx + i];
    return !std::isnan(v);
}
}

bool tmcIsDownloadedDtm(const QString& path)
{
    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpenEx(path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    if (!ds) return false;
    const char* note = ds->GetMetadataItem("TERRAIN_SOURCE");
    const bool ours = note && *note;
    GDALClose(ds);
    return ours;
}

TerrainFetchResult tmcFetchTerrain(const QVector<QPointF>& lonLat, const QString& outFile, const std::atomic_bool* cancel,
                                   const std::function<void(const QString&)>& progress)
{
    TerrainFetchResult R;
    if (lonLat.size() < 3) { R.error = QObject::tr("The boundary is not set."); return R; }
    QElapsedTimer clock;
    clock.start();
    double lon0 = 180, lon1 = -180, lat0 = 90, lat1 = -90;
    for (const QPointF& p : lonLat)
    {
        lon0 = std::min(lon0, p.x()); lon1 = std::max(lon1, p.x());
        lat0 = std::min(lat0, p.y()); lat1 = std::max(lat1, p.y());
    }
    if (lat0 < -85.0 || lat1 > 85.0) { R.error = QObject::tr("The area is too close to the pole."); return R; }

    const double latMid = 0.5 * (lat0 + lat1);
    const int z = chooseZoom(latMid);
    // The matrix covers the boundary and a margin around it (the mountains must not end at the border)
    const double dLat = kMarginMeters / 111320.0;
    const double dLon = kMarginMeters / (111320.0 * std::max(0.05, std::cos(latMid * kPi / 180.0)));
    lon0 = std::max(-179.9, lon0 - dLon); lon1 = std::min(179.9, lon1 + dLon);
    lat0 = std::max(-85.0, lat0 - dLat); lat1 = std::min(85.0, lat1 + dLat);

    // The tiles covering the area
    QVector<TileKey> keys;
    for (quint32 x = tileX(lon0, z); x <= tileX(lon1, z); ++x)
        for (quint32 y = tileY(lat1, z); y <= tileY(lat0, z); ++y)
            keys.append(TileKey{ x, y });
    if (keys.size() > 64)
    {
        R.error = QObject::tr("The area needs too many elevation tiles (%1).").arg(keys.size());
        return R;
    }

    if (progress) progress(QObject::tr("Terrain: downloading %1 tiles of elevation…").arg(keys.size()));
    QHash<TileKey, TileHeights> tiles;
    QMutex tilesMutex;
    QStringList errors;
    {
        QThreadPool pool;
        pool.setMaxThreadCount(kMaxThreads);
        QFutureSynchronizer<void> sync;
        for (const TileKey& k : keys)
            sync.addFuture(QtConcurrent::run(&pool, [&tiles, &tilesMutex, &errors, &clock, k, z, cancel]()
            {
                QByteArray bytes;
                QString err;
                if (!loadTileBytes(k, z, cancel, clock, bytes, &err))
                {
                    QMutexLocker lk(&tilesMutex);
                    errors << err;
                    return;
                }
                TileHeights h;
                if (!decodeTile(bytes, h))
                {
                    QMutexLocker lk(&tilesMutex);
                    errors << QObject::tr("a broken tile %1/%2").arg(k.x).arg(k.y);
                    return;
                }
                QMutexLocker lk(&tilesMutex);
                tiles.insert(k, h);
            }));
        sync.waitForFinished();
    }
    if (cancel && cancel->load()) { R.cancelled = true; R.error = QObject::tr("cancelled"); return R; }
    if (tiles.size() < keys.size())
    {   // a partial mosaic would be filled by the neighbours' heights — a misleading terrain: leave it out entirely
        R.error = QObject::tr("The elevation tiles could not be downloaded (%1 of %2): %3")
                      .arg(tiles.size()).arg(keys.size())
                      .arg(errors.isEmpty() ? QObject::tr("unknown reason") : errors.first());
        return R;
    }

    if (progress) progress(QObject::tr("Terrain: the tiles are ready — building the height matrix…"));
    // The sampler: longitude/latitude -> the bilinear value of the tile mosaic
    const double n = double(1 << z);
    const double worldPx = n * kTilePx;
    const DtmSampler sample = [&tiles, worldPx](double lon, double lat, double& meters)
    {
        const double fx = (lon + 180.0) / 360.0 * worldPx;
        const double fy = (1.0 - std::asinh(std::tan(lat * kPi / 180.0)) / kPi) / 2.0 * worldPx;
        const double px = fx - 0.5, py = fy - 0.5;       // the pixel centres
        const qint32 i0 = qint32(std::floor(px)), j0 = qint32(std::floor(py));
        const double tx = px - i0, ty = py - j0;
        const qint32 tx0 = qint32(std::floor((fx - 0.5) / kTilePx)), ty0 = qint32(std::floor((fy - 0.5) / kTilePx));
        float v00, v10, v01, v11;
        if (!mosaicPixel(tiles, tx0, ty0, i0 - tx0 * kTilePx, j0 - ty0 * kTilePx, v00)
            || !mosaicPixel(tiles, tx0, ty0, i0 + 1 - tx0 * kTilePx, j0 - ty0 * kTilePx, v10)
            || !mosaicPixel(tiles, tx0, ty0, i0 - tx0 * kTilePx, j0 + 1 - ty0 * kTilePx, v01)
            || !mosaicPixel(tiles, tx0, ty0, i0 + 1 - tx0 * kTilePx, j0 + 1 - ty0 * kTilePx, v11))
            return false;
        meters = (1.0 - ty) * ((1.0 - tx) * v00 + tx * v10) + ty * ((1.0 - tx) * v01 + tx * v11);
        return true;
    };

    DtmBuildParams p;
    p.lon0 = lon0; p.lat0 = lat0; p.lon1 = lon1; p.lat1 = lat1;
    p.pixel = 2.0 * kPi * kEarthR / worldPx;             // the tile pixel in the Web Mercator plane
    p.smoothMeters = 80.0;
    p.maxRaise = 150.0;
    p.sourceNote = QString::fromLatin1(kTerrainNote);
    const QString file = outFile;
    QDir().mkpath(QFileInfo(file).absolutePath());       // the matrix lies next to the data file
    DtmBuildResult res;
    QString err;
    if (!tmcBuildDtm(p, sample, file, &res, &err)) { R.error = err; return R; }
    R.ok = true;
    R.file = file;
    R.width = res.width;
    R.height = res.height;
    R.tiles = tiles.size();
    R.reliefMeters = res.reliefMeters;
    return R;
}
