#pragma once
// Height matrix (DTM) from elevation samples: the Web Mercator grid, the smoothing and the normalization
// (only raising above the current terrain), written as a GeoTIFF. The samples come from app/terrainfetch.cpp.
#include <QString>
#include <functional>
#include "core/dtmgrid.h"

struct DtmBuildParams
{
    double lon0 = 0, lat0 = 0, lon1 = 0, lat1 = 0;   //!< the extent, degrees (the boundary bbox + a margin)
    double pixel = 0;                                //!< the pixel size, EPSG:3857 meters (the tile pixel at the download zoom)
    double smoothMeters = 80.0;                      //!< the blur radius dropping the small relief, ground meters
    double maxRaise = 150.0;                         //!< the highest rise above the base, blocks
    QString sourceNote;                              //!< the attribution, written into the GeoTIFF metadata
};

//! Elevation at a longitude/latitude (WGS84 degrees); false — no data there.
using DtmSampler = std::function<bool(double lon, double lat, double& meters)>;

struct DtmBuildResult
{
    bool ok = false;
    QString error;
    int width = 0, height = 0;      //!< the written raster size
    double reliefMeters = 0;        //!< the real elevation range of the sampled area
    double baseMeters = 0;          //!< the elevation that became the base (the current terrain level)
    double scale = 1;               //!< how much the relief was compressed to fit maxRaise
};

//! Sample -> fill the holes -> smooth -> normalize -> GeoTIFF (EPSG:3857, Float32, NoData -9999).
//! progress(done, total) is called per row of the grid.
bool tmcBuildDtm(const DtmBuildParams& p, const DtmSampler& sample, const QString& outTif, DtmBuildResult* res, QString* err,
                 const std::function<void(int, int)>& progress = nullptr);
