#!/bin/bash
# Builds a universal (Apple silicon + Intel) release and packages it as a .zip and a .pkg installer.
#   packaging/package-macos.sh [version]
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:-$(grep -m1 'project(EXGF VERSION' CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')}"
BUILD=build-release
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DEX_COPY_AFTER_BUILD=OFF -DEX_BUILD_TESTS=ON
cmake --build "$BUILD" --config Release
NAME="EX-GF-$VERSION-macOS"
STAGE="dist/$NAME"
rm -rf "$STAGE" "dist/$NAME.zip" "dist/$NAME.pkg" && mkdir -p "$STAGE/VST3" "$STAGE/Components" "$STAGE/Applications"
for t in EXMultisample EXPolyWavetable EXPolyFM EXPolyExciter; do
  A="$BUILD/${t}_artefacts/Release"
  ditto "$A/VST3/"*.vst3 "$STAGE/VST3/$(basename "$A/VST3/"*.vst3)"
  ditto "$A/AU/"*.component "$STAGE/Components/$(basename "$A/AU/"*.component)"
  ditto "$A/Standalone/"*.app "$STAGE/Applications/$(basename "$A/Standalone/"*.app)"
done
# ad-hoc signatures (no Apple developer ID): Gatekeeper still needs the quarantine flag removed, see install.command
find "$STAGE" -maxdepth 2 \( -name "*.vst3" -o -name "*.component" -o -name "*.app" \) -exec codesign --force --deep -s - {} \;
cp packaging/macos/install.command docs/EX-GF-manual.pdf "$STAGE/"
cp packaging/INSTALL.txt "$STAGE/"
chmod +x "$STAGE/install.command"
(cd dist && ditto -c -k --keepParent "$NAME" "$NAME.zip")
# .pkg installer: plug-ins to /Library/Audio/Plug-Ins, apps to /Applications
PKGROOT="$(mktemp -d)"
mkdir -p "$PKGROOT/Library/Audio/Plug-Ins/VST3" "$PKGROOT/Library/Audio/Plug-Ins/Components" "$PKGROOT/Applications"
ditto "$STAGE/VST3" "$PKGROOT/Library/Audio/Plug-Ins/VST3"
ditto "$STAGE/Components" "$PKGROOT/Library/Audio/Plug-Ins/Components"
ditto "$STAGE/Applications" "$PKGROOT/Applications"
pkgbuild --root "$PKGROOT" --identifier com.freakiepie.ex-gf --version "$VERSION" --install-location / "dist/$NAME.pkg"
rm -rf "$PKGROOT"
shasum -a 256 "dist/$NAME.zip" "dist/$NAME.pkg"
