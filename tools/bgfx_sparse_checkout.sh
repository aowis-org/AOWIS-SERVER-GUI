#!/usr/bin/env bash
set -euo pipefail

# Restricts the bx/bimg/bgfx submodule working trees to what AOWIS builds
# (cmake/aowis_bgfx.cmake, cmake/aowis_bgfx_shaderc.cmake). A full bgfx
# checkout is ~270 MB (144 MB examples, 57 MB prebuilt DirectX compiler
# binaries, Dawn/Tint, ...); the runtime needs ~15 MB of it, the optional
# shader compiler another ~20 MB.
#
# Usage: tools/bgfx_sparse_checkout.sh [--shader-tools | --no-shader-tools | --full]
#
#   (none)             runtime set; keep the shader-compiler set if it was
#                      added earlier with --shader-tools
#   --shader-tools     runtime + shader-compiler sources
#   --no-shader-tools  runtime set only
#   --full             disable sparse checkout (complete upstream trees)
#
# Sparse checkout only changes which tracked files are present in the
# working tree; "git sparse-checkout disable" (or --full) restores all.

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

mode="keep"
case "${1:-}" in
    '') ;;
    --shader-tools) mode="tools" ;;
    --no-shader-tools) mode="runtime" ;;
    --full) mode="full" ;;
    *)
        printf 'Unknown option: %s\n' "$1" >&2
        exit 2
        ;;
esac

for path in external/bx external/bimg external/bgfx; do
    if ! git -C "$path" rev-parse --git-dir >/dev/null 2>&1; then
        printf '%s is not checked out; run ./git_submodule_init.sh first.\n' "$path" >&2
        exit 1
    fi
done

if [[ "$mode" == "full" ]]; then
    for path in external/bx external/bimg external/bgfx; do
        git -C "$path" sparse-checkout disable
    done
    exit 0
fi

if [[ "$mode" == "keep" ]]; then
    # Keep the shader-compiler set only if it was explicitly added before
    # (a not yet sparse, full checkout counts as runtime-only).
    mode="runtime"
    if [[ "$(git -C external/bgfx config --bool core.sparseCheckout 2>/dev/null || true)" == "true" ]] \
        && git -C external/bgfx sparse-checkout list 2>/dev/null | grep -qx '/tools/shaderc/'; then
        mode="tools"
    fi
fi

# Runtime: headers and sources, plus the bgfx 3rdparty headers the
# renderers include (EGL/GL/Vulkan headers, RenderDoc API, DirectX headers
# for Windows, metal-cpp for macOS).
bgfx_patterns=(
    '/include/'
    '/src/'
    '/3rdparty/khronos/'
    '/3rdparty/renderdoc/'
    '/3rdparty/directx-headers/'
    '/3rdparty/metal-cpp/'
)
if [[ "$mode" == "tools" ]]; then
    # shaderc without Tint/Dawn/DXC (see cmake/aowis_bgfx_shaderc.cmake).
    bgfx_patterns+=(
        '/tools/shaderc/'
        '/3rdparty/glslang/'
        '/3rdparty/spirv-tools/'
        '/3rdparty/spirv-headers/'
        '/3rdparty/spirv-cross/'
    )
fi

git -C external/bx sparse-checkout set --no-cone '/include/' '/src/'
git -C external/bimg sparse-checkout set --no-cone '/include/' '/src/'
git -C external/bgfx sparse-checkout set --no-cone "${bgfx_patterns[@]}"

printf 'bgfx sparse checkout: %s set\n' "$mode"
du -sh external/bx external/bimg external/bgfx 2>/dev/null || true
