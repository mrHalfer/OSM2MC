#!/usr/bin/env bash
# Makes a ready-to-run Linux release of OSMC: an AppImage with everything inside — Qt5 + plugins, the slim GDAL/PROJ and their data,
# libcurl + OpenSSL + a CA bundle, fonts, the resources. The target machine needs only glibc >= 2.31 (Debian 11 era) and the video drivers.
# MUST run on the floor system (Debian 11 / docker image debian:11): the glibc of the build machine is the compatibility floor of the result.
# Same variables as build.bat: OSMC_BUILD (work dir, default ~/builds/tmc), OSMC_OUT (where the artifacts are copied, optional), OSMC_GDAL_URL/SHA.
# Artifacts: $OSMC_OUT|[$OSMC_BUILD/]package/OSMC-<version>-x86_64.AppImage and OSMC-<version>-linux64.tar.gz (the same bundle unpacked, for servers).
# Steps mirror make_release.bat: build -> collect -> bundle -> a check without the system Qt/GDAL -> pack.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${OSMC_BUILD:-$HOME/builds/tmc}"
PKG="$BUILD/package"
APP="$BUILD/appdir"
GDAL_PREFIX="$BUILD/gdal-prefix"
GDAL_VER=3.4.3                                    # the same version as third_party/gdal on Windows
GDAL_URL="${OSMC_GDAL_URL:-https://github.com/OSGeo/gdal/releases/download/v$GDAL_VER/gdal-$GDAL_VER.tar.gz}"
GDAL_SHA="${OSMC_GDAL_SHA:-7244962628c82921b4a4903dbb721c7222b7d82ed5928fef55a52f87f68ad1fe}"
AT_URL="${OSMC_AT_URL:-https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage}"
QTP=/usr/lib/x86_64-linux-gnu/qt5/plugins
LIBDIR=/usr/lib/x86_64-linux-gnu

# the synced file can keep CRLF (it comes from the Windows working copy), so strip every whitespace incl. \r
VER="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]*=[[:space:]]*//p' "$ROOT/osmc.pro" | head -1 | tr -d '[:space:]')"
[ -n "$VER" ] || { echo "cannot read VERSION from osmc.pro"; exit 1; }
echo "=== OSMC $VER, $(uname -a | cut -c1-60) ==="
if [ "${OSMC_ALLOW_ANY_BASE:-0}" != "1" ] && ! grep -q '^VERSION_ID="11"' /etc/os-release; then
  echo "WARNING: the build host is not Debian 11 — the AppImage floor will be whatever this glibc is. OSMC_ALLOW_ANY_BASE=1 to override."
fi

echo "=== 1/5 GDAL $GDAL_VER (slim, into $GDAL_PREFIX) ==="
mkdir -p "$BUILD/dl"
if [ ! -x "$GDAL_PREFIX/bin/gdal-config" ]; then
  SRC="$BUILD/dl/gdal-$GDAL_VER.tar.gz"
  [ -f "$SRC" ] || wget -q -O "$SRC" "$GDAL_URL"
  echo "$GDAL_SHA  $SRC" | sha256sum -c -
  WORK="$BUILD/gdal-build"
  rm -rf "$WORK"; mkdir -p "$WORK"
  tar -C "$WORK" -xf "$SRC"
  cd "$WORK/gdal-$GDAL_VER"
  # only what osmc reads: OSM XML/PBF, Shapefile, GPKG, the MEM raster (green mapper) — nothing else, all codecs minimal.
  # GEOS stays IN: the Windows GDAL has it (geos_c.dll) and the OSM multipolygon path calls it, without it every release log fills with "GEOS support not enabled".
  ./configure --prefix="$GDAL_PREFIX" --with-proj=yes --with-sqlite3=yes --with-geos=yes \
    --with-rename-internal-shapelib-symbols --with-hide-internal-symbols --with-libtiff=yes \
    --without-curl --without-hdf4 --without-hdf5 --without-netcdf \
    --without-jasper --without-openjpeg --without-webp --without-xerces --without-xml2 --without-gnm \
    --without-mongocxx --without-pcre2 --without-python \
    > "$BUILD/gdal_configure.log" 2>&1
  make -j"$(nproc)" > "$BUILD/gdal_make.log" 2>&1
  make install > "$BUILD/gdal_install.log" 2>&1
  strip "$GDAL_PREFIX"/lib/libgdal.so.*.*.*
