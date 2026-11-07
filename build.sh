#!/usr/bin/env bash
# Linux development build of OSMC (the mirror of build.bat): qmake + make into $OSMC_BUILD (default ~/builds/tmc), log next to the result.
# Qt5 and the system dev packages are expected (see docs/porting-linux.md); OSMC_GDAL=<prefix> points at a custom GDAL build (its gdal.pc wins over the system one).
# Result: $OSMC_BUILD/tmc/osmc with the resources next to it.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${OSMC_BUILD:-$HOME/builds/tmc}"
if [ -n "${OSMC_GDAL:-}" ]; then
  export PKG_CONFIG_PATH="$OSMC_GDAL/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi
mkdir -p "$BUILD" || exit 1
cd "$BUILD" || exit 1
LOG="$BUILD/build_make.log"
{
  echo "== qmake $(date -Is)"
  qmake "$ROOT/osmc.pro" -spec linux-g++ CONFIG+=release &&
  echo "== make $(date -Is)" &&
  make -k -j"$(nproc)"
} > "$LOG" 2>&1
rc=$?
tail -n 15 "$LOG"
if [ -x "$BUILD/osmc" ]; then
  echo "build ok: $BUILD/osmc"
else
  echo "build FAILED, see $LOG"
  exit "${rc:-1}"
fi
