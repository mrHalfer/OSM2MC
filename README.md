# tMC — a Minecraft world exporter

**English** · [Русский](README.ru.md)

tMC turns map data into a Minecraft Java Edition world (a zip save): buildings with floors, windows, interiors and roofs, roads with marking and crosswalks, railways, water, forests, parks, fields, relief, caves, villages, and a few hidden structures (a stronghold, an Ancient City, a Pale Garden …).
It is a standalone Windows program (Qt 5.15.2 / MSVC, qmake, GDAL).

The data source is one of:

- an **OpenStreetMap** extract (`.osm`, `.pbf`, `.osm.pbf`) — or the area you draw on the built-in online map, which tMC downloads for you;
- a **`.tqt` project** (layers `Shape=`, `Rastr=`, `Wkt`, `SpatialFilter`, 3D settings `Wdf3D=`);
- a **folder with shapefiles** (all `*.shp` and height matrices `*.tif/*.tiff` in the folder; every shapefile needs a `.prj`).

## Quick start (window)

1. Run `tMC.exe`. The right half shows the online OpenStreetMap map. Find a place with the search field, press **Boundary** and click out the polygon of the area to export (up to **25 km²**; the area is shown on the map). Optionally mark the spawn point and the church entrances of the stronghold / the Ancient City / the Pale Garden.
2. Press **Download the area** — the data is fetched from the OpenStreetMap servers (Overpass API) and opened as a local file. (Instead, you may type or browse for an OSM file in the *OSM file* field on the left; clearing the field brings the online map back.)
3. Choose the world options on the left (version, what is outside the area, seed, caves, climate) and press **Export**. The result is `<file name>.zip` next to the OSM file; unpack it into `.minecraft/saves` (or put it there as is).

**Project files.** The *Save project…* and *Open project…* buttons in the top-left corner save and restore a piece of work in a `.tmc` file (JSON): the map view, the boundary, the spawn and entrance marks (in longitude/latitude, independent of the data projection), the OSM file if the data was loaded from one, and the world options. `tMC project.tmc` opens it right at the start.

When an export is finished, the result window has the checkbox *Show the file in the folder* (the Explorer opens with the archive selected).

Network use follows the OpenStreetMap policies: map tiles are cached on disk, searches go to Nominatim at most once per second, the program identifies itself with a `User-Agent`, and the map carries the "© OpenStreetMap contributors" attribution. Please do not abuse the public servers.

### Interface language

The interface is available in **English** and **Russian**. By default it follows the system language (anything other than Russian shows English). The **Language** selector in the top-right corner of the window switches it at once, without a restart; `--lang auto|en|ru` overrides it for one run without saving. Exported worlds do not depend on the interface language.

## Command line

```
tMC [source] [-o|--out world.zip] [--format 26|1.21] [--flat] [--seed N] [--caves 0|1]
    [--climate auto|temperate|boreal|tundra|steppe|mediterranean|desert|savanna|tropical]
    [--clip "lon lat; lon lat; ..."] [--spawn-lonlat "lon lat"]
    [--stronghold "x z"] [--city "x z"] [--pale "x z"] [--no-locate-datapack]
    [--osm-rules rules.json] [--osm-ini osmconf.ini] [--no-greenery] [--enhancer file.dat | --no-enhancer]
    [--t3d file.t3d] [--real-meters 0|1] [--dry-run] [--download-osm "lon lat; ..."]
    [--lang auto|en|ru] [--theme auto|light|dark] [--debug] [--log log.txt] [--nogui]
```

`tMC --help` lists everything. Notes:

- `source` is a `.tqt` file, an OSM extract or a folder (default — the current folder). Without arguments the window opens.
- `--nogui` runs the export without a window; the exit code is 0 on success. The program is a GUI application, so redirected console output may be empty — use `--log`.
- `--clip` limits the export to a polygon (WGS84): only what is inside is exported and chunks outside are not written.
- `--download-osm "lon lat; …" -o area.osm` downloads an area (up to 25 km²) and exits.
- `--dry-run` only loads and classifies the data (a summary "code × count" goes to the log) — handy for tuning the OSM rules.
- 3D settings are chosen in this order: `--t3d`, any `.t3d` in the shapefile folder, `Wdf3D=` of the `.tqt` project, `default.t3d` next to the exe.
- The **datapack for finding structures** (on by default; `--no-locate-datapack` turns it off) adds structure sets so that `/locate` and the Eye of Ender find the stronghold, the Ancient City, trial chambers and ruined portals. The structures themselves are written into the chunks and do not depend on it. The price: the game warns about experimental features when opening such a world.
- Example: `tMC city.osm.pbf --osm-rules osm_rules.json --nogui --log run.log`.

