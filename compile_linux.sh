#!/bin/bash
set -euo pipefail

PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR="${PROJECT_DIR}/build-linux"
HASH_STATE_FILE="${PROJECT_DIR}/.aowis-build-state/content-hashes.json"

cd "${PROJECT_DIR}"

python3 tools/build/content_hash_guard.py \
    --source-dir "${PROJECT_DIR}" \
    --state-file "${HASH_STATE_FILE}"

cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" -G Ninja
cmake --build "${BUILD_DIR}" --parallel

"${BUILD_DIR}/aowis-server-gui"
