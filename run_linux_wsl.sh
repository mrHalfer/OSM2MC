#!/usr/bin/env bash
# Helper of run_linux.bat: starts a Linux OSMC build (AppImage, or an unpacked tar.gz) from WSL.
# $1 = the package folder, $2 = the caller's current folder (both already WSL paths), the rest = arguments for osmc.
# Relative file arguments then work as typed, "E:\..." paths are converted by wslpath.
# Variables: OSMC_APPIMAGE (an explicit AppImage file), OSMC_DIR (the folder extracted from the release tar.gz
# instead of the AppImage — the OSMC/ one with osmc and lib/, or its parent — like the Windows zip's folder).
set -eu
PKG="$1"; CWD="$2"; shift 2
cd "$CWD" || exit 1

# OSMC_DIR/OSMC_APPIMAGE may arrive as Windows paths (run_linux.bat forwards them via WSLENV without converting)
fixwin() { local v="$1"; case "$v" in [A-Za-z]:*) v="${v//\\//}"; v="$(wslpath -u "$v")";; esac; printf '%s' "$v"; }
OSMC_DIR="${OSMC_DIR:-}"; OSMC_DIR="$(fixwin "$OSMC_DIR")"
OSMC_APPIMAGE="${OSMC_APPIMAGE:-}"; OSMC_APPIMAGE="$(fixwin "$OSMC_APPIMAGE")"

BIN=""
if [ -n "${OSMC_DIR:-}" ]; then
  if   [ -x "$OSMC_DIR/OSMC/osmc" ]; then BIN="$OSMC_DIR/OSMC/osmc"
  elif [ -x "$OSMC_DIR/osmc" ];      then BIN="$OSMC_DIR/osmc"
  else echo "OSMC_DIR has no OSMC/osmc: $OSMC_DIR"; exit 1; fi
else
  AI="${OSMC_APPIMAGE:-$(ls -t "$PKG"/OSMC-*-x86_64.AppImage 2>/dev/null | head -1 || true)}"
  [ -n "$AI" ] && [ -f "$AI" ] || { echo "no OSMC-*-x86_64.AppImage in $PKG - run make_release_linux.bat first"; exit 1; }
  # /tmp of the distro: the AppImage unpacks faster from ext4 than from a 9p mount, and always the fresh copy
  BIN=/tmp/osmc-linux.AppImage
  cp -f "$AI" "$BIN"
  chmod +x "$BIN"
fi

# the caller is Windows: backslashes everywhere become slashes, "E:\x"-style paths then become WSL paths;
# the GUI is automatic: WSLg gives DISPLAY/WAYLAND_DISPLAY itself
args=()
for a in "$@"; do
  a="${a//\\//}"
  if [[ "$a" =~ ^[A-Za-z]:[\\/] ]]; then a=$(wslpath -u "$a"); fi
  args+=("$a")
done

if [ "${BIN##*.}" = "AppImage" ]; then
  exec env APPIMAGE_EXTRACT_AND_RUN=1 "$BIN" "${args[@]}"
fi
exec "$BIN" "${args[@]}"