fi
"$GDAL_PREFIX/bin/gdal-config" --version

echo "=== 2/5 building osmc ==="
# always relink: make does not consider the binary stale when qmake only changed the LIBS line (an osmc.pro
# edit like another curl would otherwise not reach the bundle)
rm -f "$BUILD/osmc"
OSMC_BUILD="$BUILD" OSMC_GDAL="$GDAL_PREFIX" bash "$ROOT/build.sh"
OSMC_BIN="$BUILD/osmc"
[ -x "$OSMC_BIN" ] || { echo "build failed, see $BUILD/build_make.log"; exit 1; }

echo "=== 3/5 collecting the bundle ==="
rm -rf "$APP"
mkdir -p "$APP/usr/lib" "$APP/usr/plugins" "$APP/usr/fonts" "$APP/usr/gdal-data" "$APP/usr/projlib"
# the executable and the user-visible files (the same list as make_release.bat; README images are the Windows zip's habit, the AppImage ships none)
cp -u "$OSMC_BIN" "$APP/usr/osmc"
for f in default.t3d osm_rules.json heights.csv point_models.csv tmc_buildings.dat; do
  [ -f "$ROOT/resources/$f" ] && cp -u "$ROOT/resources/$f" "$APP/usr/"
done
cp -u "$ROOT"/resources/templates/*.bin "$APP/usr/"
for f in README.md README.ru.md CHANGELOG.md LICENSE THIRD_PARTY_NOTICES.md; do
  [ -f "$ROOT/$f" ] && cp -u "$ROOT/$f" "$APP/usr/"
done
cp -u "$GDAL_PREFIX"/share/gdal/* "$APP/usr/gdal-data/"
cp -u /usr/share/proj/proj.db /usr/share/proj/*.csv /usr/share/proj/*.xml /usr/share/proj/epsg /usr/share/proj/esri_* "$APP/usr/projlib/" 2>/dev/null || true
cp -u /etc/ssl/certs/ca-certificates.crt "$APP/usr/ca-certificates.crt"
[ -f "$ROOT/resources/osmc.png" ] && cp "$ROOT/resources/osmc.png" "$APP/osmc.png"

cat > "$APP/usr/qt.conf" <<'EOF'
[Paths]
Plugins = plugins
EOF

cat > "$APP/osmc.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=OSMC
GenericName=OpenStreetMap to Minecraft
Comment=Convert OpenStreetMap data into a Minecraft world
Exec=osmc
Icon=osmc
Terminal=false
Categories=Graphics;Education;
StartupWMClass=osmc
EOF

cat > "$APP/AppRun" <<'EOF'
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/osmc" "$@"
EOF
chmod +x "$APP/AppRun"

# the fonts shipped with the bundle (a server or a minimal Fedora may have none); the system fonts stay where they exist
mkdir -p "$APP/usr/fonts"
if [ -d /usr/share/fonts/truetype/dejavu ]; then
  cp -u /usr/share/fonts/truetype/dejavu/*.ttf "$APP/usr/fonts/" 2>/dev/null || true
fi
cat > "$APP/usr/fonts/fonts.conf" <<'EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir prefix="relative">.</dir>
  <include ignore_missing="yes">/etc/fonts/fonts.conf</include>
</fontconfig>
EOF

# the self-built GDAL: ldd cannot see it in the system, so it is placed into the bundle explicitly (everything else is collected below)
cp -L "$GDAL_PREFIX/lib/libgdal.so.30" "$APP/usr/lib/libgdal.so.30"
# Qt plugins: the platforms (xcb, offscreen for --nogui, wayland when available), imageformats + svg (the GUI draws SVG check marks), iconengines, the compose input context, the GLX integration;
# the wayland client also needs its shell/graphics integration folders, without them the wayland platform plugin loads but finds "no shell integration"
for d in platforms imageformats iconengines xcbglintegrations wayland-shell-integration wayland-graphics-integration-client wayland-decoration-client; do
  [ -d "$QTP/$d" ] || continue
  mkdir -p "$APP/usr/plugins/$d"
  for p in "$QTP/$d"/*.so; do
    [ -e "$p" ] || continue
    case "$d/$p" in
      */platforms/libqe*|*/platforms/libqlinuxfb*|*/platforms/libqvnc*|*/platforms/libqminimal*) continue ;;
    esac
    cp -L "$p" "$APP/usr/plugins/$d/"
  done
