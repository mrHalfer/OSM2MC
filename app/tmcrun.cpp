#include "app/tmcrun.h"
#include "app/polygoneditor.h"                       // PolygonGeom::pointOnSurface for the default spawn
#include "core/steplog.h"
#include "export/climate.h"
#include "input/tmcproject.h"
#include "input/t3dsettings.h"
#include "scene/mcscene.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QRandomGenerator>
#include <QElapsedTimer>
#include <QDateTime>
#include <QSet>
#include "ogr_spatialref.h"
#include <algorithm>
#include <memory>

const char* const kTmcDefaultT3dName = "default.t3d";
const char* const kTmcDefaultOsmRulesName = "osm_rules.json";

//! Any .t3d in the shapefile folder (if there are several — the first alphabetically)
static QString findT3dInFolder(const QString& dir)
{
    const QStringList l = QDir(dir).entryList(QStringList() << QStringLiteral("*.t3d"), QDir::Files, QDir::Name);
    return l.isEmpty() ? QString() : QDir(dir).filePath(l.first());
}

//! Fallback default.t3d: next to the exe, then in the current folder
static QString findDefaultT3d()
{
    for (const QString& d : { QCoreApplication::applicationDirPath(), QDir::currentPath() })
    {
        const QString p = QDir(d).filePath(QString::fromLatin1(kTmcDefaultT3dName));
        if (QFileInfo::exists(p)) return p;
    }
    return QString();
}

