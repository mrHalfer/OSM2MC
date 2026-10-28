# tMC — a standalone Minecraft exporter: reads a tqt project (.tqt), shapefiles from a folder or an OpenStreetMap extract, builds a model from the .t3d settings and writes a Minecraft world (zip).
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
TARGET = tMC

DEPENDPATH += .
INCLUDEPATH += .

# GDAL: the headers and the import library gdal_i.lib are in third_party/gdal (the project builds out of the box); another GDAL can be used
# through the GDAL_DIR environment variable (a directory with include and lib subfolders). The DLLs are needed only to run (see README).
TMC_GDAL = $$(GDAL_DIR)
isEmpty(TMC_GDAL): TMC_GDAL = $$PWD/third_party/gdal
INCLUDEPATH += $$TMC_GDAL/include
LIBS += $$TMC_GDAL/lib/gdal_i.lib
# WinHTTP: online mode (OSM map tiles, search, data download) — HTTPS through the system certificates, no OpenSSL
LIBS += winhttp.lib

QT += core gui widgets concurrent

RC_ICONS = $$PWD/resources/tMC.ico
RC_LANG = 0x0419

# Sources are UTF-8 without BOM: without this flag MSVC reads non-ASCII characters in string literals using the system code page
QMAKE_CXXFLAGS += /utf-8
QMAKE_CFLAGS += /utf-8

win32:{
    VERSION = 1.0.26.1028
    QMAKE_TARGET_COMPANY = "Denis Rakhmatullin (mrHalfer)"
    QMAKE_TARGET_PRODUCT = "tMC"
    QMAKE_TARGET_DESCRIPTION = "Minecraft world exporter"
    QMAKE_TARGET_COPYRIGHT = "Copyright (c) 2026 Denis Rakhmatullin (mrHalfer)"
}

HEADERS += \
    app/mainwindow.h \
    app/tmcrun.h \
    app/mappreview.h \
    app/theme.h \
    app/httpclient.h \
    app/i18n.h \
    app/osmdownload.h \
    app/osmonlinepanel.h \
    app/osmtilemap.h \
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
    export/mcworldexporter.h \
    export/format/bytes.h \
    export/format/nbtwriter.h \
    export/format/minizip.h \
    core/mapfeature.h \
    core/maplayer.h \
    core/heightraster.h \
    core/steplog.h \
    core/geoutil.h

FORMS += \
    app/mainwindow.ui

# Translations: the source strings are English; tmc_ru.ts is the Russian translation. lrelease compiles it and embed_translations puts the .qm into the executable (:/i18n/tmc_ru.qm).
# Update the .ts files with: lupdate tMC.pro
TRANSLATIONS += translations/tmc_ru.ts
CONFIG += lrelease embed_translations
QM_FILES_RESOURCE_PREFIX = /i18n

RESOURCES += \
    resources/tmc.qrc

SOURCES += \
    app/main.cpp \
    app/mainwindow.cpp \
    app/tmcrun.cpp \
    app/mappreview.cpp \
    app/theme.cpp \
    app/httpclient.cpp \
    app/i18n.cpp \
    app/osmdownload.cpp \
    app/osmonlinepanel.cpp \
    app/osmtilemap.cpp \
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
    export/format/bytes.cpp \
    core/heightraster.cpp

# Copy the resources next to tMC.exe after every build: default settings, OSM rules, height table, enhancer, *.bin templates.
# The program looks for them next to the exe, so nothing has to be copied by hand. Missing files (for example the enhancer) are skipped.
CONFIG(debug, debug|release): TMC_OUTDIR = $$OUT_PWD/debug
else: TMC_OUTDIR = $$OUT_PWD/release
TMC_RES = default.t3d osm_rules.json heights.csv tmc_buildings.dat
for(f, TMC_RES): exists($$PWD/resources/$$f): QMAKE_POST_LINK += xcopy /Y /D /Q /I \"$$shell_path($$PWD/resources/$$f)\" \"$$shell_path($$TMC_OUTDIR)\" >nul $$escape_expand(\n\t)
QMAKE_POST_LINK += xcopy /Y /D /Q /I \"$$shell_path($$PWD/resources/templates/*.bin)\" \"$$shell_path($$TMC_OUTDIR)\" >nul $$escape_expand(\n\t)
# The GDAL runtime (gdal.dll and its dependencies, gdal-data, projlib, gdalplugins) is not in the repository (about 100 MB): if it is put into <GDAL folder>/bin
# (third_party/gdal/bin by default, see third_party/gdal/README.md), it is copied next to the exe too, so the program starts right from Qt Creator.
exists($$TMC_GDAL/bin): QMAKE_POST_LINK += xcopy /E /Y /D /Q /I \"$$shell_path($$TMC_GDAL/bin)\" \"$$shell_path($$TMC_OUTDIR)\" >nul $$escape_expand(\n\t)
