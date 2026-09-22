#!/bin/bash
set -euo pipefail

PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR="${PROJECT_DIR}/build-linux-tests"

cd "${PROJECT_DIR}"

cmake \
    -S "${PROJECT_DIR}" \
    -B "${BUILD_DIR}" \
    -G Ninja \
    -DBUILD_TESTING=ON

cmake --build "${BUILD_DIR}" --target aowis-tests --parallel

ctest \
    --test-dir "${BUILD_DIR}" \
    --output-on-failure \
    -L '^aowis$'
