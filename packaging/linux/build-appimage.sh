#!/usr/bin/env bash
# Build an OCTAVE AppImage for Linux (x86_64 or aarch64, detected from uname -m).
#
# Layout matches the binary's runtime asset lookup in src/main.cpp:
#   applicationDirPath() -> AppDir/usr/bin
#   ../frontend          -> AppDir/usr/frontend       <-- this is what we ship
# Putting the QML tree anywhere else (e.g. usr/share/octave/frontend) breaks
# the lookup and the app exits silently with no window.
#
# Inputs:  none (run from the repo root or any cwd; script resolves its own paths)
# Output:  dist/OCTAVE-${VERSION}-linux-<arch>.AppImage
#
# Required system packages (Ubuntu 22.04): build-essential cmake ninja-build
#   pkg-config qt6-base-dev qt6-declarative-dev qt6-multimedia-dev
#   qt6-serialport-dev qt6-networkauth-dev qt6-connectivity-dev
#   qml6-module-qtquick-controls2 libtag1-dev fuse libfuse2 wget file
#
# Required system packages (Arch, local-test fallback): qt6-base qt6-declarative
#   qt6-multimedia qt6-serialport qt6-networkauth qt6-connectivity taglib
#   fuse2 cmake ninja wget
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build-appimage"
APPDIR="${REPO_ROOT}/AppDir"
DIST_DIR="${REPO_ROOT}/dist"
TOOLS_DIR="${REPO_ROOT}/packaging/linux/tools"

VERSION="${OCTAVE_VERSION:-${GITHUB_REF_NAME:-}}"
VERSION="${VERSION#v}"
[ -z "$VERSION" ] && VERSION="$(grep -oP 'project\(OCTAVE VERSION \K[0-9.]+' "$REPO_ROOT/CMakeLists.txt" || echo 0.0.0)"

echo "==> OCTAVE AppImage build (version=$VERSION)"

# ---- 1. Configure + build ------------------------------------------------
echo "==> Configuring CMake (Release)"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release

echo "==> Building"
cmake --build "$BUILD_DIR" -j

# ---- 2. Lay out AppDir ---------------------------------------------------
echo "==> Staging AppDir at $APPDIR"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/frontend"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"

cp "$BUILD_DIR/octave" "$APPDIR/usr/bin/octave"
cp -r "$REPO_ROOT/frontend/." "$APPDIR/usr/frontend/"
# Google platform-tools (adb) next to the binary, if fetched
# (scripts/fetch_platform_tools.py), so phone mirroring needs no user install.
if [ -d "$REPO_ROOT/tools/platform-tools/linux" ]; then
    mkdir -p "$APPDIR/usr/bin/platform-tools"
    cp -r "$REPO_ROOT/tools/platform-tools/linux/." "$APPDIR/usr/bin/platform-tools/"
fi

# ---- 3. Desktop file + icon ---------------------------------------------
cat > "$APPDIR/usr/share/applications/octave.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=OCTAVE
Comment=Open-source Cross-platform Telematics for Augmented Vehicle Experience
Exec=octave
Icon=octave
Categories=AudioVideo;Audio;Player;
Terminal=false
EOF

# Reuse a project icon if one exists; otherwise generate a minimal placeholder.
ICON_SRC=""
for candidate in \
    "$REPO_ROOT/frontend/images/octave.png" \
    "$REPO_ROOT/frontend/images/icon.png" \
    "$REPO_ROOT/android/res/mipmap-xxxhdpi/ic_launcher.png" \
    "$REPO_ROOT/android/res/mipmap-xxhdpi/ic_launcher.png"; do
    if [ -f "$candidate" ]; then ICON_SRC="$candidate"; break; fi
done

if [ -n "$ICON_SRC" ]; then
    echo "==> Using icon: $ICON_SRC"
    cp "$ICON_SRC" "$APPDIR/usr/share/icons/hicolor/256x256/apps/octave.png"
