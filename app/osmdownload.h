#pragma once
// Online OpenStreetMap data: downloading an extract for a polygon (Overpass API) and searching for places (Nominatim).
// The calls are blocking — run them from a worker thread.
#include <QPointF>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>

struct OsmDownloadResult
{
    bool ok = false;
    bool cancelled = false;
    QString error;
    qint64 bytes = 0;
    QString source;                 //!< where the data came from: "OSM API" (official) or "Overpass"
};

//! Download into outFile (.osm) all OSM objects that fall into the polygon (vertices — longitude/latitude, the boundary size has already been checked).
//! First the official OSM API (/api/0.6/map by the extent of the boundary); if it refuses (limit of 50,000 nodes / 0.25 square degrees, overload,
//! no connection) — Overpass: the mirrors in turn, when a server is busy (429/5xx) the next one is tried.
//! progress (optional) is called from the worker thread with a text about the current step — including the switch to the Overpass fallback.
OsmDownloadResult tmcDownloadOsm(const QVector<QPointF>& lonLat, const QString& outFile, const std::atomic_bool* cancel,
                                 const std::function<void(const QString&)>& progress = nullptr);

//! Overpass query text for a polygon (for checks and the log)
QString tmcOverpassQuery(const QVector<QPointF>& lonLat);

struct OsmPlace
{
    QString name;                           //!< full name ("Voronezh, Voronezh Oblast, Russia")
    double lon = 0, lat = 0;                //!< center
    double lon0 = 0, lat0 = 0, lon1 = 0, lat1 = 0;   //!< extent (degrees)
};
//! Search for a place by name (Nominatim, up to 6 variants). An empty result + error — the network/server is unavailable.
QVector<OsmPlace> tmcSearchPlaces(const QString& query, QString* error);
