# OSMC — a standalone Minecraft exporter: reads a tqt project (.tqt), shapefiles from a folder or an OpenStreetMap extract, builds a model from the .t3d settings and writes a Minecraft world (zip).
#
# Layout:
#   app/        window, command line, the full export cycle
#   input/      data loading (tqt, shapefile folder, OSM) and 3D settings (.t3d)
#   osm/        "OSM tags -> layer/code" rules, building enhancer, heights, vegetation top-up
#   scene/      export object assembly and the mesh builder
#   export/     Minecraft world exporter (voxels, chunks, NBT, zip)
#   core/       base classes: object, layer, height matrix
#   resources/  icon, default settings, OSM rules, height table, enhancer, *.bin templates
TEMPLATE = app
TARGET = osmc

DEPENDPATH += .
INCLUDEPATH += .

# GDAL: on Windows the headers and the import library gdal_i.lib are in third_party/gdal (the project builds out of the box); another GDAL can be used
# through the GDAL_DIR environment variable (a directory with include and lib subfolders). The DLLs are needed only to run (see README).
# On Linux GDAL comes from the system (libgdal-dev, 3.4 API); GDAL_DIR may point to a custom build with include/ and lib/libgdal.so.
OSMC_GDAL = $$(GDAL_DIR)
win32:{
    isEmpty(OSMC_GDAL): OSMC_GDAL = $$PWD/third_party/gdal
    INCLUDEPATH += $$OSMC_GDAL/include
    LIBS += $$OSMC_GDAL/lib/gdal_i.lib
}
unix:{
    !isEmpty(OSMC_GDAL) {
        INCLUDEPATH += $$OSMC_GDAL/include
        LIBS += -L$$OSMC_GDAL/lib -lgdal
    } else {
        # the system libgdal-dev: pkg-config gives the header folder (on Ubuntu the headers are in /usr/include/gdal,
        # which is not in the default include path); without gdal.pc — the classic layout with -lgdal
        system(pkg-config --exists gdal) {
            CONFIG += link_pkgconfig
            PKGCONFIG += gdal
        } else {
            INCLUDEPATH += /usr/include/gdal
            LIBS += -lgdal
        }
    }
}
# HTTPS for the online mode (OSM map tiles, search, data download): Windows uses WinHTTP (system certificates, no OpenSSL);
# Linux uses the system libcurl (CA bundle and proxy from the system, still no Qt Network and no bundled OpenSSL libraries).
# Link the gnutls build of curl by its exact file name when it exists: the -lcurl symlink can point to a newer curl than the
# glibc floor ships, and the binary then requires symbol versions (CURL_OPENSSL_*) a clean Debian 11 does not have; the bundle
# always carries libcurl-gnutls.so.4 itself, so rpath gives the runtime the very same library.
win32: LIBS += winhttp.lib
unix {
    exists(/usr/lib/x86_64-linux-gnu/libcurl-gnutls.so.4): LIBS += -L/usr/lib/x86_64-linux-gnu -l:libcurl-gnutls.so.4
    else: LIBS += -lcurl
}

QT += core gui widgets concurrent

RC_ICONS = $$PWD/resources/osmc.ico
RC_LANG = 0x0419

# Sources are UTF-8 without BOM: without this flag MSVC reads non-ASCII characters in string literals using the system code page
# (GCC/Clang treat sources as UTF-8 by default, so the flag is Windows-only)
win32: QMAKE_CXXFLAGS += /utf-8
win32: QMAKE_CFLAGS += /utf-8

    VERSION = 1.6.26.11121
# The version string in the code (export info, app/exportmeta.cpp)
DEFINES += TMC_VERSION_STR=\\\"$$VERSION\\\"
win32:{
    QMAKE_TARGET_COMPANY = "Denis Rakhmatullin (mrHalfer)"
    QMAKE_TARGET_PRODUCT = "OSMC"
    QMAKE_TARGET_DESCRIPTION = "Minecraft world exporter"
    QMAKE_TARGET_COPYRIGHT = "Copyright (c) 2026 Denis Rakhmatullin (mrHalfer)"
}

HEADERS += \
    app/mainwindow.h \
    app/exportmeta.h \
    app/exportsettingsdialog.h \
    app/tmcrun.h \
    app/mappreview.h \
    app/theme.h \
    app/httpclient.h \
    app/i18n.h \
    app/osmdownload.h \
    app/osmonlinepanel.h \
    app/osmtilemap.h \
    app/terrainfetch.h \
    app/polygoneditor.h \
    input/tmcproject.h \
    input/t3dsettings.h \
    osm/tmcosmrules.h \
    osm/tmcrel.h \
    osm/tmcenhancer.h \
    osm/tmcaddress.h \
    scene/mcscene.h \
    scene/modelbuilder.h \
    scene/modeldata.h \
    export/climate.h \
    export/anvil.h \
    export/blocks.h \
    export/buildingmerge.h \
    export/colkey.h \
    export/facade.h \
    export/voxelstore.h \
    export/carset.h \
    export/playgroundset.h \
    export/caveset.h \
    export/geom.h \
    export/ground.h \
    export/lairset.h \
    export/paledonor.h \
    export/rails.h \
    export/sites.h \
    export/voxelize.h \
    export/exportrun.h \
    export/mcworldexporter.h \
    export/format/bytes.h \
    export/format/nbtwriter.h \
    export/format/minizip.h \
    core/mapfeature.h \
    core/maplayer.h \
    core/heightraster.h \
    core/stlmodel.h \
    core/dtmgrid.h \
    core/dtmbuild.h \
    core/steplog.h \
    core/geoutil.h