bool tmcLoadProject(const TmcRunOptions& opt, const std::function<void(const QString&)>& logFn, TmcProject& project, T3dSettings& settings, QString* err)
{
    auto log = [&](const QString& s) { if (logFn) logFn(s); };
    auto fail = [&](const QString& e) { if (err) *err = e; log(QObject::tr("Error: %1").arg(e)); return false; };

    QString source = opt.source.isEmpty() ? QDir::currentPath() : opt.source;
    const QFileInfo srcInfo(source);
    const bool isTqt = srcInfo.isFile() && srcInfo.suffix().compare(QLatin1String("tqt"), Qt::CaseInsensitive) == 0;
    if (!srcInfo.exists()) return fail(QObject::tr("Source not found: %1").arg(source));

    // OSM extract: an .osm/.pbf file or a folder without shapefiles but with such a file
    QString osmFile;
    if (srcInfo.isFile() && TmcProject::isOsmFile(source)) osmFile = srcInfo.absoluteFilePath();
    else if (srcInfo.isDir() && QDir(source).entryList(QStringList() << QStringLiteral("*.shp"), QDir::Files).isEmpty())
    {
        const QStringList o = QDir(source).entryList(QStringList() << QStringLiteral("*.osm") << QStringLiteral("*.pbf") << QStringLiteral("*.osm.bz2"), QDir::Files, QDir::Name);
        if (!o.isEmpty()) osmFile = QDir(source).filePath(o.first());
    }
    const QString sourceDir = srcInfo.isDir() ? srcInfo.absoluteFilePath() : srcInfo.absolutePath();

    // --- 3D settings ---
    QByteArray t3dJson;
    QString t3dUsed;
    QString t3dPath = opt.t3dPath;
    if (t3dPath.isEmpty() && (srcInfo.isDir() || !osmFile.isEmpty())) t3dPath = findT3dInFolder(sourceDir);
    if (t3dPath.isEmpty() && isTqt)
    {   // settings stored in the tqt project itself (the Wdf3D= line)
        t3dJson = TmcProject::peekEmbeddedT3d(srcInfo.absoluteFilePath());
        if (!t3dJson.isEmpty()) t3dUsed = QObject::tr("(from the project)");
    }
    if (t3dPath.isEmpty() && t3dJson.isEmpty()) t3dPath = findDefaultT3d();
    if (!t3dPath.isEmpty())
    {
        QFile f(t3dPath);
        if (!f.open(QIODevice::ReadOnly)) return fail(QObject::tr("Could not open the 3D settings file: %1").arg(t3dPath));
        t3dJson = f.readAll();
        t3dUsed = t3dPath;
    }
    if (t3dJson.isEmpty())
        return fail(QObject::tr("3D settings file not found: specify --t3d, put a .t3d into the shapefile folder or put %1 next to the program.")
                        .arg(QString::fromLatin1(kTmcDefaultT3dName)));

    // --- project ---
    project.setLogger(log);
    project.setEnhancer(opt.enhancerPath, opt.noEnhancer);
    project.setGreeneryOff(opt.noGreenery);
    QString e;
    if (!parseT3dSettings(t3dJson, settings, &e)) return fail(e);
    {
        QSet<QString> want;
        for (const T3dSource& s : settings.sources) want.insert(s.layer.toLower());
        project.setWantedSuffixes(want);
    }
    log(QObject::tr("Loading: %1").arg(QDir::toNativeSeparators(srcInfo.absoluteFilePath())));
    if (!osmFile.isEmpty())
    {
        QString rules = opt.osmRulesPath;
        if (rules.isEmpty())
            for (const QString& d : { QFileInfo(osmFile).absolutePath(), QCoreApplication::applicationDirPath(), QDir::currentPath() })
            {
                const QString p = QDir(d).filePath(QString::fromLatin1(kTmcDefaultOsmRulesName));
                if (QFileInfo::exists(p)) { rules = p; break; }
            }
        if (rules.isEmpty()) return fail(QObject::tr("OSM rules file (%1) not found: specify --osm-rules or put it next to the program.").arg(QString::fromLatin1(kTmcDefaultOsmRulesName)));
        if (!project.loadOsm(osmFile, rules, opt.osmIniPath, &e)) return fail(e);
    }
    else if (isTqt ? !project.loadTqt(srcInfo.absoluteFilePath(), &e) : !project.loadFolder(srcInfo.absoluteFilePath(), &e))
        return fail(e);
    // terrain: DTM.tif from the working folder, then from the shapefile/project folder (if the project did not bring a height matrix itself)
    if (!project.addDefaultDtm(QDir::currentPath()))
        if (!project.addDefaultDtm(sourceDir))
            log(QObject::tr("Height matrix DTM.tif not found — the terrain is flat."));
    log(QObject::tr("3D settings: %1").arg(t3dUsed));
    QVector<QPointF> clip = opt.clipPolygon;
    if (clip.isEmpty())
        for (const QPointF& ll : opt.clipLonLat)
        {
            double x, y;
            if (project.lonLatToProject(ll.x(), ll.y(), x, y)) clip << QPointF(x, y);
        }
    if (clip.size() >= 3)
    {
        const int n = project.clipTo(clip);
        log(QObject::tr("Polygon boundary (%1 vertices): objects discarded outside it: %2").arg(clip.size()).arg(n));
        double a, b, c, d;
        if (!project.bounds(a, b, c, d)) return fail(QObject::tr("There are no objects inside the boundary."));
    }
    return true;
}

