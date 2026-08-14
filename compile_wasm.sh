#!/bin/bash
set -euo pipefail

PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
HASH_STATE_FILE="${PROJECT_DIR}/.aowis-build-state/content-hashes.json"

cd "${PROJECT_DIR}"

python3 tools/build/content_hash_guard.py \
    --source-dir "${PROJECT_DIR}" \
    --state-file "${HASH_STATE_FILE}"

source tools/qt-emscripten/toolchain_versions.sh

echo "=== Starting WASM build using AOWIS Docker toolchain ==="

CONFIG_TEMPLATE="tools/qt-emscripten/aowis-server-gui.ini"
BUILD_CONFIG="build-wasm/aowis-server-gui.ini"
DIST_CONFIG="build-wasm-dist/aowis-server-gui.ini"
PRESERVED_DIST_CONFIG=""

cleanup()
{
    if [ -n "$PRESERVED_DIST_CONFIG" ] && [ -f "$PRESERVED_DIST_CONFIG" ]; then
        rm -f "$PRESERVED_DIST_CONFIG"
    fi
}
trap cleanup EXIT

if [ -f "$DIST_CONFIG" ]; then
    PRESERVED_DIST_CONFIG=$(mktemp)
    cp "$DIST_CONFIG" "$PRESERVED_DIST_CONFIG"
fi

if ! docker image inspect "${AOWIS_WASM_IMAGE}" >/dev/null 2>&1; then
    echo "=== AOWIS WASM toolchain image not found; preparing it first ==="
    ./prepare_wasm_docker.sh
fi

echo "Toolchain image: ${AOWIS_WASM_IMAGE}"

docker run --rm \
    -v "$(pwd)":/project \
    -w /project \
    "${AOWIS_WASM_IMAGE}" \
    /bin/bash /project/tools/qt-emscripten/build_wasm_inside_container.sh

# Qt's default HTML wrapper does not work with setShortcut and keyPressEvents reliably.
# Replace it with the project wrapper.
cp tools/qt-emscripten/index.html build-wasm/
cp tools/qt-emscripten/.htaccess build-wasm/
cp tools/qt-emscripten/aowis-browser-map.js build-wasm/
cp tools/qt-emscripten/aowis-browser-vector.js build-wasm/
cp tools/qt-emscripten/aowis-browser-network-webgl.js build-wasm/
cp tools/qt-emscripten/aowis-browser-network.js build-wasm/
cp tools/qt-emscripten/aowis-browser-map-editor.js build-wasm/
# Do not carry the retired Canvas renderer worker forward from an older build tree.
rm -f build-wasm/aowis-browser-monitor-worker.js

# The WASM configuration is administered in the webroot. Never overwrite an existing
# administrator-edited build configuration with the source template.
if [ ! -f "$BUILD_CONFIG" ]; then
    cp "$CONFIG_TEMPLATE" "$BUILD_CONFIG"
fi

MAP_JS_VERSION=$(sha256sum tools/qt-emscripten/aowis-browser-map.js | cut -c1-16)
VECTOR_JS_VERSION=$(sha256sum tools/qt-emscripten/aowis-browser-vector.js | cut -c1-16)
NETWORK_JS_VERSION=$(sha256sum \
    tools/qt-emscripten/aowis-browser-network-webgl.js \
    tools/qt-emscripten/aowis-browser-network.js | sha256sum | cut -c1-16)
EDITOR_JS_VERSION=$(sha256sum tools/qt-emscripten/aowis-browser-map-editor.js | cut -c1-16)
sed -i "s|__AOWIS_MAP_JS_VERSION__|${MAP_JS_VERSION}|g" build-wasm/index.html
sed -i "s|__AOWIS_VECTOR_JS_VERSION__|${VECTOR_JS_VERSION}|g" build-wasm/index.html
sed -i "s|__AOWIS_NETWORK_JS_VERSION__|${NETWORK_JS_VERSION}|g" build-wasm/index.html
sed -i "s|__AOWIS_EDITOR_JS_VERSION__|${EDITOR_JS_VERSION}|g" build-wasm/index.html

rm -rf build-wasm/svg
cp -r tools/qt-emscripten/svg build-wasm/

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

cp assets/img/favicon.ico build-wasm/.
cp assets/img/aowis.png build-wasm/.

echo "=== Creating cleaned up, ready for distribution directory build-wasm-dist ==="

rm -rf build-wasm-dist
mkdir -p build-wasm-dist

cp build-wasm/*.js build-wasm/*.wasm build-wasm/*.html build-wasm/favicon.ico build-wasm/aowis.png build-wasm/index.html build-wasm-dist 2>/dev/null || true
cp build-wasm/.htaccess build-wasm-dist/
cp -r build-wasm/map-editor-icons build-wasm-dist/
cp -r build-wasm/svg build-wasm-dist/

# Preserve an administrator-edited distribution config across rebuilds. On the first
# build, initialize it from build-wasm, which itself is only initialized once.
if [ -n "$PRESERVED_DIST_CONFIG" ]; then
    cp "$PRESERVED_DIST_CONFIG" "$DIST_CONFIG"
else
    cp "$BUILD_CONFIG" "$DIST_CONFIG"
fi

echo "=== WASM build finished ==="
