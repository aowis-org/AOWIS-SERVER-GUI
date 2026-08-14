#!/bin/bash
set -euo pipefail

QT_WASM_PATH="${AOWIS_QT_WASM_PATH:-/opt/qt-wasm}"
QT_CMAKE="${QT_WASM_PATH}/bin/qt-cmake"
EMSDK_ROOT="${EMSDK:-/opt/emsdk}"

echo "=== Building AOWIS for WebAssembly ==="

if [ ! -f "${EMSDK_ROOT}/emsdk_env.sh" ]; then
    echo "Emscripten SDK not found at ${EMSDK_ROOT}." >&2
    exit 1
fi

if [ ! -x "${QT_CMAKE}" ]; then
    echo "Thread-enabled Qt WebAssembly installation not found at ${QT_WASM_PATH}." >&2
    echo "Rebuild the AOWIS WASM Docker image with ./prepare_wasm_docker.sh." >&2
    exit 1
fi

source "${EMSDK_ROOT}/emsdk_env.sh"

em++ --version | head -n 1
"${QT_CMAKE}" --version | head -n 1

cd /project
mkdir -p build-wasm

# A build directory configured with an older/single-threaded Qt toolchain must not
# be reused. Keep normal generated output, but force CMake to regenerate toolchain state.
rm -f build-wasm/CMakeCache.txt
rm -rf build-wasm/CMakeFiles

"${QT_CMAKE}" \
    -S /project \
    -B /project/build-wasm \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-pthread" \
    -DCMAKE_EXE_LINKER_FLAGS="-lembind -pthread -sPTHREAD_POOL_SIZE=1"

cmake --build /project/build-wasm --parallel "$(nproc)"

echo "=== WASM build complete ==="
