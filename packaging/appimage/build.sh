#!/bin/sh
# Build THC-0.1.3-x86_64.AppImage. libmpv is not bundled: THC uses the
# system's libmpv (install mpv) for video; music works without it.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${BUILD_DIR:-$ROOT/build-appimage}
cmake -S "$ROOT" -B "$B" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$B" --parallel
rm -rf "$B/AppDir"
DESTDIR="$B/AppDir" cmake --install "$B"
cd "$B"
[ -x linuxdeploy ] || { curl -sSfL -o linuxdeploy https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage; chmod +x linuxdeploy; }
APPIMAGE_EXTRACT_AND_RUN=1 LINUXDEPLOY_OUTPUT_VERSION=0.1.3 ./linuxdeploy --appdir AppDir \
  -d AppDir/usr/share/applications/thc.desktop -i AppDir/usr/share/icons/hicolor/512x512/apps/thc.png --output appimage
ls -1 THC-*.AppImage
