#!/bin/sh
# Build thc-inspect-<version>-x86_64.AppImage: the read-only checker, as a single file for any distribution.
# Expects thc-inspect to be built already in $BUILD_DIR (cmake --build ... --target thc-inspect).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${BUILD_DIR:-$ROOT/build-inspect}
V=$(sed -n 's/^project(THC VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
cd "$B"
rm -rf AppDirI
mkdir -p AppDirI/usr/bin AppDirI/usr/share/applications AppDirI/usr/share/icons/hicolor/512x512/apps
cp thc-inspect AppDirI/usr/bin/
cp "$ROOT/packaging/linux/thc.png" AppDirI/usr/share/icons/hicolor/512x512/apps/thc-inspect.png
cat > AppDirI/usr/share/applications/thc-inspect.desktop <<EOT
[Desktop Entry]
Type=Application
Name=thc-inspect
Comment=Check what THC stores and whether it uses the network
Exec=thc-inspect
Icon=thc-inspect
Terminal=true
Categories=Utility;
EOT
[ -x linuxdeploy ] || { curl -sSfL -o linuxdeploy https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage; chmod +x linuxdeploy; }
APPIMAGE_EXTRACT_AND_RUN=1 LINUXDEPLOY_OUTPUT_VERSION=$V ./linuxdeploy --appdir AppDirI \
  -d AppDirI/usr/share/applications/thc-inspect.desktop -i AppDirI/usr/share/icons/hicolor/512x512/apps/thc-inspect.png --output appimage
# linuxdeploy names the file after the desktop entry
for f in thc-inspect-*.AppImage; do [ -f "$f" ] && echo "$f"; done
