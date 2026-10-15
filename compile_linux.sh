#!/bin/bash
set -euo pipefail

PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR="${PROJECT_DIR}/build-linux"

usage()
{
    cat <<'HELP'
Usage: ./compile_linux.sh [--shader-tools]

  --shader-tools  Also build the bgfx shader compiler and regenerate the
                  committed shader binaries in src/map/bgfx/shaders/bin
                  (needed after editing a .sc file; commit the result).
HELP
}

SHADER_TOOLS=OFF
case "${1:-}" in
    '') ;;
    --shader-tools) SHADER_TOOLS=ON ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
esac

cd "${PROJECT_DIR}"

# The shader compiler sources are not part of the default sparse checkout.
if [ "${SHADER_TOOLS}" = "ON" ] && [ ! -d external/bgfx/tools/shaderc ]; then
    tools/bgfx_sparse_checkout.sh --shader-tools
fi

# Explicit values, so an existing build-linux cache configured while an
# option had another value is switched as well (option() defaults never
# override a cached value). Without --shader-tools no shader compiler is
# built: the committed binaries are embedded.
cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DAOWIS_ENABLE_BGFX_MAP=ON \
    -DAOWIS_BGFX_SHADER_TOOLS="${SHADER_TOOLS}"
cmake --build "${BUILD_DIR}" --parallel

"${BUILD_DIR}/aowis-server-gui"
