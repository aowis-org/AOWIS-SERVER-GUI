#!/bin/bash
set -euo pipefail

# The Docker environment may default to the ASCII-only C locale. Host-side Qt
# tools (particularly rcc) require UTF-8 and otherwise warn on each
# invocation during the build.
export LANG=C.UTF-8
export LC_ALL=C.UTF-8

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

# The map renders with bgfx and its pre-compiled shaders: the image's Qt
# ShaderTools / qsb (built for the removed QRhi renderer) are no longer used.

cd /project

# bgfx shaders are pre-compiled and committed (src/map/bgfx/shaders/bin,
# cmake/aowis_bgfx_shaders.cmake): no shader compiler is built here. If the
# configure step reports stale essl shaders, regenerate them with a native
# build (./compile_linux.sh --shader-tools) and commit them.
mkdir -p build-wasm

# A build directory configured with an older/single-threaded Qt toolchain must not
# be reused. Keep normal generated output, but force CMake to regenerate toolchain state.
rm -f build-wasm/CMakeCache.txt
rm -rf build-wasm/CMakeFiles

"${QT_CMAKE}" \
    -S /project \
    -B /project/build-wasm \
    -DCMAKE_BUILD_TYPE=Release \
    -DAOWIS_BUILD_BGFX_PROBE="${AOWIS_BUILD_BGFX_PROBE:-OFF}" \
    -DAOWIS_ENABLE_BGFX_MAP="${AOWIS_ENABLE_BGFX_MAP:-ON}" \
    -DAOWIS_WASM_FUNCTION_NAMES="${AOWIS_WASM_FUNCTION_NAMES:-OFF}" \
    -DAOWIS_WASM_SIMD="${AOWIS_WASM_SIMD:-ON}" \
    -DCMAKE_CXX_FLAGS="-pthread" \
    -DCMAKE_EXE_LINKER_FLAGS="-lembind -pthread -sPTHREAD_POOL_SIZE=1"

cmake --build /project/build-wasm --parallel "$(nproc)"

echo "=== WASM build complete ==="
