#!/bin/bash
set -euo pipefail

PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "${PROJECT_DIR}"

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
    -e AOWIS_BUILD_BGFX_PROBE="${AOWIS_BUILD_BGFX_PROBE:-OFF}" \
    -e AOWIS_ENABLE_BGFX_MAP="${AOWIS_ENABLE_BGFX_MAP:-ON}" \
    -e AOWIS_WASM_FUNCTION_NAMES="${AOWIS_WASM_FUNCTION_NAMES:-OFF}" \
    -e AOWIS_WASM_SIMD="${AOWIS_WASM_SIMD:-ON}" \
    "${AOWIS_WASM_IMAGE}" \
    /bin/bash /project/tools/qt-emscripten/build_wasm_inside_container.sh

# Qt's default HTML wrapper does not work with setShortcut and keyPressEvents reliably.
# Replace it with the project wrapper.
cp tools/qt-emscripten/index.html build-wasm/
cp tools/qt-emscripten/.htaccess build-wasm/
if [ "${AOWIS_BUILD_BGFX_PROBE:-OFF}" = "ON" ]; then
    # Include possible Emscripten worker JS and sidecar WASM files, not just
    # the application JS/WASM pair.
    cp build-wasm/tests/bgfx_probe/aowis-bgfx-probe*.js build-wasm/
    cp build-wasm/tests/bgfx_probe/aowis-bgfx-probe*.wasm build-wasm/
    cp tools/qt-emscripten/bgfx-probe.html build-wasm/
    cp tools/qt-emscripten/bgfx-probe-stack.js build-wasm/
else
    rm -f build-wasm/aowis-bgfx-probe*.js build-wasm/aowis-bgfx-probe*.wasm \
          build-wasm/bgfx-probe.html build-wasm/bgfx-probe-stack.js
fi
cp tools/qt-emscripten/aowis-bgfx-map.js build-wasm/
# The JavaScript/WebGL map renderer (monitor and editor) was removed. Do not
# carry its scripts forward from an older build tree into build-wasm-dist.
rm -f build-wasm/aowis-browser-map.js build-wasm/aowis-browser-vector.js \
      build-wasm/aowis-browser-network-webgl.js build-wasm/aowis-browser-network.js \
      build-wasm/aowis-browser-map-editor.js build-wasm/aowis-browser-monitor-worker.js

# The WASM configuration is administered in the webroot. Never overwrite an existing
# administrator-edited build configuration with the source template.
if [ ! -f "$BUILD_CONFIG" ]; then
    cp "$CONFIG_TEMPLATE" "$BUILD_CONFIG"
fi

BGFX_MAP_JS_VERSION=$(sha256sum tools/qt-emscripten/aowis-bgfx-map.js | cut -c1-16)
sed -i "s|__AOWIS_BGFX_MAP_JS_VERSION__|${BGFX_MAP_JS_VERSION}|g" build-wasm/index.html

# The SVG and PNG icon folders were only loaded by the removed JavaScript map.
# Icons now come from the Qt resources inside the WASM binary.
rm -rf build-wasm/svg build-wasm/map-editor-icons

cp assets/img/favicon.ico build-wasm/.
cp assets/img/aowis.png build-wasm/.

echo "=== Creating cleaned up, ready for distribution directory build-wasm-dist ==="

rm -rf build-wasm-dist
mkdir -p build-wasm-dist

cp build-wasm/*.js build-wasm/*.wasm build-wasm/*.html build-wasm/favicon.ico build-wasm/aowis.png build-wasm/index.html build-wasm-dist 2>/dev/null || true
cp build-wasm/.htaccess build-wasm-dist/

# Preserve an administrator-edited distribution config across rebuilds. On the first
# build, initialize it from build-wasm, which itself is only initialized once.
if [ -n "$PRESERVED_DIST_CONFIG" ]; then
    cp "$PRESERVED_DIST_CONFIG" "$DIST_CONFIG"
else
    cp "$BUILD_CONFIG" "$DIST_CONFIG"
fi

echo "=== WASM build finished ==="
