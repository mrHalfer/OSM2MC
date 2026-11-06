# tMC tests

**English** · [Русский](README.ru.md)

Everything needed to check the exporter lives here, inside the tMC project.

```
tests\
  run_tests.py        a single entry point: py -3 run_tests.py [osm shp harness servers | all] [-v]
  tools\              checks and helpers (Python)
  harness\            the C++ exporter harness: mc_export_test.cpp, mc_scene_run.cpp, build_harness.bat
  data\               test data: gen_osm_test.py -> osm_buildings.osm + osm_buildings.expected.json
  servers\mc26\       the Minecraft 26.3 server (server.jar; working files are created on start)
  servers\mc121\      the Minecraft 1.21.11 server
  worlds\             generated worlds (zip) and their logs; unpacked copies are not kept
  build\              harness artifacts (exe, obj)
  local_config.json   optional, not in the repository: machine paths (QT_BIN, GDAL_BIN, OSMC_EXE, JAVA, TMC_TEST_SHP)
```

The tests run `osmc.exe` with `--lang en`, so the log messages they match are the English ones.

## Stages of `run_tests.py`

| stage | what it does | time |
|---|---|---|
| `osm` | a synthetic OSM (`data/gen_osm_test.py`: 28 buildings with expected codes, roads, rails, parking lots, sites, a square) is built through `osmc.exe` by the working rules `osm_rules.json`; it checks the code of every building (pass 1: tags, POI, plots; pass 2: neighbors and area), the object count and codes in the other layers, no extra "buildings", the fallback Pale Garden; then the same data in 1.21.x and in a superflat world | ~15 s |
| `shp` | the "folder with shapefiles" mode (the directory is set by `TMC_TEST_SHP`), `default.t3d`, the result by categories. No data — SKIP | ~10 s |
| `harness` | building the harness from `mcworldexporter.cpp` of the tMC root, 6 worlds `mc_test*.zip`, the full `verify_mc_export.py` (we expect `RESULT PASS`) | ~10-20 min |
| `servers` | real servers 26.3 and 1.21.11 open the tMC worlds (`server_check.py`) and the harness noise worlds (`smoke_mc.py`) | ~10 min |

A quick cycle while editing the OSM rules: `py -3 run_tests.py osm`. Before handing in exporter changes — `py -3 run_tests.py all`.

## Requirements

- A built `osmc.exe` (`build.bat` in the root); the path is the `OSMC_EXE` variable, a `local_config.json` key, or `osmc.exe` from `PATH`.
- Qt and GDAL directories with DLLs: `QT_BIN`, `GDAL_BIN` (environment variables or `local_config.json`); they are added to the `PATH` of the programs that are started.
- Java for the servers: `JAVA` (a variable or `local_config.json`), otherwise `java` from `PATH`.
- The Minecraft servers (`server.jar`) are not in the repository: put them into `servers\mc26` and `servers\mc121` (the Mojang EULA).
- The servers 26 and 121 share port 25565 — `run_tests.py` starts them strictly one after another; do not run them in parallel.

## Useful separately

- `tools\test_osm.py`, `tools\test_shp.py` — separate stages (exit code 0 — PASS, 77 — SKIP).
- `tools\server_check.py 26|121 <zip> [--area x0 z0 x1 z1]` — open any world with a server, forceload a box.
- `tools\smoke_mc.py 26|121 <zip>` — a server smoke test of the harness noise worlds (mannequins, spawner, the bottom).
- `tools\verify_mc_export.py [zip ...]`, `tools\lightcheck.py`, `tools\probe_blocks.py` and the others - checks of ready worlds; the worlds lie in `tests\worlds`.
- `tools\verify_subway.py <zip>` - the underground metro: rails under a solid ceiling (section 7+2 wide, air at +-3 and lining at +-4 on straight runs), lamps, floor, no holes above; no stray solid columns inside the corridor; a powered rail must never carry a curved shape (ascending is fine; sideways rails of a parallel line are not a turn); electrified surface rails (trams, `electrified` railways) must stand on a stone/brick bed (exit code 77 - no metro in the world). Known limitation: `--flat` worlds have no underground everywhere, the checker reports plugs where a chain passes chunks without terrain - this is an exporter quirk of flat mode, not of the station code.
- `tools\verify_fauna.py <zip> [zone]` - the aquatic fauna as entities: PersistenceRequired and the species fields, the fish species per climate zone.
- `tools\verify_biomes.py <zip | unpacked save> [biome]` - the biome palette histogram and the chunks of the wanted biome (with a spawn hint).
- `tools\scan_points.py <zip> [marker ...]`, `tools\scan_zone.py <zip> [log ...]`, `tools\count_blocks.py <zip ...> [name part ...]`, `tools\chunk_range.py <zip>` - world scans: point-structure markers, zone trees, block counts, the real world size.
- `tools\steps_report.py <mcx log ...>` - the step table over `MCX_LOG_STEPS` logs (the last one is compared with the first - speed regressions).
- `tools\bench_scenes.py <dir> [scene ...]` - the bench/probe .osm scenes (s1..s6, bench, p1..p5; see the speed check in DEVELOPING/AGENTS); `tools\check_zones.py` - the climate zone of the world at real city coordinates.
- With `TMC_OSM_DEBUG=1` the tMC log has a line per building: the tags the rules saw and the resulting layer/code.

## What was not carried over

The template extraction scripts (`extract_*.py`, `gen_*.py`) are not part of the project: they are not tests but generators of `resources/templates/*.bin` from a donor world.