FORMS += \
    app/mainwindow.ui

# Translations: the source strings are English; osmc_ru.ts is the Russian translation, osmc_zh.ts is the Chinese one. lrelease compiles them and embed_translations puts the .qm into the executable (:/i18n/osmc_ru.qm, :/i18n/osmc_zh.qm).
# Update the .ts files with: lupdate osmc.pro
TRANSLATIONS += translations/osmc_ru.ts \
    translations/osmc_zh.ts
CONFIG += lrelease embed_translations
QM_FILES_RESOURCE_PREFIX = /i18n

RESOURCES += \
    resources/osmc.qrc

SOURCES += \
    app/main.cpp \
    app/mainwindow.cpp \
    app/exportmeta.cpp \
    app/exportsettingsdialog.cpp \
    app/tmcrun.cpp \
    app/mappreview.cpp \
    app/theme.cpp \
    app/i18n.cpp \
    app/osmdownload.cpp \
    app/osmonlinepanel.cpp \
    app/osmtilemap.cpp \
    app/terrainfetch.cpp \
    input/tmcproject.cpp \
    input/t3dsettings.cpp \
    osm/tmcosmrules.cpp \
    osm/tmcrel.cpp \
    osm/tmcenhancer.cpp \
    osm/tmcaddress.cpp \
    osm/tmcgreen.cpp \
    scene/mcscene.cpp \
    scene/modelbuilder.cpp \
    export/mcworldexporter.cpp \
    export/anvil.cpp \
    export/blocks.cpp \
    export/buildingmerge.cpp \
    export/carset.cpp \
    export/playgroundset.cpp \
    export/caveset.cpp \
    export/facade.cpp \
    export/geom.cpp \
    export/ground.cpp \
    export/lairset.cpp \
    export/paledonor.cpp \
    export/rails.cpp \
    export/sites.cpp \
    export/voxelize.cpp \
    export/exportrun.cpp \
    export/format/bytes.cpp \
    core/heightraster.cpp \
    core/stlmodel.cpp \
    core/dtmgrid.cpp \
    core/dtmbuild.cpp

# The HTTPS client is platform-specific (common interface in app/httpclient.h): WinHTTP on Windows, system libcurl on Linux.
win32: SOURCES += app/httpclient.cpp
unix:  SOURCES += app/httpclient_unix.cpp

# Copy the resources next to osmc.exe after every build: default settings, OSM rules, height table, enhancer; the
# *.bin structure templates go into the "assets" subfolder (the pile of templates next to the exe looked untidy).
# The program looks for them next to the exe, so nothing has to be copied by hand. Missing files (for example the enhancer) are skipped.
# MSVC puts the executable in a debug/release subfolder, the Unix make — straight into OUT_PWD, so the copy target differs.
CONFIG(debug, debug|release): TMC_OUTDIR = $$OUT_PWD/debug
else: TMC_OUTDIR = $$OUT_PWD/release
unix: TMC_OUTDIR = $$OUT_PWD
TMC_RES = default.t3d osm_rules.json heights.csv tmc_buildings.dat point_models.csv
for(f, TMC_RES) {
    exists($$PWD/resources/$$f) {
        win32: QMAKE_POST_LINK += xcopy /Y /D /Q /I \"$$shell_path($$PWD/resources/$$f)\" \"$$shell_path($$TMC_OUTDIR)\" >nul
        else:  QMAKE_POST_LINK += cp -u $$shell_path($$PWD/resources/$$f) $$shell_path($$TMC_OUTDIR)
        QMAKE_POST_LINK += $$escape_expand(\n\t)
    }
}
# (the glob is left unquoted on purpose: the shell expands it, cp has no wildcard support of its own; mkdir comes first because cp cannot create the target folder)
win32: QMAKE_POST_LINK += xcopy /Y /D /Q /I \"$$shell_path($$PWD/resources/templates/*.bin)\" \"$$shell_path($$TMC_OUTDIR/assets)\" >nul
else:  QMAKE_POST_LINK += mkdir -p $$shell_path($$TMC_OUTDIR/assets) && cp -u $$PWD/resources/templates/*.bin $$shell_path($$TMC_OUTDIR/assets)
QMAKE_POST_LINK += $$escape_expand(\n\t)
# The GDAL runtime (gdal.dll and its dependencies, gdal-data, projlib, gdalplugins) is not in the repository (about 100 MB): if it is put into <GDAL folder>/bin
# (third_party/gdal/bin by default, see third_party/gdal/README.md), it is copied next to the exe too, so the program starts right from Qt Creator.
# Windows only: on Linux the system libgdal already brings its own data files.
win32: exists($$OSMC_GDAL/bin): QMAKE_POST_LINK += xcopy /E /Y /D /Q /I \"$$shell_path($$OSMC_GDAL/bin)\" \"$$shell_path($$TMC_OUTDIR)\" >nul $$escape_expand(\n\t)
