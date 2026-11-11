#include "core/dtmbuild.h"
#include "gdal_priv.h"
#include "ogr_spatialref.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double kEarthR = 6378137.0;                    // the Web Mercator sphere radius (EPSG:3857)
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxSide = 4096;                           // the raster side limit (a 4096x4096 float matrix is 67 MB)

double mercX(double lon) { return kEarthR * lon * kPi / 180.0; }
double mercY(double lat) { return kEarthR * std::log(std::tan(kPi / 4.0 + lat * kPi / 360.0)); }
double mercLon(double x) { return x / kEarthR * 180.0 / kPi; }
double mercLat(double y) { return (2.0 * std::atan(std::exp(y / kEarthR)) - kPi / 2.0) * 180.0 / kPi; }
}

bool tmcBuildDtm(const DtmBuildParams& p, const DtmSampler& sample, const QString& outTif, DtmBuildResult* res, QString* err,
                 const std::function<void(int, int)>& progress)
{
    auto fail = [&](const QString& e) { if (err) *err = e; return false; };
    if (res) *res = DtmBuildResult();
    if (!sample) return fail(QStringLiteral("no elevation sampler"));
    if (!(p.pixel > 0.0)) return fail(QStringLiteral("bad pixel size"));
    if (!(p.lon1 > p.lon0) || !(p.lat1 > p.lat0)) return fail(QStringLiteral("bad extent"));
    if (p.lat0 < -85.0 || p.lat1 > 85.0) return fail(QStringLiteral("the latitude is out of the Web Mercator range"));

    // The grid is snapped to the tile pixel grid: the tile borders are multiples of the pixel size in the Mercator plane
    const double mx0 = mercX(p.lon0), mx1 = mercX(p.lon1);
    const double my0 = mercY(p.lat0), my1 = mercY(p.lat1);
    DtmGrid g;
    g.px = p.pixel;
    g.x0 = std::floor(mx0 / g.px) * g.px;
    g.yTop = std::ceil(my1 / g.px) * g.px;
    g.w = int(std::ceil((mx1 - g.x0) / g.px));
    g.h = int(std::ceil((g.yTop - my0) / g.px));
    if (g.w <= 0 || g.h <= 0) return fail(QStringLiteral("the extent is empty"));
    if (g.w > kMaxSide || g.h > kMaxSide) return fail(QStringLiteral("the extent is too large for the matrix (%1x%2)").arg(g.w).arg(g.h));
    g.z.resize(qsizetype(g.w) * g.h);

    // Sample the elevation (the pixel centre) row by row
    for (int j = 0; j < g.h; ++j)
    {
        const double lat = mercLat(g.yTop - (double(j) + 0.5) * g.px);
        for (int i = 0; i < g.w; ++i)
        {
            const double lon = mercLon(g.x0 + (double(i) + 0.5) * g.px);
            double h = 0;
            g.at(i, j) = sample(lon, lat, h) ? float(h) : std::numeric_limits<float>::quiet_NaN();
        }
        if (progress && ((j & 63) == 63 || j + 1 == g.h)) progress(j + 1, g.h);
    }

    // Holes (missing tiles at the border) -> the neighbours' mean; then the small relief goes away, the big stays
    tmcDtmFillHoles(g);
    double lo = std::numeric_limits<double>::max(), hi = -lo;
    for (const float v : g.z)
    {
        if (std::isnan(v)) continue;
        lo = std::min(lo, double(v));
        hi = std::max(hi, double(v));
    }
    if (hi < lo) return fail(QStringLiteral("no elevation data was received"));
    tmcDtmSmooth(g, p.smoothMeters);
    double scale = 1.0;
    const double relief = tmcDtmNormalize(g, p.maxRaise, &scale);

    // Write the GeoTIFF in the Web Mercator plane: the height raster is read by core/heightraster.cpp with the project CRS
    GDALAllRegister();
    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv) return fail(QStringLiteral("the GDAL GTiff driver is not available"));
    const QByteArray path = outTif.toUtf8();
    char* opts[] = { const_cast<char*>("COMPRESS=DEFLATE"), const_cast<char*>("PREDICTOR=3"), nullptr };
    GDALDataset* ds = drv->Create(path.constData(), g.w, g.h, 1, GDT_Float32, opts);
    if (!ds) return fail(QStringLiteral("could not create %1").arg(outTif));
    {
        const double gt[6] = { g.x0, g.px, 0.0, g.yTop, 0.0, -g.px };
        ds->SetGeoTransform(const_cast<double*>(gt));
        OGRSpatialReference srs;
        srs.SetFromUserInput("EPSG:3857");
        char* wkt = nullptr;
        srs.exportToWkt(&wkt);
        ds->SetProjection(wkt);
        CPLFree(wkt);
        GDALRasterBand* band = ds->GetRasterBand(1);
        band->SetNoDataValue(-9999.0);
        QVector<float> out(g.z.size());
        for (qsizetype k = 0; k < g.z.size(); ++k)
            out[k] = std::isnan(g.z[k]) ? -9999.0f : g.z[k];
        if (band->RasterIO(GF_Write, 0, 0, g.w, g.h, out.data(), g.w, g.h, GDT_Float32, 0, 0) != CE_None)
        {
            GDALClose(ds);
            return fail(QStringLiteral("could not write %1").arg(outTif));
        }
        if (!p.sourceNote.isEmpty())
            ds->SetMetadataItem("TERRAIN_SOURCE", p.sourceNote.toUtf8().constData());
    }
    GDALClose(ds);
    if (res)
    {
        res->ok = true;
        res->width = g.w;
        res->height = g.h;
        res->reliefMeters = relief;
        res->baseMeters = lo;
        res->scale = scale;
    }
    return true;
}
