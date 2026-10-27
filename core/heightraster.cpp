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
    const int col = int(std::floor((rx - mGt[0]) / mGt[1]));
    const int row = int(std::floor((ry - mGt[3]) / mGt[5]));
    if (col < 0 || col >= xSize || row < 0 || row >= ySize) return -9999.0;
    float v;
    if (!mCache.isEmpty())
        v = mCache[qint64(row) * xSize + col];
    else
    {
        QMutexLocker lk(&mMutex);
        if (mDs->GetRasterBand(1)->RasterIO(GF_Read, col, row, 1, 1, &v, 1, 1, GDT_Float32, 0, 0) != CE_None) return -9999.0;
    }
    if (std::isnan(v)) return -9999.0;
    if (mHasNoData && std::fabs(double(v) - mNoData) < 1e-6 * std::max(1.0, std::fabs(mNoData))) return -9999.0;
    if (v <= -9998.0f) return -9999.0;
    return double(v);
}
