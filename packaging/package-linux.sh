#!/bin/bash
# Builds and packages the Linux release (VST3 + LV2 + standalone apps) as a .tar.gz.
#   packaging/package-linux.sh [version]
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:-$(grep -m1 'project(ExGirlfriend VERSION' CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')}"
BUILD=build-release
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DEX_COPY_AFTER_BUILD=OFF -DEX_BUILD_TESTS=ON
cmake --build "$BUILD" --config Release
NAME="ex-girlfriend-$VERSION-Linux-$(uname -m)"
STAGE="dist/$NAME"
rm -rf "$STAGE" && mkdir -p "$STAGE/vst3" "$STAGE/lv2" "$STAGE/bin"
for t in EXMultisample EXPolyWavetable EXPolyFM EXPolyExciter; do
  A="$BUILD/${t}_artefacts/Release"
  cp -r "$A/VST3/"*.vst3 "$STAGE/vst3/"
  cp -r "$A/LV2/"*.lv2 "$STAGE/lv2/"
  cp "$A/Standalone/"* "$STAGE/bin/"
done
cp packaging/linux/install.sh packaging/INSTALL.txt "$STAGE/"
chmod +x "$STAGE/install.sh"
tar -C dist -czf "dist/$NAME.tar.gz" "$NAME"
sha256sum "dist/$NAME.tar.gz"
