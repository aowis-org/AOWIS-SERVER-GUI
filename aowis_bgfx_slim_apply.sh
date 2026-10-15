#!/usr/bin/env bash
# One-shot migration to the slim bgfx build. Run once from anywhere after
# extracting the patch ZIP; it deletes itself after success (on failure it
# stays, fix the reported problem and run it again: every step is idempotent).
#
# What it does, i.e. what a ZIP cannot carry:
#   1. removes the external/bgfx.cmake submodule (no longer used),
#   2. marks bx/bimg/bgfx shallow in .gitmodules,
#   3. pins bx/bimg/bgfx to the revisions the committed shader binaries in
#      src/map/bgfx/shaders/bin were compiled with (shader binaries must
#      match the bgfx runtime) and stages the gitlinks,
#   4. switches bx/bimg/bgfx to a sparse checkout (tools/bgfx_sparse_checkout.sh),
#   5. removes the Emscripten shims from tests/bgfx_probe (moved to
#      cmake/bgfx_emscripten) and stale bgfx.cmake build directories.
# Nothing is committed; review with "git status" and commit.
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
self="$(pwd)/$(basename -- "${BASH_SOURCE[0]}")"

if [[ "$(git rev-parse --show-toplevel 2>/dev/null)" != "$(pwd -P)" ]]; then
    printf 'Run this from the AOWIS-SERVER-GUI git checkout (not found at %s).\n' "$(pwd)" >&2
    exit 1
fi

# Revisions (bgfx.cmake's consistent bx/bimg/bgfx set of 2026-10-14) used to
# compile src/map/bgfx/shaders/bin.
bgfx_paths=(external/bx external/bimg external/bgfx)
declare -A pin=(
    [external/bx]=d4e6ed3cf4f75c355bb04088bc1c13cd919aafa6
    [external/bimg]=8e665781eff8e963a04924f1acc1d74324149bcb
    [external/bgfx]=fd541f05865f6e7fbfc302f81f5a71d2b05794eb
)

echo "=== 1. Removing the bgfx.cmake submodule ==="
if [[ "$(git ls-files --stage -- external/bgfx.cmake)" == '160000 '* ]]; then
    git submodule deinit -f -- external/bgfx.cmake
    # Also removes the .gitmodules section and stages both changes.
    git rm -q -f -- external/bgfx.cmake
fi
if git config -f .gitmodules --get-regexp '^submodule\.external/bgfx\.cmake\.' >/dev/null 2>&1; then
    git config -f .gitmodules --remove-section submodule.external/bgfx.cmake
fi
git config --remove-section submodule.external/bgfx.cmake 2>/dev/null || true
rm -rf -- external/bgfx.cmake "$(git rev-parse --git-common-dir)/modules/external/bgfx.cmake"

echo "=== 2. Marking bx/bimg/bgfx shallow ==="
for path in "${bgfx_paths[@]}"; do
    git config -f .gitmodules "submodule.${path}.shallow" true
done
# Drop the empty line bgfx.cmake's removed section may leave behind.
sed -i -e '/^$/d' .gitmodules
git add -- .gitmodules

echo "=== 3. Pinning bx/bimg/bgfx to the shader-compatible revisions ==="
for path in "${bgfx_paths[@]}"; do
    if ! git -C "$path" rev-parse --git-dir >/dev/null 2>&1 \
        || [[ "$(git -C "$path" rev-parse --show-toplevel 2>/dev/null)" != "$(pwd -P)/$path" ]]; then
        git submodule update --init --depth 1 -- "$path"
    fi
    sha="${pin[$path]}"
    if [[ "$(git -C "$path" rev-parse HEAD)" != "$sha" ]]; then
        if ! git -C "$path" cat-file -e "${sha}^{commit}" 2>/dev/null; then
            if [[ "$(git -C "$path" rev-parse --is-shallow-repository)" == "true" ]]; then
                git -C "$path" fetch --depth 1 origin "$sha"
            else
                # Keep an existing full clone complete.
                git -C "$path" fetch origin "$sha"
            fi
        fi
        git -C "$path" checkout -q --detach "$sha"
    fi
    git add -- "$path"
    printf '%-14s %s\n' "$path" "$(git -C "$path" log -1 --format='%h %cd' --date=short)"
done

echo "=== 4. Sparse checkout ==="
chmod +x tools/bgfx_sparse_checkout.sh git_submodule_init.sh git_submodule_update.sh compile_linux.sh
# Runtime set only; ./compile_linux.sh --shader-tools adds the shader
# compiler sources when they are needed.
tools/bgfx_sparse_checkout.sh --no-shader-tools

echo "=== 5. Removing moved and stale files ==="
for file in tests/bgfx_probe/bx_emscripten_compat.h tests/bgfx_probe/emscripten_version_compat.h; do
    if git ls-files --error-unmatch -- "$file" >/dev/null 2>&1; then
        git rm -q -f -- "$file"
    else
        rm -f -- "$file"
    fi
done
# Old bgfx.cmake build trees (the slim build uses different targets/paths).
rm -rf -- build-bgfx-shaderc-host build-linux/bgfx-build build-wasm/bgfx-build

echo "=== Done. Review and commit: ==="
git status --short -- .gitmodules external tests/bgfx_probe cmake src/map/bgfx/shaders/bin \
    tools/bgfx_sparse_checkout.sh
rm -f -- "$self"