## OpenStreetMap rules

Objects are read by the GDAL OSM driver; then the rules of **`osm_rules.json`** sort them into virtual "tqt layers" (`osm.bldpol`, `osm.vegpol`, `osm.rdslin` …) and set the fields `CODE` (the object type), `REL` (height), `MAT` (material) and so on. From there everything works as for shapefiles with the 3D settings (`default.t3d`). The format of the rules is described in the `_help` section of the file itself; the file is edited by hand, without rebuilding.

- The rules file is looked for in this order: `--osm-rules`, next to the OSM file, next to the exe, the current folder.
- Conditions: a value, `"*"` (the tag exists), `"-"` (no tag), `"!v"` (the tag exists and differs), lists, regular expressions `"~re"`, numbers `">=4"`; the service tags `poi:<tag>` (points inside a building outline), `area:<tag>` (enclosing plots), `_area`, `_perim`. Templates: `{tag}`, `{tag1,tag2}`, `{tag|num}`, `{tag|num*3}`, alternatives through `||`.
- **Two passes.** Rules with `"pass": 2` only see buildings the first pass did not recognize and use the tags about the nearest recognized buildings (`nb:code`, `nb:share`, `nb:n`, `nb:rel`, `nb:dist`).
- **Height** (`"REL": "auto"`): the `height` tag → `building:levels` by the floor formulas → the typical height by code from `heights.csv` → 6 m. **Material** (`MAT`) is taken from `building:material` / `building:facade:material` (brick, stone, wood, glass, metal, concrete, panels …).
- **Addresses** (`osm/tmcaddress.cpp`): the street type, name and house number for the signs on houses and the signposts at intersections are parsed from `addr:*` and `name` automatically (Russian street types are recognized; for other languages the whole name is used as is).
- **Greenery**: yards between houses, taiga in the private sector, forests up to a share of the area, parks, "wild" forests with bandit camps on big empty plots; tuned by the `greenery` block of the rules, `--no-greenery` turns it off.
- **Climate**: the biomes, the surface and the tree species of the export area depend on the climate zone — detected from the coordinates or set manually (`--climate`, the *Climate* list).
- **The enhancer** (`tmc_buildings.dat`) is a file of building points (type, height, floors) used only when OSM does not give them; it has no priority over OSM. The heights and floor counts in it are deliberately fuzzed, so they are rough values, not exact data. Without the file everything works (`--no-enhancer`).
- Debugging: `TMC_OSM_DEBUG=1` writes a line per building (which tags the rules saw and the resulting layer/code); `TMC_OSM_UNMATCHED=file.tsv` with `--dry-run` writes the counts of the objects no rule matched.
- Not supported: point objects (signs, stops, traffic lights) and relations other than multipolygons.

## What is in the world

Besides the exported objects, the world contains: caves under the ground (from templates of a real game world), a Pale Garden with a mansion, bandit outposts and camps, shipwrecks in big water bodies, ruined portals in forests, floating islands above parks, villages from clusters of private houses, church dungeons with secret passages to a stronghold and an Ancient City, science-building laboratories with redstone mechanisms, furniture, villagers, mannequins and chests with vanilla loot. The structure templates are `resources/templates/*.bin`; without a template the corresponding feature is skipped or simplified (the reason is written in the final log line).

## Building

You need Qt 5.15.2 (MSVC) and Visual Studio with the MSVC x64 toolset. The GDAL headers and the import library `gdal_i.lib` (GDAL 3.4) are in `third_party/gdal`, so nothing else is needed to build. To RUN the program you need the GDAL 3.4 runtime (`gdal304.dll`, its dependencies and the folders `gdal-data`, `projlib`, `gdalplugins`, about 83 MB): download `gdal-runtime-3.4.zip` from the releases and unpack it into `third_party/gdal/bin` (see `third_party/gdal/README.md`). After every build it is copied next to `tMC.exe` automatically, so the program starts right from Qt Creator.

