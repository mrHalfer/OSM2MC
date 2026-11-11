#include "core/heightraster.h"
#include "gdal_priv.h"
#include "ogr_spatialref.h"
#include <cmath>
#include <algorithm>
#include <limits>

HeightRaster::~HeightRaster()
{
    delete mCt;
    if (mDs) GDALClose(mDs);
}

bool HeightRaster::open(const QString& path, const OGRSpatialReference* projSrs, QString* err)
{
    mPath = path;
    GDALAllRegister();
    mDs = static_cast<GDALDataset*>(GDALOpenEx(path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    if (!mDs)
    {
        if (err) *err = QStringLiteral("could not open raster %1").arg(path);
        return false;
    }
    if (mDs->GetRasterCount() < 1 || mDs->GetGeoTransform(mGt) != CE_None)
    {
        if (err) *err = QStringLiteral("raster %1 has no georeferencing or no bands").arg(path);
        return false;
    }
    xSize = mDs->GetRasterXSize();
    ySize = mDs->GetRasterYSize();
    GDALRasterBand* band = mDs->GetRasterBand(1);
    if (band->GetColorTable() || band->GetRasterDataType() == GDT_Byte)
    {   // an 8-bit picture is not a height matrix
        if (err) *err = QStringLiteral("raster %1 is not a height matrix (8-bit / palette)").arg(path);
        return false;
    }
    int hasNd = 0;
    const double nd = band->GetNoDataValue(&hasNd);
    mHasNoData = hasNd != 0;
    if (mHasNoData) mNoData = nd;

    // extent in the project CRS
    OGRSpatialReference rasterSrs;
    bool haveRasterSrs = false;
    if (mDs->GetProjectionRef() && *mDs->GetProjectionRef())
        haveRasterSrs = rasterSrs.SetFromUserInput(mDs->GetProjectionRef()) == OGRERR_NONE;
    if (haveRasterSrs) rasterSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    const double cx[4] = { mGt[0], mGt[0] + mGt[1] * xSize, mGt[0], mGt[0] + mGt[1] * xSize };
    const double cy[4] = { mGt[3], mGt[3], mGt[3] + mGt[5] * ySize, mGt[3] + mGt[5] * ySize };
    double px[4], py[4];
    for (int i = 0; i < 4; ++i) { px[i] = cx[i]; py[i] = cy[i]; }
    if (projSrs && haveRasterSrs && !projSrs->IsSame(&rasterSrs))
    {
        OGRSpatialReference* proj = projSrs->Clone();
        proj->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        OGRCoordinateTransformation* toProj = OGRCreateCoordinateTransformation(&rasterSrs, proj);
        mCt = OGRCreateCoordinateTransformation(proj, &rasterSrs);
        proj->Release();
        if (!toProj || !mCt)
        {
            delete toProj;
            if (err) *err = QStringLiteral("no coordinate transformation for raster %1").arg(path);
            return false;
        }
        for (int i = 0; i < 4; ++i)
            if (!toProj->Transform(1, &px[i], &py[i])) { delete toProj; if (err) *err = QStringLiteral("could not reproject the extent of raster %1").arg(path); return false; }
        delete toProj;
    }
    xB = *std::min_element(px, px + 4); xE = *std::max_element(px, px + 4);
    yB = *std::min_element(py, py + 4); yE = *std::max_element(py, py + 4);

    // the whole matrix in memory if it fits (up to ~400 million pixels = 1.6 GB as float — larger ones are not loaded, read per pixel)
    const qint64 pix = qint64(xSize) * ySize;
    if (pix <= 120000000ll)
    {
        mCache.resize(int(pix));
        if (band->RasterIO(GF_Read, 0, 0, xSize, ySize, mCache.data(), xSize, ySize, GDT_Float32, 0, 0) != CE_None)
            mCache.clear();
    }
    return true;
}

bool HeightRaster::pixelAt(int col, int row, double& v)
{
    if (col < 0 || col >= xSize || row < 0 || row >= ySize) return false;
    float f;
    if (!mCache.isEmpty())
        f = mCache[qint64(row) * xSize + col];
    else
    {
        QMutexLocker lk(&mMutex);
        if (mDs->GetRasterBand(1)->RasterIO(GF_Read, col, row, 1, 1, &f, 1, 1, GDT_Float32, 0, 0) != CE_None) return false;
    }
    if (std::isnan(f)) return false;
    if (mHasNoData && std::fabs(double(f) - mNoData) < 1e-6 * std::max(1.0, std::fabs(mNoData))) return false;
    if (f <= -9998.0f) return false;
    v = double(f);
    return true;
}

double HeightRaster::heightAt(double x, double y)
{
    if (!mDs) return -9999.0;
    if (x < xB || x > xE || y < yB || y > yE) return -9999.0;
    double rx = x, ry = y;
    if (mCt)
    {
        QMutexLocker lk(&mMutex);
        if (!mCt->Transform(1, &rx, &ry)) return -9999.0;
    }
    // Bilinear over the pixel centres: a height matrix is a continuous field, and the nearest-pixel reading
    // drew a 30-block staircase on every slope — a mountain terrain must not have that.
    const double fx = (rx - mGt[0]) / mGt[1] - 0.5;
    const double fy = (ry - mGt[3]) / mGt[5] - 0.5;
    const int c0 = int(std::floor(fx)), r0 = int(std::floor(fy));
    const double tx = fx - c0, ty = fy - r0;
    double v00, v10, v01, v11;
    const bool o00 = pixelAt(c0, r0, v00), o10 = pixelAt(c0 + 1, r0, v10);
    const bool o01 = pixelAt(c0, r0 + 1, v01), o11 = pixelAt(c0 + 1, r0 + 1, v11);
    if (o00 && o10 && o01 && o11)
        return (1.0 - ty) * ((1.0 - tx) * v00 + tx * v10) + ty * ((1.0 - tx) * v01 + tx * v11);
    // A hole or the matrix edge: the nearest valid pixel of the four
    double best = -9999.0, bestD = std::numeric_limits<double>::max();
    auto take = [&](bool ok, double v, double d) { if (ok && d < bestD) { bestD = d; best = v; } };
    take(o00, v00, tx * tx + ty * ty);
    take(o10, v10, (1.0 - tx) * (1.0 - tx) + ty * ty);
    take(o01, v01, tx * tx + (1.0 - ty) * (1.0 - ty));
    take(o11, v11, (1.0 - tx) * (1.0 - tx) + (1.0 - ty) * (1.0 - ty));
    return best;
}

QString HeightRaster::sourceNote() const
{
    if (!mDs) return QString();
    const char* s = mDs->GetMetadataItem("TERRAIN_SOURCE");
    return s ? QString::fromUtf8(s) : QString();
}
