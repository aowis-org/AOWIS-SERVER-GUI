#!/bin/bash
set -e

echo "=== Starting WASM build using Docker ==="

# https://hub.docker.com/r/mattbas/qt-emscripten
docker run --rm \
    -v "$(pwd)":/project \
    -w /project \
    mattbas/qt-emscripten:6.10.2 \
    /bin/bash /project/tools/qt-emscripten/build_wasm_inside_container.sh

#ln -sf aowis-server-gui.html build-wasm/index.html
# Qt's default HTML wrap does not work with setShortcut and
# keyPressEvents reliably. We replace that with a fixed one
cp tools/qt-emscripten/index.html build-wasm/
cp tools/qt-emscripten/aowis-browser-map.js build-wasm/
cp tools/qt-emscripten/aowis-browser-network.js build-wasm/
cp tools/qt-emscripten/aowis-browser-map-editor.js build-wasm/

rm -rf build-wasm/map-editor-icons
mkdir -p build-wasm/map-editor-icons
cp assets/iconsets/gothic/junction.png build-wasm/map-editor-icons/
cp assets/iconsets/gothic/reservoir.png build-wasm/map-editor-icons/
cp assets/iconsets/gothic/tower.png build-wasm/map-editor-icons/
cp assets/iconsets/gothic/pipe.png build-wasm/map-editor-icons/
cp assets/iconsets/gothic/pump.png build-wasm/map-editor-icons/
cp assets/iconsets/gothic/valve.png build-wasm/map-editor-icons/
cp assets/iconsets/gothic/customer.png build-wasm/map-editor-icons/
cp assets/iconsets/gothic/electricity.png build-wasm/map-editor-icons/
cp assets/iconsets/gothic/energy.png build-wasm/map-editor-icons/
cp assets/iconsets/gothic/geomarker.png build-wasm/map-editor-icons/
#rm build-wasm/aowis-server-gui.html

#HTML_FILE="build-wasm/index.html"
#sed -i 's|<title>.*</title>|<title>AOWIS Controller</title>|' "$HTML_FILE"

cp assets/img/favicon.ico build-wasm/.

echo "=== Creating cleaned up, ready for distribution directory build-wasm-dist ==="

rm -rf build-wasm-dist

if [ ! -d "build-wasm-dist" ]; then
    mkdir build-wasm-dist
fi

cp build-wasm/*.js build-wasm/*.wasm build-wasm/*.html build-wasm/favicon.ico build-wasm/index.html build-wasm-dist 2>/dev/null || true
cp -r build-wasm/map-editor-icons build-wasm-dist/

echo "=== WASM build finished ==="