**A new machine, in short:** install Qt 5.15.2 (the MSVC 2019 64-bit kit), Visual Studio or Build Tools (the "Desktop development with C++" workload) and, for the tests, Python 3 → clone the repository → run `setup.bat` (it checks the tools and downloads the GDAL runtime into `third_party/gdal/bin`) → open `tMC.pro` in Qt Creator and press Run (or run `build.bat`). `build.bat` finds Visual Studio itself (vswhere) and Qt (`C:\Qt\5.15.2\msvc2019_64` or the `qmake` from `PATH`).
`build.bat` makes a shadow build (`qmake` + `nmake release`) in `%TMC_BUILD%` (by default `Builds\tMC` next to the `Projects` folder); the result is `%TMC_BUILD%\release\tMC.exe`, the log is `build_nmake.log`.
The paths are set with the environment variables `VCVARS` (vcvarsall.bat), `QTDIR` (default `C:\Qt\5.15.2\msvc2019_64`), `GDAL_DIR` (optional: another GDAL; by default the headers and `gdal_i.lib` from `third_party/gdal` are used), `TMC_BUILD`.
After linking, `tMC.pro` copies everything from `resources/` next to the exe. If the exe is running, linking fails (LNK1104) — close the program. Add new files to `tMC.pro`.

The Russian translation of the interface is `translations/tmc_ru.ts` (embedded into the exe by `lrelease`); to refresh it after changing strings run `lupdate tMC.pro -ts translations/tmc_ru.ts`. Source strings in the code are English.

## Making a release

`make_release.bat` makes a ready-to-run package in one step: it builds the program, runs `windeployqt` (the Qt DLLs and plugins), adds the Visual C++ runtime (app-local), the GDAL runtime from `third_party/gdal/bin`, the structure templates `*.bin`, `default.t3d`, `osm_rules.json`, `heights.csv`, `tmc_buildings.dat`, the README files and `THIRD_PARTY_NOTICES.md` (and `LICENSE` when it exists),
then **starts the result with a clean `PATH`** (no Qt, no GDAL) to be sure it works by itself, and packs it into `Builds/tMC/package/tMC-<version>-win64.zip` (the version is the `VERSION` line of `tMC.pro`). The user only unpacks the zip and runs `tMC.exe`; nothing has to be installed.
`third_party/gdal/make_runtime_zip.bat` makes `gdal-runtime-3.4.zip` (the contents of `third_party/gdal/bin`) for the releases page: it is what `setup.bat` downloads (set its address in `setup.bat` or in the variable `GDAL_RUNTIME_URL`).
Not part of a release: `tests/`, `docs/`, the sources.

## Tests

The tests, the C++ exporter harness, the Minecraft servers (26.3 and 1.21.11) and the test-world generation live in `tests/` — see `tests/README.md`.
A quick check (the OSM classification and the folder mode): `py -3 tests\run_tests.py`. A full run with the harness and the servers: `py -3 tests\run_tests.py all`.

## Source layout

| Folder | Purpose |
|---|---|
| `app/` | `main.cpp` (the command line), `mainwindow.*` and `mainwindow.ui` (the window; edit the layout in Qt Designer), `mappreview.*` (the preview of loaded data), `osmtilemap.*` and `osmonlinepanel.*` (the online map, search, boundary, download), `httpclient.*` (WinHTTP client), `osmdownload.*` (Overpass download), `i18n.*` (interface language), `theme.*` + `resources/tmc.qss` (light/dark theme), `tmcrun.*` (the whole export cycle shared by the window and the command line) |
| `input/` | `tmcproject.*` — loading a `.tqt`, a shapefile folder or an OSM extract through OGR, reprojection; `t3dsettings.*` — parsing `.t3d` |
| `osm/` | `tmcosmrules.*` — the "OSM tags → layer/code" rules; `tmcenhancer.*` — the building enhancer; `tmcrel.*` — building heights; `tmcaddress.*` — addresses; `tmcgreen.cpp` — vegetation top-up |
| `scene/` | `mcscene.*` — collecting the export objects by layers; `modelbuilder.*` — the mesh builder of objects |
| `export/` | the Minecraft world exporter (voxels, chunks, NBT, zip), the climate zones (`climate.h`) |
| `core/` | base classes: `MapFeature`, `MapLayer`, `HeightRaster`, geodesy helpers |
| `resources/` | the icon, `default.t3d`, `osm_rules.json`, `heights.csv`, `tmc_buildings.dat`, the templates `templates/*.bin` |
| `translations/` | the Qt translation files |
| `tests/`, `docs/` | developer scripts, tests, working notes |

The project CRS is the `Wkt` line of the `.tqt`, otherwise the `.prj` of the first layer; for WGS84 data — EPSG:3395 (or `DefaultCrs=` from `tMC.ini`/`TQT.ini` next to the exe).

## Notes

Minecraft is a trademark of Mojang AB; tMC is not affiliated with Mojang or Microsoft. tMC is under the MIT license (`LICENSE`); the third-party components are listed in `THIRD_PARTY_NOTICES.md`. Map data © OpenStreetMap contributors (ODbL). The Minecraft server files used by the tests are not included (the Mojang EULA).
