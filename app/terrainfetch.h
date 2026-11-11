#pragma once
// Terrain (elevation) for the online mode: the open AWS terrain tiles (terrarium) -> a height matrix DTM.tif.
// The calls are blocking — run them from a worker thread.
#include <QPointF>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>

struct TerrainFetchResult
{
    bool ok = false;
    bool cancelled = false;
    QString error;
    QString file;                   //!< the written height matrix (DTM.tif in the target folder)
    int width = 0, height = 0;
    int tiles = 0;                  //!< the tiles downloaded or taken from the cache
    double reliefMeters = 0;        //!< the real elevation range of the area
};

//! Download the elevation tiles covering the polygon (longitude/latitude) and write the height matrix to outFile
//! (named after the data file, e.g. "osm_city_20261111_120000.osm.tif"): the matrix is normalized so its minimum is
//! the current terrain level and the rest only rises (up to 150 blocks). The download is limited by a timeout: an
//! unreachable server leaves the world without terrain (the caller warns), it is not an error of the whole download.
TerrainFetchResult tmcFetchTerrain(const QVector<QPointF>& lonLat, const QString& outFile, const std::atomic_bool* cancel,
                                   const std::function<void(const QString&)>& progress = nullptr);

//! true — the file is a height matrix made by tmcFetchTerrain (its GeoTIFF metadata says so).
bool tmcIsDownloadedDtm(const QString& path);