else
    echo "==> No project icon found, writing placeholder SVG"
    cat > "$APPDIR/usr/share/icons/hicolor/scalable/apps/octave.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256">
  <rect width="256" height="256" fill="#2196F3" rx="32"/>
  <text x="128" y="160" text-anchor="middle" font-family="sans-serif"
        font-size="120" font-weight="bold" fill="white">O</text>
</svg>
EOF
fi

# linuxdeploy expects the .desktop and icon to also live at the AppDir root.
cp "$APPDIR/usr/share/applications/octave.desktop" "$APPDIR/octave.desktop"
if [ -f "$APPDIR/usr/share/icons/hicolor/256x256/apps/octave.png" ]; then
    cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/octave.png" "$APPDIR/octave.png"
elif [ -f "$APPDIR/usr/share/icons/hicolor/scalable/apps/octave.svg" ]; then
    cp "$APPDIR/usr/share/icons/hicolor/scalable/apps/octave.svg" "$APPDIR/octave.svg"
fi

# ---- 4. Custom AppRun (sets QML / Qt env before exec) -------------------
# linuxdeploy generates an AppRun, but we override it so QML2_IMPORT_PATH
# also includes our usr/frontend tree (linuxdeploy only points it at usr/qml).
cat > "$APPDIR/AppRun" <<'EOF'
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "${0}")")"
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$HERE/usr/plugins:${QT_PLUGIN_PATH:-}"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/usr/plugins/platforms"
export QML2_IMPORT_PATH="$HERE/usr/qml:$HERE/usr/frontend:${QML2_IMPORT_PATH:-}"
export QML_IMPORT_PATH="$QML2_IMPORT_PATH"
exec "$HERE/usr/bin/octave" "$@"
EOF
chmod +x "$APPDIR/AppRun"

# ---- 5. Fetch linuxdeploy + qt plugin -----------------------------------
mkdir -p "$TOOLS_DIR"
cd "$TOOLS_DIR"

# linuxdeploy publishes x86_64 and aarch64 builds under the same "continuous" tag.
ARCH="$(uname -m)"
LD_BIN="linuxdeploy-${ARCH}.AppImage"
LDQT_BIN="linuxdeploy-plugin-qt-${ARCH}.AppImage"
LD_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/${LD_BIN}"
LDQT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/${LDQT_BIN}"

if [ ! -x "$LD_BIN" ]; then
    echo "==> Downloading linuxdeploy"
    wget -q --show-progress -O "$LD_BIN" "$LD_URL"
    chmod +x "$LD_BIN"
fi
if [ ! -x "$LDQT_BIN" ]; then
    echo "==> Downloading linuxdeploy-plugin-qt"
    wget -q --show-progress -O "$LDQT_BIN" "$LDQT_URL"
    chmod +x "$LDQT_BIN"
fi

# ---- 6. Locate libtag (linuxdeploy occasionally misses it) --------------
# Avoid `awk ... exit` here: it would close the pipe early and SIGPIPE the
# upstream ldconfig, which `set -o pipefail` then turns into a script abort.
LDCONFIG_OUT="$(ldconfig -p || true)"
TAGLIB_SO="$(echo "$LDCONFIG_OUT" | grep -m1 -oE '/[^ ]*libtag\.so\.[0-9][^ ]*' || true)"
if [ -z "$TAGLIB_SO" ] || [ ! -e "$TAGLIB_SO" ]; then
    echo "WARNING: libtag.so not found via ldconfig; AppImage may be missing it."
    TAGLIB_ARG=""
else
    echo "==> Bundling libtag from $TAGLIB_SO"
    TAGLIB_ARG="--library=$TAGLIB_SO"
fi

