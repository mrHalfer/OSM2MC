// tMC — a standalone Minecraft exporter. A window (default) or the batch mode --nogui.
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QFileInfo>
#include <QTextStream>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QThread>
#include <ogr_srs_api.h>
#include "app/httpclient.h"
#include "app/i18n.h"
#include "export/climate.h"
#include "app/mainwindow.h"
#include "app/mappreview.h"
#include "app/osmdownload.h"
#include "app/osmtilemap.h"
#include "core/geoutil.h"
#include <QPushButton>
#include "app/theme.h"
#include "app/tmcrun.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>
#endif

bool g_debugLog = false;   // needed by the copied model builder (modelbuilder.cpp); always off in tMC

static void setupGdalEnvironment()
{
    // as in tqt/main.cpp: the GDAL/PROJ data lies next to the exe (if present); polygon layout is configured the same way as in tqt
    qputenv("OGR_GEOJSON_MAX_OBJ_SIZE", "500MB");
    qputenv("OGR_ORGANIZE_POLYGONS", "EXPLON");
    const QString dir = QCoreApplication::applicationDirPath();
    // The data bundled with the program ALWAYS wins over the environment: other programs (PostgreSQL/PostGIS, QGIS, Python …) put their own PROJ_LIB / GDAL_DATA into the system, and a
    // proj.db of another PROJ version makes any projection "invalid".
    auto setIfExists = [&](const char* var, const QString& sub)
    {
        const QString p = dir + QLatin1Char('/') + sub;
        if (QDir(p).exists()) qputenv(var, QDir::toNativeSeparators(p + QLatin1Char('/')).toLocal8Bit());
    };
    setIfExists("GDAL_DATA", QStringLiteral("gdal-data"));
    setIfExists("GDAL_DRIVER_PATH", QStringLiteral("gdalplugins"));
    setIfExists("PROJ_DATA", QStringLiteral("projlib"));
    setIfExists("PROJ_LIB", QStringLiteral("projlib"));
    const QString projDir = dir + QStringLiteral("/projlib");
    if (QFileInfo::exists(projDir + QStringLiteral("/proj.db")))
    {   // explicitly, not only through the variables: the search path of PROJ is set for the whole process
        static const QByteArray projPath = QDir::toNativeSeparators(projDir).toLocal8Bit();
        const char* paths[] = { projPath.constData(), nullptr };
        OSRSetPROJSearchPaths(paths);
    }
}

