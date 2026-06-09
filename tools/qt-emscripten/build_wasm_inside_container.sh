#!/bin/bash
set -e

echo "=== Building AOWIS for WebAssembly ==="

cd /project

# Clean previous build
rm -rf build-wasm

if [ ! -d "build-wasm" ]; then
    mkdir build-wasm
fi

cd build-wasm

# Activate Emscripten environment
source /opt/emsdk/emsdk_env.sh

# Configure Qt WASM build
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE \
  -DQT_HOST_PATH=$QT_HOST_PATH \
  -DCMAKE_CXX_FLAGS="-matomics -mbulk-memory" \
  -DCMAKE_EXE_LINKER_FLAGS="-lembind" \
  -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

echo "=== WASM build complete ==="
