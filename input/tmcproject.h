#pragma once
// Loading data for export: a tqt project file (.tqt) or all shapefiles of a folder. Pure GDAL/OGR and Qt Core only — no windows and no modal dialogs.
#include <QString>
#include <QStringList>
#include <QSet>
#include <QVector>
#include <QByteArray>
#include <memory>
#include <functional>
#include "core/maplayer.h"
#include "core/heightraster.h"

class OGRSpatialReference;
struct GreeneryOptions;
class OGRGeometry;

class TmcProject
{
public:
    TmcProject();
    ~TmcProject();
    TmcProject(const TmcProject&) = delete;
    TmcProject& operator=(const TmcProject&) = delete;

    //! Which layers to read: names without a prefix (the suffix after the last dot, case-insensitive: "bldpol", "vegpol"...). Empty — all layers. Set before load*().
    void setWantedSuffixes(const QSet<QString>& s) { mWanted = s; }
    //! Loading progress messages (optional)
    void setLogger(const std::function<void(const QString&)>& fn) { mLog = fn; }

    //! A tqt project file: Wkt, SpatialFilter, Layers (Shape/Rastr), Wdf3D. false + err — the file did not open / no layers.
    bool loadTqt(const QString& file, QString* err);
    //! A folder: all *.shp in the folder itself (terrain — addDefaultDtm). false + err — no shapefiles.
    bool loadFolder(const QString& dir, QString* err);

    //! Default height matrix: DTM.tif from the dir folder if there are no rasters yet. true — a raster exists (it was there or was added). Call after load*().
    bool addDefaultDtm(const QString& dir);
    //! WGS84 longitude/latitude -> project coordinates. false — the project CRS is unknown or there is no transformation.
    bool lonLatToProject(double lon, double lat, double& x, double& y) const;

    //! An OpenStreetMap extract (.osm / .pbf / .osm.pbf): objects are sorted by the rules of rulesFile (JSON) into virtual tqt layers (osm.bldpol …) with the CODE and other fields.
    //! iniFile — a custom osmconf.ini; empty — a temporary ini generated from the tags of the rules.
    bool loadOsm(const QString& file, const QString& rulesFile, const QString& iniFile, QString* err);
    static bool isOsmFile(const QString& path);
    //! The enhancer file (tmc_buildings.dat): fallback building type/height/floors when OSM does not allow determining them.
    //! an empty path — look for tmc_buildings.dat next to the exe / in the current folder; off=true — do not use. Set before loadOsm().
    void setEnhancer(const QString& path, bool off) { mEnhancerPath = path; mEnhancerOff = off; }
    //! Turn off the vegetation top-up for OSM data (on by default through the rules, the "greenery" block)
    void setGreeneryOff(bool off) { mGreeneryOff = off; }

    //! 3D settings from the Wdf3D= line of the project file without loading the layers (empty — none)
    static QByteArray peekEmbeddedT3d(const QString& tqtFile);

    QString wkt() const { return mWkt; }
    const OGRSpatialReference* srs() const { return mSrs.get(); }
    const QVector<MapLayer*>& layers() const { return mLayers; }
    const QVector<HeightRaster*>& rasters() const { return mRasters; }
    QByteArray embeddedT3d() const { return mEmbeddedT3d; }          //!< 3D settings from the Wdf3D= line (JSON), empty — none
    QString projectBaseName() const { return mBaseName; }
    QStringList warnings() const { return mWarnings; }

    //! Keep only the objects intersecting the polygon (project coordinates); objects on the boundary stay whole. Returns the number removed.
    int clipTo(const QVector<QPointF>& polygon);
    //! Bounds of all loaded objects in the project CRS; false — there are no objects
    bool bounds(double& x0, double& y0, double& x1, double& y1) const;
    //! A layer by name suffix ("bldpol"), nullptr — not found
    MapLayer* layerBySuffix(const QString& suffix) const;

private:
    void log(const QString& s) const { if (mLog) mLog(s); }
    //! Read a vector layer from a dataset; empty layerName — the first layer. nullptr — it did not open.
    MapLayer* readLayer(const QString& dsPath, const QString& layerName);
    void addRaster(const QString& path);
    bool finishProjectCrs();
    //! OSM data: top up vegetation (yards, private-sector taiga, forests, parks) in free places — see tmcgreen.cpp
    void synthesizeGreenery(const GreeneryOptions& opt);

    QString mEnhancerPath;
    bool mEnhancerOff = false;
    bool mGreeneryOff = false;
    QSet<QString> mWanted;
    std::function<void(const QString&)> mLog;
    QString mWkt;
    std::unique_ptr<OGRSpatialReference> mSrs;
    OGRGeometry* mFilter = nullptr;               //!< the project spatial filter (in the project CRS), owned
    QVector<MapLayer*> mLayers;
    QVector<HeightRaster*> mRasters;
    QByteArray mEmbeddedT3d;
    QString mBaseName;
    QStringList mWarnings;
    bool mCrsFromProject = false;
};
