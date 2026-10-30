#pragma once
// The full export cycle: load a project/folder -> 3D settings -> objects -> a Minecraft world (zip). Shared by the window and the command line.
#include <QImage>
#include <QString>
#include <functional>
#include <atomic>
#include "export/mcworldexporter.h"

struct TmcRunOptions
{
    QString source;                       //!< a .tqt project file, an OSM extract (.osm/.pbf) or a folder with shapefiles; empty — the current folder
    QString t3dPath;                      //!< .t3d settings file; empty — look for default.t3d (current folder, next to the exe, source folder), otherwise Wdf3D= from the project
    QString outPath;                      //!< world zip; empty — <project name>.zip in the current folder
    QString savesDir;                     //!< GUI export setting: save the world UNPACKED into this "saves" folder (launcher/Modrinth/CurseForge); empty — the zip of outPath
    QString osmRulesPath;                 //!< JSON with the "OSM tags -> layer/code" rules (for an .osm/.pbf source); empty — osm_rules.json next to the file, the exe or in the current folder
    QString enhancerPath;                 //!< enhancer file tmc_buildings.dat; empty — look next to the exe / in the current folder
    bool noEnhancer = false;              //!< do not use the enhancer
    bool noGreenery = false;              //!< do not top up vegetation for OSM data (yards, taiga, forests, parks)
    bool dryRun = false;                 //!< only load and classify (log/code summary), the world is not built
    QString osmIniPath;                  //!< a custom osmconf.ini for the OSM driver; empty — a temporary ini built from the tags used by the rules
    McWorldExporter::Format format = McWorldExporter::Format::Native26x;
    bool noiseWorld = true;               //!< a normal seed-based world outside the area (false — superflat)
    bool caves = true;
    quint64 seed = 0;                     //!< 0 — random
    bool locateDatapack = true;           //!< datapack for /locate and the eye of ender (stronghold, Ancient City, trial chambers, ruined portals); without it the game does not ask for experimental features
    bool realMeters = true;               //!< convert map units to meters by the projection
    QString strongholdChunk, cityChunk, paleForestChunk;   //!< F3 format "x y z" / "x z"; empty — automatic
    bool hasSpawn = false;                //!< a spawn point is set (otherwise — the data center)
    double spawnX = 0, spawnY = 0;        //!< spawn point in the data project coordinates (chosen by a click in the preview)
    QVector<QPointF> clipPolygon;         //!< export boundary — a polygon in the data project coordinates (drawn in the preview); empty — everything
    //! Entrance places chosen by a click on the map (project coordinates): the nearest church / forest. Works if the corresponding chunk is not set as text.
    bool hasStrongholdPt = false, hasCityPt = false, hasPalePt = false;
    QPointF strongholdPt, cityPt, palePt;
    int climate = -1;                     //!< climate zone (Climate::Zone); -1 — automatic by the coordinates of the data center
    QVector<QPointF> clipLonLat;        //!< the same, but WGS84 longitude/latitude (command line); converted to project coordinates after loading
    bool hasSpawnLonLat = false;        //!< the spawn point is given as WGS84 longitude/latitude (command line); converted to project coordinates after loading
    double spawnLon = 0, spawnLat = 0;
};

struct TmcRunResult
{
    bool ok = false;
    bool cancelled = false;
    QString error;
    QString outPath;
    QString summary;                      //!< ready summary text (number of objects/blocks + per-category information)
    quint64 seed = 0;
};

//! logFn(text) — log lines; progressFn(stage, done, total): stage 0 loading, 1 meshes, 2..5 exporter phases (objects, ground, chunks, zip).
TmcRunResult tmcRunExport(const TmcRunOptions& opt, const std::function<void(const QString&)>& logFn,
                          const std::function<void(int, int, int)>& progressFn, const std::atomic_bool* cancel);

class TmcProject;
struct T3dSettings;
//! Loading of the data and 3D settings (shared by the export and the preview). false + err — failed.
bool tmcLoadProject(const TmcRunOptions& opt, const std::function<void(const QString&)>& logFn, TmcProject& project, T3dSettings& settings, QString* err);

//! Basic preview: pictures of the whole area at several resolutions (no 3D) + the mapping of pixels to project coordinates for choosing the spawn point.
#include <QVector>
struct TmcPreview
{
    bool ok = false;
    QString error;
    QImage image;                               //!< overview picture (the lowest resolution, = levels[0])
    QVector<QImage> levels;                     //!< the same area at several resolutions in ascending order (for mouse-wheel zoom)
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;      //!< extent of the picture in project coordinates (the top is y1)
    double centerX = 0, centerY = 0;            //!< data center: the default spawn
    double unitsToMetersX = 1, unitsToMetersY = 1;
    QString wkt;                                //!< project CRS (for conversion to latitude/longitude)
    int objects = 0;
};
TmcPreview tmcBuildPreview(const TmcRunOptions& opt, const std::function<void(const QString&)>& logFn);
//! WGS84 longitude/latitude -> project coordinates (by wkt from TmcPreview). false — failed.
bool tmcLonLatToProject(const QString& wkt, double lon, double lat, double& x, double& y);
//! Project coordinates -> WGS84 longitude/latitude (by wkt from TmcPreview). false — failed.
bool tmcProjectToLonLat(const QString& wkt, double x, double y, double& lon, double& lat);

//! Default settings file name
extern const char* const kTmcDefaultT3dName;
//! Default OSM rules file name
extern const char* const kTmcDefaultOsmRulesName;