int main(int argc, char* argv[])
{
    // Qt shuffles the QHash iteration order on every run; in places the exporter picks the "first suitable" one by that order (for example the church for
    // the stronghold entrance) — the same input gave slightly different worlds. A fixed seed makes the world reproducible (and lets code versions be compared).
    qSetGlobalQHashSeed(0);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("EL-MODS MEDIA"));
    QCoreApplication::setApplicationName(QStringLiteral("tMC"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));
    setupGdalEnvironment();
    {   // the language must be known before any message is built: look for --lang in the raw arguments
        QString langArg;
        for (int i = 1; i + 1 < argc; ++i)
            if (QLatin1String(argv[i]) == QLatin1String("--lang") || QLatin1String(argv[i]) == QLatin1String("-lang")) langArg = QString::fromLocal8Bit(argv[i + 1]);
        tmcInstallTranslator(langArg);
    }

    QCommandLineParser p;
    p.setApplicationDescription(QObject::tr("tMC — exports tqt data (a .tqt project or the shapefiles of a folder) or OpenStreetMap data to a Minecraft world."));
    p.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);      // "-debug" as well as "--debug"
    p.addHelpOption();
    p.addVersionOption();
    p.addPositionalArgument(QStringLiteral("source"), QObject::tr("a .tqt project file, an OSM extract (.osm/.pbf) or a folder with shapefiles (default — the current folder)"));
    p.addOption({ QStringLiteral("t3d"), QObject::tr("3D settings file (.t3d); by default the .t3d from the shapefile folder, otherwise the settings of the .tqt project, otherwise default.t3d next to the program"), QStringLiteral("file") });
    p.addOption({ { QStringLiteral("o"), QStringLiteral("out") }, QObject::tr("world zip archive"), QStringLiteral("file") });
    p.addOption({ QStringLiteral("format"), QObject::tr("world version: 26 (default) or 1.21"), QStringLiteral("26|1.21") });
    p.addOption({ QStringLiteral("flat"), QObject::tr("superflat outside the area (default — a normal seed-based world)") });
    p.addOption({ QStringLiteral("seed"), QObject::tr("world seed (random by default)"), QStringLiteral("number") });
    p.addOption({ QStringLiteral("caves"), QObject::tr("caves: 1 (default) or 0"), QStringLiteral("0|1") });
    p.addOption({ QStringLiteral("real-meters"), QObject::tr("convert map units to meters using the projection: 1 (default) or 0"), QStringLiteral("0|1") });
    p.addOption({ QStringLiteral("stronghold"), QObject::tr("church chunk for the stronghold, F3 format: \"x z\" or \"x y z\""), QStringLiteral("chunk") });
    p.addOption({ QStringLiteral("city"), QObject::tr("church chunk for the Ancient City"), QStringLiteral("chunk") });
    p.addOption({ QStringLiteral("pale"), QObject::tr("a chunk inside the forest that becomes the Pale Garden"), QStringLiteral("chunk") });
    p.addOption({ QStringLiteral("osm-rules"), QObject::tr("JSON with the \"OSM tags -> layer and codes\" rules for an .osm/.pbf source (default: osm_rules.json next to the file/program)"), QStringLiteral("file") });
    p.addOption({ QStringLiteral("osm-ini"), QObject::tr("a custom osmconf.ini for the OSM driver (by default generated from the tags used by the rules)"), QStringLiteral("file") });
    p.addOption({ QStringLiteral("clip"), QObject::tr("export boundary — a polygon of WGS84 longitudes and latitudes: \"lon lat; lon lat; lon lat …\" (in the window — \"Boundary\")"), QStringLiteral("vertices") });
    p.addOption({ QStringLiteral("saves-dir"), QObject::tr("save the world UNPACKED into this saves folder (a launcher or an instance folder), no zip — the world folder <name> is created inside it (in the window — \"Export settings\")"), QStringLiteral("folder") });
    p.addOption({ QStringLiteral("log"), QObject::tr("copy the log to a text file (UTF-8), handy with --nogui"), QStringLiteral("file") });
    p.addOption({ QStringLiteral("enhancer"), QObject::tr("enhancer .dat file (fallback building type/height for OSM data); by default tmc_buildings.dat next to the program"), QStringLiteral("file") });
    p.addOption({ QStringLiteral("no-enhancer"), QObject::tr("do not use the enhancer") });
    p.addOption({ QStringLiteral("spawn-lonlat"), QObject::tr("spawn point as WGS84 longitude and latitude: \"lon lat\" (in the window — a click on the map preview)"), QStringLiteral("lon lat") });
    p.addOption({ QStringLiteral("preview-png"), QObject::tr("save the map preview to a PNG and exit (no window)"), QStringLiteral("file") });
    p.addOption({ QStringLiteral("gui-screenshot"), QObject::tr("(for testing) open the window, load the map, save a window screenshot to a PNG and exit"), QStringLiteral("file") });
    p.addOption({ QStringLiteral("gui-click"), QObject::tr("(for testing, with --gui-screenshot) \"click\" on the map: fractions of width and height, for example \"0.4 0.6\""), QStringLiteral("x y") });
    p.addOption({ QStringLiteral("no-locate-datapack"), QObject::tr("do not put the /locate and eye-of-ender datapack in the world (stronghold, Ancient City …): the game will not ask for experimental features, but these structures cannot be found with a command") });
    p.addOption({ QStringLiteral("no-greenery"), QObject::tr("do not top up vegetation for OSM data (yards, taiga, forests, parks)") });
    p.addOption({ QStringLiteral("dry-run"), QObject::tr("only load and classify the data (summary in the log), do not build the world") });
    p.addOption({ QStringLiteral("theme"), QObject::tr("window theme: auto (as in Windows, default), light or dark"), QStringLiteral("auto|light|dark") });
    p.addOption({ QStringLiteral("debug"), QObject::tr("show the service fields in the window: paths to .t3d, the OSM rules and the enhancer, \"real meters\" (without the key the default files are always used)") });
    p.addOption({ QStringLiteral("gui-online"), QObject::tr("(for testing, with --gui-screenshot) online mode: \"lon lat zoom; lon lat; lon lat; …\" — map view and boundary vertices"), QStringLiteral("view") });
    p.addOption({ QStringLiteral("download-osm"), QObject::tr("download OSM data for the polygon \"lon lat; lon lat; …\" (up to 25 km²) into the --out file and exit; the result goes to the --log file"), QStringLiteral("vertices") });
    p.addOption({ QStringLiteral("climate"), QObject::tr("climate zone: auto (by coordinates, default), temperate, boreal, tundra, steppe, mediterranean, desert, savanna, tropical, monsoon, semi-arid"), QStringLiteral("zone") });
    p.addOption({ QStringLiteral("net-test"), QObject::tr("(for testing) download a test OSM tile and a Nominatim reply, write the response codes to a file and exit"), QStringLiteral("file") });
    p.addOption({ QStringLiteral("gui-switch-lang"), QObject::tr("(for testing, with --gui-screenshot) switch the interface language at run time (en or ru) before the screenshot; the saved choice is not changed"), QStringLiteral("en|ru") });
    p.addOption({ QStringLiteral("gui-project"), QObject::tr("(for testing, with --gui-screenshot) \"open:file\" or \"save:file\" — open or save a project file around the screenshot"), QStringLiteral("action:file") });
    p.addOption({ QStringLiteral("lang"), QObject::tr("interface language: auto (the system one, default), en or ru; not saved"), QStringLiteral("auto|en|ru") });
    p.addOption({ QStringLiteral("nogui"), QObject::tr("no window: run the export and exit (code 0 — success)") });
    p.process(app);

    TmcRunOptions o;
    if (!p.positionalArguments().isEmpty()) o.source = p.positionalArguments().first();
    o.t3dPath = p.value(QStringLiteral("t3d"));
    o.outPath = p.value(QStringLiteral("out"));
    o.savesDir = p.value(QStringLiteral("saves-dir"));
    o.osmRulesPath = p.value(QStringLiteral("osm-rules"));
    o.dryRun = p.isSet(QStringLiteral("dry-run"));
    o.enhancerPath = p.value(QStringLiteral("enhancer"));
    o.noEnhancer = p.isSet(QStringLiteral("no-enhancer"));
    o.noGreenery = p.isSet(QStringLiteral("no-greenery"));
    o.locateDatapack = !p.isSet(QStringLiteral("no-locate-datapack"));
    if (p.isSet(QStringLiteral("spawn-lonlat")))
    {
        const QStringList ll = p.value(QStringLiteral("spawn-lonlat")).split(QRegularExpression(QStringLiteral("[ ,;]+")), Qt::SkipEmptyParts);
        bool ok1 = false, ok2 = false;
        if (ll.size() == 2) { o.spawnLon = ll[0].toDouble(&ok1); o.spawnLat = ll[1].toDouble(&ok2); }
        o.hasSpawnLonLat = ok1 && ok2;
        if (!o.hasSpawnLonLat) fprintf(stderr, "--spawn-lonlat: expected \"longitude latitude\"\n");
    }
    if (p.isSet(QStringLiteral("clip")))
    {
        for (const QString& pt : p.value(QStringLiteral("clip")).split(QLatin1Char(';'), Qt::SkipEmptyParts))
        {
            const QStringList ll = pt.split(QRegularExpression(QStringLiteral("[ ,]+")), Qt::SkipEmptyParts);
            bool ok1 = false, ok2 = false;
            if (ll.size() == 2) { const double lo = ll[0].toDouble(&ok1), la = ll[1].toDouble(&ok2); if (ok1 && ok2) o.clipLonLat << QPointF(lo, la); }
            if (!(ok1 && ok2)) fprintf(stderr, "--clip: could not parse the vertex \"%s\"\n", pt.toLocal8Bit().constData());
        }
    }
    if (p.isSet(QStringLiteral("climate")) && p.value(QStringLiteral("climate")).compare(QLatin1String("auto"), Qt::CaseInsensitive) != 0)
    {
        o.climate = Climate::fromKey(p.value(QStringLiteral("climate")));
        if (o.climate < 0) fprintf(stderr, "--climate: unknown zone \"%s\"\n", p.value(QStringLiteral("climate")).toLocal8Bit().constData());
    }
    o.osmIniPath = p.value(QStringLiteral("osm-ini"));
    o.format = (p.value(QStringLiteral("format")) == QLatin1String("1.21")) ? McWorldExporter::Format::Legacy121x : McWorldExporter::Format::Native26x;
    o.noiseWorld = !p.isSet(QStringLiteral("flat"));
    o.seed = p.value(QStringLiteral("seed")).toULongLong();
    o.caves = (p.isSet(QStringLiteral("caves")) ? p.value(QStringLiteral("caves")) : QStringLiteral("1")) != QLatin1String("0");
    o.realMeters = (p.isSet(QStringLiteral("real-meters")) ? p.value(QStringLiteral("real-meters")) : QStringLiteral("1")) != QLatin1String("0");
    o.strongholdChunk = p.value(QStringLiteral("stronghold"));
    o.cityChunk = p.value(QStringLiteral("city"));
    o.paleForestChunk = p.value(QStringLiteral("pale"));

    if (p.isSet(QStringLiteral("download-osm")))
    {   // download an area without a window: "lon lat; lon lat; …" (not larger than the allowed area) -> the --out file; the result goes to the --log file
        QVector<QPointF> pts;
        for (const QString& s : p.value(QStringLiteral("download-osm")).split(QLatin1Char(';'), Qt::SkipEmptyParts))
        {
            const QStringList c = s.split(QRegularExpression(QStringLiteral("[ ,]+")), Qt::SkipEmptyParts);
            if (c.size() == 2) pts << QPointF(c[0].toDouble(), c[1].toDouble());
        }
        QString msg;
        int rc = 1;
        QString why;
        if (!tmcOnlineClipAllowed(pts, &why)) msg = QStringLiteral("the boundary is not suitable: %1").arg(why);
        else if (o.outPath.isEmpty()) msg = QStringLiteral("--out is not set");
        else
        {
            const OsmDownloadResult r = tmcDownloadOsm(pts, o.outPath, nullptr);
            msg = r.ok ? QStringLiteral("ok %1 bytes").arg(r.bytes) : QStringLiteral("error: %1").arg(r.error);
            rc = r.ok ? 0 : 1;
        }
        QFile lf(p.value(QStringLiteral("log")));
        if (lf.open(QIODevice::WriteOnly | QIODevice::Text)) lf.write(msg.toUtf8());
        return rc;
    }
    if (p.isSet(QStringLiteral("net-test")))
    {
        const HttpResult tile = httpRequest(QStringLiteral("https://tile.openstreetmap.org/3/4/2.png"));
        const HttpResult geo = httpRequest(QStringLiteral("https://nominatim.openstreetmap.org/search?format=jsonv2&limit=2&accept-language=ru&q=%D0%92%D0%BE%D1%80%D0%BE%D0%BD%D0%B5%D0%B6"));
        QFile out(p.value(QStringLiteral("net-test")));                           // the application is windowed: the result goes to a file
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write(QStringLiteral("tile: status=%1 bytes=%2 %3\nnominatim: status=%4 %5\n%6\n").arg(tile.status).arg(tile.body.size()).arg(tile.error)
                          .arg(geo.status).arg(geo.error, QString::fromUtf8(geo.body.left(300))).toUtf8());
        return (tile.ok() && geo.ok()) ? 0 : 1;
    }
    if (p.isSet(QStringLiteral("preview-png")))
    {   // preview without a window: the same picture as in the window — for checks and scripts
        const TmcPreview pv = tmcBuildPreview(o, [](const QString& s) { fprintf(stderr, "%s\n", s.toLocal8Bit().constData()); });
        if (!pv.ok) { fprintf(stderr, "%s\n", pv.error.toLocal8Bit().constData()); return 1; }
        return pv.image.save(p.value(QStringLiteral("preview-png"))) ? 0 : 1;
    }
    if (p.isSet(QStringLiteral("nogui")))
    {
#ifdef Q_OS_WIN
        if (AttachConsole(ATTACH_PARENT_PROCESS))
        {   // the application is built as a GUI one — attach to the parent console so the log is visible
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
        }
#endif
        QTextStream console(stdout);
        console.setCodec("UTF-8");
        QFile logFile(p.value(QStringLiteral("log")));
        QTextStream fileOut;
        if (p.isSet(QStringLiteral("log")) && logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            fileOut.setDevice(&logFile);
            fileOut.setCodec("UTF-8");
        }
        QMutex sayMutex;                                    // the log and progress come from worker threads, and QTextStream is not thread-safe
        auto say = [&](const QString& s)
        {
            QMutexLocker lock(&sayMutex);
            if (fileOut.device()) { fileOut << s << "\n"; fileOut.flush(); }       // write the file first: the console may be closed (stdout to null)
            console << s << "\n"; console.flush();
        };
        auto lastStage = std::make_shared<int>(-1);
        auto lastPct = std::make_shared<int>(-1);
        const TmcRunResult r = tmcRunExport(o,
            [&](const QString& s) { say(s); },
            [&, lastStage, lastPct](int stage, int done, int total)
            {
                const int pct = total > 0 ? int(100.0 * done / total) : 0;
                if (stage != *lastStage) { *lastStage = stage; *lastPct = -1; say(QStringLiteral("[stage %1]").arg(stage)); }
                if (total > 0 && pct / 10 != *lastPct / 10) { *lastPct = pct; say(QStringLiteral("  %1% (%2/%3)").arg(pct).arg(done).arg(total)); }
            },
            nullptr);
        if (!r.ok && !r.cancelled) say(QObject::tr("Error: %1").arg(r.error));
        return r.ok ? 0 : 1;
    }

    const QString theme = p.value(QStringLiteral("theme")).toLower();
    TmcTheme::apply(app, theme == QLatin1String("dark") ? TmcTheme::Mode::Dark : theme == QLatin1String("light") ? TmcTheme::Mode::Light : TmcTheme::Mode::Auto);
    MainWindow w(o, p.isSet(QStringLiteral("debug")));
    w.show();
    // a test of the project file: "open:file.tmc" is read before the screenshot, "save:file.tmc" is written after it
    const QString projArg = p.isSet(QStringLiteral("gui-project")) ? p.value(QStringLiteral("gui-project")) : QString();
    const auto openProjForTest = [&]()
    {
        if (!projArg.startsWith(QLatin1String("open:"))) return;
        QString err;
        w.openProjectFrom(projArg.mid(5), &err);
        if (!err.isEmpty()) std::fprintf(stderr, "%s\n", qPrintable(err));
    };
    // a test of the language switch without restarting: the window changes the language, then the screenshot is taken (the saved choice is put back)
    const auto switchLangForTest = [&]()
    {
        if (!p.isSet(QStringLiteral("gui-switch-lang"))) return;
        const QString saved = tmcLanguageSetting();
        tmcSwitchLanguage(p.value(QStringLiteral("gui-switch-lang")));
        tmcSetLanguageSetting(saved);
        QElapsedTimer tw;
        tw.start();
        while (tw.elapsed() < 400) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(10); }      // the layouts are rebuilt from posted events
    };
    if (p.isSet(QStringLiteral("gui-screenshot")) && p.isSet(QStringLiteral("gui-online")))
    {   // online mode without a human: "lon lat zoom; lon lat; lon lat; …" — map view and boundary vertices; wait for the tiles and save a screenshot
        const QStringList parts = p.value(QStringLiteral("gui-online")).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        OsmTileMap* map = w.findChild<OsmTileMap*>();
        for (QPushButton* b : w.findChildren<QPushButton*>()) if (b->text() == QObject::tr("Online")) b->click();
        if (map && !parts.isEmpty())
        {
            const QStringList v = parts[0].split(QRegularExpression(QStringLiteral("[ ,]+")), Qt::SkipEmptyParts);
            if (v.size() == 3) map->setView(v[0].toDouble(), v[1].toDouble(), v[2].toDouble());
            QVector<QPointF> pts;
            for (int i = 1; i < parts.size(); ++i)
            {
                const QStringList c = parts[i].split(QRegularExpression(QStringLiteral("[ ,]+")), Qt::SkipEmptyParts);
                if (c.size() == 2) pts << QPointF(c[0].toDouble(), c[1].toDouble());
            }
            map->setClipLonLat(pts, pts.size() >= 3);
        }
        openProjForTest();                                      // a project file replaces the view, the boundary and the marks given above
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 8000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 50); QThread::msleep(20); }      // the tiles arrive from worker threads
        switchLangForTest();
        if (projArg.startsWith(QLatin1String("save:"))) w.saveProjectTo(projArg.mid(5));
        return w.grab().save(p.value(QStringLiteral("gui-screenshot"))) ? 0 : 1;
    }
    if (p.isSet(QStringLiteral("gui-screenshot")))
    {   // for checks without a human: load the map, with --gui-click "click" (fractions of the picture width and height) and save a window screenshot
        openProjForTest();
        QMetaObject::invokeMethod(&w, "loadPreview");
        MapPreview* pv = w.findChild<MapPreview*>(QStringLiteral("preview"));
        QElapsedTimer t;
        t.start();
        while (pv && !pv->hasPreview() && t.elapsed() < 180000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 50); QThread::msleep(20); }
        if (pv && pv->hasPreview() && p.isSet(QStringLiteral("gui-click")))
        {
            const QStringList f = p.value(QStringLiteral("gui-click")).split(QRegularExpression(QStringLiteral("[ ,;]+")), Qt::SkipEmptyParts);
            if (f.size() == 2)
            {
                const QRect r = pv->imageRect();
                const QPoint pos(r.x() + int(f[0].toDouble() * r.width()), r.y() + int(f[1].toDouble() * r.height()));
                QMouseEvent press(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(pv, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);        // the selection fires on release
                QCoreApplication::sendEvent(pv, &release);
            }
        }
        QCoreApplication::processEvents();
        switchLangForTest();
        if (projArg.startsWith(QLatin1String("save:"))) w.saveProjectTo(projArg.mid(5));
        return w.grab().save(p.value(QStringLiteral("gui-screenshot"))) ? 0 : 1;
    }
    return app.exec();
}

