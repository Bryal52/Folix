#!/bin/sh
# Builds Folix.app, bundles Qt into it, signs it locally and installs it into /Applications.
set -e
cd "$(dirname "$0")"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build

rm -rf dist
mkdir -p dist
cp -R build/Folix.app dist/
/opt/homebrew/opt/qtbase/bin/macdeployqt dist/Folix.app -always-overwrite
# SVG icon engine is not used and its QtSvg dependency lives outside qtbase
rm -rf dist/Folix.app/Contents/PlugIns/iconengines

codesign --force --deep --sign - dist/Folix.app
rm -rf /Applications/Folix.app
cp -R dist/Folix.app /Applications/
xattr -dr com.apple.quarantine /Applications/Folix.app 2>/dev/null || true
echo "Installed /Applications/Folix.app"