TmcRunResult tmcRunExport(const TmcRunOptions& optIn, const std::function<void(const QString&)>& logFn,
                          const std::function<void(int, int, int)>& progressFn, const std::atomic_bool* cancel)
{
    TmcRunResult R;
    auto log = [&](const QString& s) { if (logFn) logFn(s); };
    auto progress = [&](int stage, int done, int total) { if (progressFn) progressFn(stage, done, total); };
    auto fail = [&](const QString& e) { R.ok = false; R.error = e; log(QObject::tr("Error: %1").arg(e)); return R; };

    const TmcRunOptions opt = optIn;
    tmcStepLog("run: start");
    QElapsedTimer tAll; tAll.start();                                      // the whole export: loading, scene building, world writing
    progress(0, 0, 0);
    TmcProject project;
    T3dSettings settings;
    QString err;
    if (!tmcLoadProject(opt, log, project, settings, &err)) { R.ok = false; R.error = err; return R; }
    if (opt.dryRun)
    {
        R.ok = true;
        R.summary = QObject::tr("Dry run (--dry-run): the data was loaded, the world was not built.");
        log(R.summary);
        return R;
    }
    for (const QString& w : project.warnings()) log(w);

    // --- map units -> meters ---
    McSceneOptions sopt;
    double bx0, by0, bx1, by1;
    double ux = 1.0, uy = 1.0;
    if (project.bounds(bx0, by0, bx1, by1) && opt.realMeters)
    {
        if (metersPerMapUnit(project.wkt(), 0.5 * (bx0 + bx1), 0.5 * (by0 + by1), ux, uy)
            && (std::abs(ux - 1.0) > 0.002 || std::abs(uy - 1.0) > 0.002))
        {
            sopt.unitsToMetersX = ux; sopt.unitsToMetersY = uy;
            log(QObject::tr("Real meters: x%1 along X, x%2 along Y").arg(ux, 0, 'f', 3).arg(uy, 0, 'f', 3));
        }
    }

    // --- objects ---
    McSceneResult scene;
    tmcStepLog("run: data loaded");
    progress(1, 0, 0);
    if (!buildMcScene(project, settings, sopt, scene, [&](int d, int t) { progress(1, d, t); }, cancel, &err)) return fail(err);
    for (const QString& w : scene.warnings) log(w);
    if (scene.cancelled) { R.cancelled = true; R.error = QObject::tr("Export cancelled."); return R; }
    log(QObject::tr("Objects to export: %1").arg(scene.objects.size()));
    log(QObject::tr("Scene center (project projection): x=%1 y=%2 — this is block (0, 0) of the world; scale x%3 along X, x%4 along Y")
            .arg(scene.centerX, 0, 'f', 1).arg(scene.centerY, 0, 'f', 1).arg(sopt.unitsToMetersX, 0, 'f', 4).arg(sopt.unitsToMetersY, 0, 'f', 4));

    // --- world ---
    McWorldExporter::WorldOptions w;
    w.normalSurroundings = opt.noiseWorld;
    w.apronChunks = 0;
    w.caves = opt.caves;
    w.locateDatapack = opt.locateDatapack;
    if (TmcProject::isOsmFile(opt.source))
        w.attribution = QStringLiteral("Map data (c) OpenStreetMap contributors, Open Database License (ODbL) 1.0: https://www.openstreetmap.org/copyright\n"
                                       "This world was made from OpenStreetMap data with tMC. If you publish it, keep this attribution and follow the ODbL.\n");
    w.strongholdChunk = opt.strongholdChunk;
    w.cityChunk = opt.cityChunk;
    w.paleForestChunk = opt.paleForestChunk;
    w.clipPolygon = opt.clipPolygon;
    if (w.clipPolygon.isEmpty())
        for (const QPointF& ll : opt.clipLonLat)             // a boundary given in longitudes/latitudes (--clip, the online map): into the project projection, as in tmcLoadProject
        {
            double x, y;
            if (project.lonLatToProject(ll.x(), ll.y(), x, y)) w.clipPolygon << QPointF(x, y);
        }
    if (opt.climate >= 0) { w.climateZone = opt.climate; log(QObject::tr("Climate zone: %1 (chosen manually)").arg(Climate::name(opt.climate))); }
    else
    {   // automatic: by the latitude/longitude of the data center
        double lon = 0, lat = 0;
        if (tmcProjectToLonLat(project.wkt(), scene.centerX, scene.centerY, lon, lat))
        {
            w.climateZone = Climate::fromLonLat(lon, lat);
            log(QObject::tr("Climate zone: %1 (automatic: longitude %2, latitude %3)").arg(Climate::name(w.climateZone)).arg(lon, 0, 'f', 2).arg(lat, 0, 'f', 2));
        }
    }
    w.unitsToMetersX = sopt.unitsToMetersX;
    w.unitsToMetersY = sopt.unitsToMetersY;
    w.hasSpawnPoint = true;
    w.spawnMapX = scene.centerX;
    w.spawnMapY = scene.centerY;
    if (!w.clipPolygon.isEmpty())
    {   // default spawn — a point INSIDE the drawn boundary, like ST_PointOnSurface in PostGIS (the centroid when it
        // falls inside, otherwise the middle of the longest inside span): a very long object sticking out of the
        // boundary can drag the data center (block 0, 0) outside the generated area, and the bounding-box center of
        // an irregular boundary lies outside its outline.
        QPointF sp;
        if (PolygonGeom::pointOnSurface(w.clipPolygon, sp))
        {
            w.spawnMapX = sp.x();
            w.spawnMapY = sp.y();
        }
    }
    {   // the spawn point chosen by the user (a click in the preview or --spawn-lonlat); otherwise the data center
        double sx = opt.spawnX, sy = opt.spawnY;
        bool have = opt.hasSpawn;
        if (!have && opt.hasSpawnLonLat) have = project.lonLatToProject(opt.spawnLon, opt.spawnLat, sx, sy);
        if (opt.hasSpawnLonLat && !have) log(QObject::tr("The spawn longitude/latitude could not be converted to the project projection — spawn at the data center."));
        if (have)
        {
            w.spawnMapX = sx; w.spawnMapY = sy;
            log(QObject::tr("Spawn point: x=%1 y=%2 (project coordinates)").arg(sx, 0, 'f', 1).arg(sy, 0, 'f', 1));
        }
    }
    {   // entrance places chosen on the map -> world chunks (block (0, 0) is the scene center); "~" — take the nearest church without a distance limit
        auto chunkOf = [&](const QPointF& pt)
        {
            const double bx = (pt.x() - scene.centerX) * sopt.unitsToMetersX, bz = (scene.centerY - pt.y()) * sopt.unitsToMetersY;
            return QStringLiteral("~%1 %2").arg(int(std::floor(bx / 16.0))).arg(int(std::floor(bz / 16.0)));
        };
        if (w.strongholdChunk.isEmpty() && opt.hasStrongholdPt) w.strongholdChunk = chunkOf(opt.strongholdPt);
        if (w.cityChunk.isEmpty() && opt.hasCityPt) w.cityChunk = chunkOf(opt.cityPt);
        if (w.paleForestChunk.isEmpty() && opt.hasPalePt) w.paleForestChunk = chunkOf(opt.palePt);
    }
    quint64 seed = opt.seed;
    if (opt.noiseWorld && seed == 0) seed = (QRandomGenerator::global()->generate64() >> 2) + 1;
    w.seed = seed;
    R.seed = seed;

    QString out = opt.outPath;
    if (!opt.savesDir.isEmpty())
    {   // export setting: the world goes UNPACKED into the chosen saves folder (launcher, Modrinth/CurseForge instance), no zip
        QString base = project.projectBaseName();
        if (base.isEmpty()) base = QStringLiteral("tmc_export");
        if (opt.format == McWorldExporter::Format::Legacy121x) base += QStringLiteral("_legacy");
        out = QDir(opt.savesDir).filePath(base);
        if (QFileInfo::exists(out))       // the same rule as for the zip: an existing save is not replaced, the new one gets the export date and time
            out += QLatin1Char('_') + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        w.unpackedFolder = true;
    }
    else if (out.isEmpty())
    {
        QString base = project.projectBaseName();
        if (base.isEmpty()) base = QStringLiteral("tmc_export");
        if (opt.format == McWorldExporter::Format::Legacy121x) base += QStringLiteral("_legacy");
        // by default next to the source data: the folder of the project/OSM file or the chosen folder itself (no source — the current folder)
        const QFileInfo si(opt.source);
        const QString dir = opt.source.isEmpty() ? QDir::currentPath() : (si.isDir() ? si.absoluteFilePath() : si.absolutePath());
        out = QDir(dir).filePath(base + QStringLiteral(".zip"));
        if (QFileInfo::exists(out))       // re-export of the same data: the previous world stays, the new one gets the export date and time
        {
            const QFileInfo oi(out);
            out = QDir(oi.absolutePath()).filePath(oi.completeBaseName() + QLatin1Char('_')
                  + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".zip"));
        }
    }
    R.outPath = QDir::toNativeSeparators(QFileInfo(out).absoluteFilePath());
    QDir().mkpath(QFileInfo(out).absolutePath());                       // the folder of the world file is created if it does not exist
    log(QObject::tr("Writing the world: %1").arg(R.outPath));

    tmcStepLog("run: scene built");
    if (qEnvironmentVariableIsSet("MCX_STOP_AFTER_SCENE")) { R.ok = true; R.summary = QStringLiteral("MCX_STOP_AFTER_SCENE: world export skipped"); return R; }   // debugging: time the loading and the scene without exporting
    const McWorldExporter::Result res = McWorldExporter::Export(
        scene.objects, scene.sampler, R.outPath,
        w.unpackedFolder ? QFileInfo(out).fileName() : QFileInfo(out).completeBaseName(),      // a save folder name may contain dots
        cancel, opt.format,
        [&](int done, int total, int stage) { progress(2 + stage, done, total); }, w);
    if (res.cancelled) { R.cancelled = true; R.error = QObject::tr("Export cancelled."); return R; }
    if (!res.ok) return fail(QObject::tr("Export failed: %1").arg(res.error));

    const QString fmtName = opt.format == McWorldExporter::Format::Legacy121x ? QStringLiteral("1.21.x") : QStringLiteral("26.x");
    const qint64 secAll = tAll.elapsed() / 1000;
    const QString dur = secAll >= 3600 ? QObject::tr("%1 h %2 min").arg(secAll / 3600).arg((secAll % 3600) / 60)
                          : secAll >= 60 ? QObject::tr("%1 min %2 s").arg(secAll / 60).arg(secAll % 60)
                                         : QObject::tr("%1 s").arg(secAll);       // "Экспортировано за ..." in the report
    R.ok = true;
    if (w.unpackedFolder)
        R.summary = QObject::tr("The Minecraft world was saved unpacked:\n%1\n\nObjects: %2 (skipped: %3), blocks: %4, region files: %5. Exported in %6.\n"
                                "Open the world \"%7\" in Minecraft %8%9 from your saves.\n\n%10")
                        .arg(R.outPath).arg(res.objectCount).arg(res.skippedCount).arg(res.blockCount).arg(res.regionCount).arg(dur)
                        .arg(QFileInfo(out).fileName(), fmtName)
                        .arg(opt.noiseWorld ? QObject::tr(" (seed %1)").arg(seed) : QString())
                        .arg(formatMcExportInfo(res.lairInfo));
    else
        R.summary = QObject::tr("The Minecraft save was created:\n%1\n\nObjects: %2 (skipped: %3), blocks: %4, region files: %5. Exported in %6.\n"
                                "Unpack the archive into the saves folder and open the world in Minecraft %7%8.\n\n%9")
                        .arg(R.outPath).arg(res.objectCount).arg(res.skippedCount).arg(res.blockCount).arg(res.regionCount).arg(dur).arg(fmtName)
                        .arg(opt.noiseWorld ? QObject::tr(" (seed %1)").arg(seed) : QString())
                        .arg(formatMcExportInfo(res.lairInfo));
    log(R.summary);
    return R;
}

