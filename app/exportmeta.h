#pragma once
// Info about an export: written into the world archive as export_info.json (the root) and into the "export"
// section of the .tmc project file. It NEVER contains machine paths (the source file/folder, output, saves,
// rules, enhancer): the archive is meant to be shared, the paths are private.
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <QString>

struct TmcRunOptions;

namespace TmcExportMeta
{
//! The program version from tMC.pro, e.g. "1.1.26.10305"
QString version();

struct Params
{
    QString format = QStringLiteral("26.x");        //!< "26.x" | "1.21.x"
    bool noiseWorld = true, caves = true, datapack = true, realMeters = true;
    bool greenery = true, enhancer = true;
    quint64 seed = 0;                               //!< the finally resolved seed ("0" — random at export)
    QString climate = QStringLiteral("auto");       //!< "auto" or a Climate::key() key of the chosen zone
    int climateResolved = -1;                       //!< the Climate::Zone actually used; -1 — not exported yet (a project file)
    QJsonArray dataBbox;                            //!< [west, south, east, north] in degrees; empty — unknown
    QJsonArray clip;                                //!< the export boundary [[lon,lat]…]; empty — the whole area
    QJsonArray spawn;                               //!< [lon,lat]; empty — unknown
    QJsonObject marks;                              //!< "stronghold"/"city"/"paleForest": {"chunk":"…"} and/or {"lonLat":[lon,lat]}
};

//! The JSON of the info (the same object goes into the zip and into the project file)
QJsonObject build(const Params& p);
//! Indented JSON text for writing into a file
QByteArray toJson(const Params& p);

//! Corners of the loaded data extent (data projection coordinates) → [west, south, east, north]; empty — the projection could not be converted
QJsonArray bboxLonLat(const QString& wkt, double x0, double y0, double x1, double y1);

//! Fills the params from the export options (its boundary/marks/spawn are in the data projection coordinates)
Params fromRun(const TmcRunOptions& o, const QString& wkt,
               double x0, double y0, double x1, double y1,
               quint64 seed, int climateResolved, bool hasSpawn, double spawnX, double spawnY);
}
