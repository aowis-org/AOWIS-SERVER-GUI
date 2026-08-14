#!/bin/bash
set -euo pipefail

PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "${PROJECT_DIR}"

source tools/qt-emscripten/toolchain_versions.sh

echo "=== Preparing AOWIS Qt WebAssembly Docker image ==="
echo "Qt:         ${AOWIS_QT_VERSION}"
echo "Emscripten: ${AOWIS_EMSCRIPTEN_VERSION}"
echo "Image:      ${AOWIS_WASM_IMAGE}"

# Docker's isolated build network can fail DNS resolution on Linux hosts that use
# a local resolver (for example systemd-resolved). Qt's toolchain image needs
# network access while installing Ubuntu packages and downloading its pinned
# sources, so use the host network for RUN steps by default on Linux.
DOCKER_BUILD_NETWORK=${AOWIS_DOCKER_BUILD_NETWORK:-}
if [ -z "${DOCKER_BUILD_NETWORK}" ]; then
    if [ "$(uname -s)" = "Linux" ]; then
        DOCKER_BUILD_NETWORK=host
    else
        DOCKER_BUILD_NETWORK=default
    fi
fi

echo "Docker build network: ${DOCKER_BUILD_NETWORK}"

docker build \
    --network="${DOCKER_BUILD_NETWORK}" \
    --pull \
    --build-arg "QT_VERSION=${AOWIS_QT_VERSION}" \
    --build-arg "EMSCRIPTEN_VERSION=${AOWIS_EMSCRIPTEN_VERSION}" \
    --build-arg "QTBASE_SHA256=${AOWIS_QTBASE_SHA256}" \
    -f tools/qt-emscripten/Dockerfile.threaded \
    -t "${AOWIS_WASM_IMAGE}" \
    tools/qt-emscripten

echo "=== AOWIS Qt WebAssembly Docker image ready ==="
