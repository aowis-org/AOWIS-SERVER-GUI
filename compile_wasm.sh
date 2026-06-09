#!/bin/bash
set -e

echo "=== Starting WASM build using Docker ==="

# https://hub.docker.com/r/mattbas/qt-emscripten
docker run --rm \
    -v "$(pwd)":/project \
    -w /project \
    mattbas/qt-emscripten:6.10.2 \
    /bin/bash /project/tools/qt-emscripten/build_wasm_inside_container.sh

ln -sf aowis-server-gui.html build-wasm/index.html
cp assets/img/favicon.ico build-wasm/.

echo "=== WASM build finished ==="
