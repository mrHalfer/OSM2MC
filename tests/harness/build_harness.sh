#!/usr/bin/env bash
# Builds the C++ test harness (Linux): mc_export_test and mc_scene_run from export\mcworldexporter.cpp into tests/build. Success = ALL-OK.
# Mirror of build_harness.bat. Qt5/GDAL includes come from pkg-config (apt Qt5); variable ROOT may be set from outside.
set -u
SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SELF/../.." && pwd)}"
OUT="$SELF/../build"
mkdir -p "$OUT"
cd "$OUT" || exit 1

CXXFLAGS=(-O2 -std=c++17 -fPIC -DNDEBUG
  -Wno-stringop-overflow   # known GCC>=11 false positive on Qt5 QVector/QArrayData patterns
  $(pkg-config --cflags Qt5Gui Qt5Concurrent)
  -I/usr/include/gdal -I"$ROOT" -I"$ROOT/export")
LIBS=($(pkg-config --libs Qt5Gui Qt5Concurrent))
LIBS=("-Wl,--no-as-needed" "${LIBS[@]}")

UNITS=(format/bytes blocks anvil buildingmerge carset ground caveset facade geom sites lairset paledonor rails mcworldexporter voxelize exportrun)
OBJS=()
for u in "${UNITS[@]}"; do
  o="${u##*/}.o"
  OBJS+=("$o")
  g++ -c "${CXXFLAGS[@]}" -o "$o" "$ROOT/export/$u.cpp" || { echo BUILD-FAILED; exit 1; }
done
g++ -c "${CXXFLAGS[@]}" -o dtmgrid.o "$ROOT/core/dtmgrid.cpp" || { echo BUILD-FAILED; exit 1; }
OBJS+=("dtmgrid.o")

g++ "${CXXFLAGS[@]}" -o mc_export_test "$SELF/mc_export_test.cpp" "${OBJS[@]}" "${LIBS[@]}" || { echo BUILD-FAILED; exit 1; }
g++ "${CXXFLAGS[@]}" -o mc_scene_run "$SELF/mc_scene_run.cpp" "${OBJS[@]}" "${LIBS[@]}" || { echo BUILD-FAILED; exit 1; }

# Export templates (caves, lairs, pale forest, wrecks ...) lie in the "assets" folder next to the exe (the old flat copy is still searched)
mkdir -p "$OUT/assets"
cp -f "$ROOT"/resources/templates/*.bin "$OUT/assets/" 2>/dev/null || true
echo ALL-OK