bool tmcLonLatToProject(const QString& wkt, double lon, double lat, double& x, double& y)
{
    OGRSpatialReference dst, wgs;
    const QByteArray b = wkt.toUtf8();
    const char* pw = b.constData();
    if (wkt.isEmpty() || dst.importFromWkt(&pw) != OGRERR_NONE || wgs.SetFromUserInput("EPSG:4326") != OGRERR_NONE) return false;
    dst.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    wgs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    std::unique_ptr<OGRCoordinateTransformation> ct(OGRCreateCoordinateTransformation(&wgs, &dst));
    if (!ct) return false;
    x = lon; y = lat;
    return ct->Transform(1, &x, &y) != 0;
}

bool tmcProjectToLonLat(const QString& wkt, double x, double y, double& lon, double& lat)
{
    OGRSpatialReference src, wgs;
    const QByteArray b = wkt.toUtf8();
    const char* pw = b.constData();
    if (wkt.isEmpty() || src.importFromWkt(&pw) != OGRERR_NONE || wgs.SetFromUserInput("EPSG:4326") != OGRERR_NONE) return false;
    src.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    wgs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    std::unique_ptr<OGRCoordinateTransformation> ct(OGRCreateCoordinateTransformation(&src, &wgs));
    if (!ct) return false;
    lon = x; lat = y;
    return ct->Transform(1, &lon, &lat) != 0;
}
