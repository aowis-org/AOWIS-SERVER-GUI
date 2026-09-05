#!/bin/bash
set -euo pipefail

PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR="${PROJECT_DIR}/build-linux"

cd "${PROJECT_DIR}"

cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" -G Ninja
cmake --build "${BUILD_DIR}" --parallel

"${BUILD_DIR}/aowis-server-gui"