done
[ -f "$QTP/platforminputcontexts/libcomposeplatforminputcontextplugin.so" ] && {
  mkdir -p "$APP/usr/plugins/platforminputcontexts"
  cp -L "$QTP/platforminputcontexts/libcomposeplatforminputcontextplugin.so" "$APP/usr/plugins/platforminputcontexts/"; }

# every dynamic dependency of the bundle, except the ones the system must give (glibc, kernel bits, video drivers, the wayland client stack:
# Mesa on new distros needs libwayland symbols NEWER than the floor's one and resolves them from the already loaded bundled copy (undefined symbol crash),
# while every desktop that has a wayland session has these libraries in the system anyway)
is_allowed() { case "$(basename "$1")" in
  linux-vdso.so*|ld-linux-*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libnsl.so*|libresolv.so*|libutil.so*|libanl.so*|libBrokenLocale*|libSegFault*|libgcc_s.so*|libGL.so*|libGLX_*|libGLX.so*|libOpenGL.so*|libGLESv*|libEGL.so*|libGLdispatch*|libglapi*|libdrm_*|libdrm.so*|libvulkan*|libLLVM*|libwayland*|libgallium*|dri|libnvidia*|libcuda*) return 0 ;; *) return 1 ;; esac; }
collect_libs() {
  LD_LIBRARY_PATH="$APP/usr/lib" ldd "$1" 2>/dev/null | awk '{ if ($1 ~ /^\//) print $1; else if ($2 == "=>") print $3 }' | while read -r line; do
    case "$line" in ''|not*|statically*) continue ;; esac
    case "$line" in "$APP"/*) continue ;; esac
    is_allowed "$line" && continue
    local path="$APP/usr/lib/$(basename "$line")"
    if [ ! -f "$path" ]; then cp -L "$line" "$path" 2>/dev/null || continue; echo "$line"; fi
  done
}
echo "  libraries:"
find "$APP/usr/plugins" -name '*.so' > "$BUILD/plugin_list.txt"
NEWLIST="$OSMC_BIN"
while : ; do
  FOUND=""
  while read -r b; do
    [ -n "$b" ] || continue
    NEW="$(collect_libs "$b" | tr '\n' ' ')"
    if [ -n "$NEW" ]; then   # the log shows file names, the list keeps full paths (they are ldd'ed on the next pass)
      NAMES=""; for f in $NEW; do NAMES="$NAMES$(basename "$f") "; done
      echo "  $NAMES"; FOUND="$FOUND $NEW"
    fi
  done < <({ cat "$BUILD/plugin_list.txt"; echo "$NEWLIST"; } | tr ' ' '\n'; find "$APP/usr/lib" -maxdepth 1 -name '*.so*'; echo)
  [ -z "$FOUND" ] && break
  NEWLIST="$NEWLIST $FOUND"
done
strip --strip-unneeded "$APP/usr/osmc" 2>/dev/null || true
find "$APP/usr/lib" -type f -exec strip --strip-unneeded {} + 2>/dev/null || true
# DT_RPATH (not RUNPATH): the transitive dependencies (proj from gdal, ...) must be found next to the bundle without environment — the tar.gz on servers relies on this
patchelf --force-rpath --set-rpath '$ORIGIN/lib' "$APP/usr/osmc" 2>/dev/null || true
while read -r p; do patchelf --force-rpath --set-rpath '$ORIGIN/../lib' "$p" 2>/dev/null || true; done < "$BUILD/plugin_list.txt"

echo "=== 4/5 checks ==="
# (a) purity: every binary of the bundle must find everything either in the bundle or on the is_allowed list
# (find -exec VAR=x cmd does NOT work — find would exec a program named "VAR=x"; the check runs ldd in a real loop;
# version errors like "version CURL_OPENSSL_4 not found" are ldd output too — grep 'not found' over merged streams)
BAD=""
while read -r b; do
  ALL="$(LD_LIBRARY_PATH="$APP/usr/lib" ldd "$b" 2>&1 || true)"
  V="$(printf '%s\n' "$ALL" | grep 'not found' || true)"
  [ -n "$V" ] && BAD="$BAD$V"$'\n'
  for p in $(printf '%s\n' "$ALL" | awk '$2 == "=>" && $3 ~ /^\// { print $3 }'); do
    case "$p" in "$APP"/*) continue ;; esac
    is_allowed "$p" || BAD="$BAD$(basename "$p") (needed by $(basename "$b"), resolved from $(dirname "$p"))"$'\n'
  done
done < <(find "$APP/usr/osmc" "$APP/usr/lib" "$APP/usr/plugins" -type f \( -name '*.so*' -o -name osmc \))
[ -z "$BAD" ] || { echo "NOT PURE — dependencies outside the bundle (or unresolved):"; printf '%s' "$BAD"; exit 1; }
# (a2) the floor: nothing in the bundle may require a newer glibc than Debian 11 ships (a dirty build system shows up here)
FLOOR=2.31
NEWEST="$(find "$APP/usr/osmc" "$APP/usr/lib" "$APP/usr/plugins" -type f \( -name '*.so*' -o -name osmc \) -print0 | xargs -0 objdump -T 2>/dev/null | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sed 's/GLIBC_//' | sort -V | tail -1 || true)"
{ [ -n "$NEWEST" ] && [ "$(printf '%s\n%s\n' "$FLOOR" "$NEWEST" | sort -V | tail -1)" = "$FLOOR" ]; } || { echo "the bundle needs GLIBC_$NEWEST — newer than the $FLOOR floor (is the build system a clean Debian 11?)"; exit 1; }
echo "    purity: ok (the bundle needs at most GLIBC_$NEWEST)"
# (b) the program must run from the bundle without the system Qt/GDAL
mkdir -p "$PKG"
TEST_LOG="$PKG/smoke.log"; rm -f "$TEST_LOG"
( cd "$APP" && env -u DISPLAY -u WAYLAND_DISPLAY -u LD_LIBRARY_PATH -u GDAL_DATA -u PROJ_DATA -u PROJ_LIB \
  QT_QPA_PLATFORM=offscreen QT_PLUGIN_PATH="$APP/usr/plugins" "$APP/usr/osmc" "$ROOT/tests/data/osm_buildings.osm" \
  --dry-run --nogui --lang en --log "$TEST_LOG" )
grep -q "Dry run" "$TEST_LOG" || { echo "the check failed: no result in $TEST_LOG"; exit 1; }
echo "    dry-run from the bundle: ok"

echo "=== 5/5 packing ==="
# the tar.gz for servers: the bundle packed as an OSMC/ folder — the same shape as the Windows zip: after unpacking
# run OSMC/osmc (rpath finds the libraries next to the executable, no FUSE needed on the server). The "usr" level
# stays only inside the AppImage (its runtime requires it); in the tar.gz it would just look like "unpack into /usr"
tar -C "$APP" -czf "$PKG/OSMC-$VER-linux64.tar.gz" --transform "s|^usr$|OSMC|" --transform "s|^usr/|OSMC/|" usr
AT="$BUILD/dl/appimagetool.AppImage"
[ -f "$AT" ] || wget -q -O "$AT" "$AT_URL"
chmod +x "$AT"
rm -f "$PKG/OSMC-$VER-x86_64.AppImage"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$AT" "$APP" "$PKG/OSMC-$VER-x86_64.AppImage"
chmod +x "$PKG/OSMC-$VER-x86_64.AppImage"
# the AppImage itself must also run and export (offscreen, no system libs from Qt/GDAL)
( cd "$PKG" && APPIMAGE_EXTRACT_AND_RUN=1 env -u DISPLAY -u WAYLAND_DISPLAY ./OSMC-$VER-x86_64.AppImage "$ROOT/tests/data/osm_buildings.osm" --dry-run --nogui --lang en --log "$PKG/smoke_appimage.log" )
grep -q "Dry run" "$PKG/smoke_appimage.log" || { echo "the AppImage check failed, see $PKG/smoke_appimage.log"; exit 1; }
ls -l "$PKG"
if [ -n "${OSMC_OUT:-}" ]; then mkdir -p "$OSMC_OUT/package"; cp -f "$PKG/OSMC-$VER-x86_64.AppImage" "$PKG/OSMC-$VER-linux64.tar.gz" "$OSMC_OUT/package/"; fi
echo "Done: OSMC-$VER-x86_64.AppImage ($(du -h "$PKG/OSMC-$VER-x86_64.AppImage" | cut -f1))"