# ---- 6b. Qt Quick 3D asset importers ------------------------------------
# linuxdeploy-plugin-qt bundles a fixed set of plugin categories (platforms,
# imageformats, styles, ...) and does not know about "assetimporters". Without
# it QtQuick3D's RuntimeLoader loads but cannot parse frontend/assets/cam.glb,
# so the 3D vehicle view renders an empty scene and logs
#   Failed to load asset import plugin with key: "assimp"
# Copy the directory in ourselves; skip quietly when Quick3D isn't installed
# (the build is valid without it — the Sensors hub greys out the 3D card).
# CMake wrote this in step 1 iff it found Quick3D — it names the plugin root of the
# Qt the build actually linked against, which PATH lookups can easily get wrong.
QT_PLUGIN_ROOT="$(cat "$BUILD_DIR/qt_plugins_path.txt" 2>/dev/null || true)"
if [ -n "$QT_PLUGIN_ROOT" ] && [ -d "$QT_PLUGIN_ROOT/assetimporters" ]; then
    echo "==> Bundling Qt Quick 3D asset importers from $QT_PLUGIN_ROOT/assetimporters"
    mkdir -p "$APPDIR/usr/plugins/assetimporters"
    cp -a "$QT_PLUGIN_ROOT/assetimporters/." "$APPDIR/usr/plugins/assetimporters/"
else
    echo "==> No Qt Quick 3D asset importers found; 3D vehicle view will be unavailable"
fi

# ---- 6c. Optionally drop Qt plugins that cannot be deployed ---------------
# OCTAVE_DROP_QT_PLUGINS: space-separated paths relative to Qt's plugins/
# directory, e.g. "multimedia/libgstreamermediaplugin.so". Deleted from the
# Qt install AFTER the CMake build (Qt's CMake config refuses to configure
# when a listed plugin file is missing) and BEFORE linuxdeploy-plugin-qt
# walks plugins/ (which aborts on an unresolvable dependency). Used by the
# arm64 CI job: the arm64 Qt package's GStreamer plugin needs
# libgstphotography, which Ubuntu 22.04 does not ship, and OCTAVE uses the
# FFmpeg media backend anyway.
if [ -n "${OCTAVE_DROP_QT_PLUGINS:-}" ]; then
    QT_PLUGINS_DIR="$("$(dirname "${QMAKE:-$(command -v qmake6 || command -v qmake)}")/qmake6" -query QT_INSTALL_PLUGINS 2>/dev/null \
        || "${QMAKE:-qmake6}" -query QT_INSTALL_PLUGINS)"
    for p in $OCTAVE_DROP_QT_PLUGINS; do
        echo "==> Dropping Qt plugin $QT_PLUGINS_DIR/$p"
        rm -f "$QT_PLUGINS_DIR/$p"
    done
fi

# ---- 7. Run linuxdeploy --------------------------------------------------
cd "$REPO_ROOT"

# linuxdeploy-plugin-qt scans this path for QML modules it must bundle.
export QML_SOURCES_PATHS="$APPDIR/usr/frontend"
# Skip linuxdeploy's auto-AppRun (we wrote our own with the right QML paths).
export DEPLOY_PLATFORM_THEMES=1

OUTPUT_NAME="OCTAVE-${VERSION}-linux-${ARCH}.AppImage"

echo "==> Running linuxdeploy"
"$TOOLS_DIR/$LD_BIN" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/octave" \
    --desktop-file "$APPDIR/usr/share/applications/octave.desktop" \
    $([ -f "$APPDIR/usr/share/icons/hicolor/256x256/apps/octave.png" ] \
        && echo "--icon-file=$APPDIR/usr/share/icons/hicolor/256x256/apps/octave.png" \
        || echo "--icon-file=$APPDIR/usr/share/icons/hicolor/scalable/apps/octave.svg") \
    $TAGLIB_ARG \
    --plugin qt \
    --output appimage

# linuxdeploy writes <Name>-<arch>.AppImage in cwd; rename to include version.
mkdir -p "$DIST_DIR"
PRODUCED=$(ls -t OCTAVE*.AppImage 2>/dev/null | head -n1 || true)
if [ -z "$PRODUCED" ] || [ ! -f "$PRODUCED" ]; then
    echo "ERROR: linuxdeploy did not produce an AppImage"
    exit 1
fi
mv "$PRODUCED" "$DIST_DIR/$OUTPUT_NAME"
echo "==> Wrote $DIST_DIR/$OUTPUT_NAME"
ls -lh "$DIST_DIR/$OUTPUT_NAME"
