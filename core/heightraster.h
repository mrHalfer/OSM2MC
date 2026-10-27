#pragma once
// Height matrix (DTM): GDAL, nearest pixel, project coordinates -> raster CRS. One instance is one raster.
#include <QString>
#include <QVector>
#include <QMutex>
#include <memory>

class GDALDataset;
class OGRSpatialReference;
class OGRCoordinateTransformation;

class HeightRaster
{
public:
    double xB = 0, xE = 0, yB = 0, yE = 0;   //!< matrix extent in the project coordinate system (corners; yB/yE may come in any order)
    int xSize = 0, ySize = 0;                //!< raster size in pixels

    HeightRaster() = default;
    HeightRaster(const HeightRaster&) = delete;
    HeightRaster& operator=(const HeightRaster&) = delete;
    ~HeightRaster();

    //! Open a raster; projSrs is the project CRS (may be nullptr: then coordinates are taken in the raster CRS). false + err — did not open / not a height matrix
    bool open(const QString& path, const OGRSpatialReference* projSrs, QString* err = nullptr);

    //! Height at the project point (x, y); -9999 — outside the matrix / no data
    double heightAt(double x, double y);


    QString path() const { return mPath; }

private:
    QString mPath;
    GDALDataset* mDs = nullptr;
    OGRCoordinateTransformation* mCt = nullptr;   //!< project -> raster CRS (nullptr if the systems coincide)
    double mGt[6] = { 0, 1, 0, 0, 0, 1 };
    bool mHasNoData = false;
    double mNoData = -9999.0;
    QVector<float> mCache;                        //!< the whole matrix in memory (if not too large)
    QMutex mMutex;                                //!< protects the coordinate transformation / per-pixel raster reads
};
