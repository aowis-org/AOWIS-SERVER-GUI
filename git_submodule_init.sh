#!/usr/bin/env bash
set -euo pipefail

# Initialize every pinned submodule revision.
#
# Usage: ./git_submodule_init.sh [--shader-tools]
#
#   --shader-tools  also check out the bgfx shader-compiler sources
#                   (needed only to regenerate src/map/bgfx/shaders/bin,
#                   see ./compile_linux.sh --shader-tools)
#
# bx, bimg and bgfx are fetched shallow (only the pinned commit) and checked
# out sparse (only what cmake/aowis_bgfx.cmake builds), see
# tools/bgfx_sparse_checkout.sh. bgfx.cmake is no longer used.

# Resolve paths relative to this script, not the caller's working directory.
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

sparse_option=""
case "${1:-}" in
    '') ;;
    --shader-tools) sparse_option="--shader-tools" ;;
    *)
        printf 'Usage: %s [--shader-tools]\n' "$0" >&2
        exit 2
        ;;
esac

# A .gitmodules entry alone is not a Git submodule: the superproject index
# also needs a gitlink (mode 160000). ZIP patches cannot carry gitlinks.
# For the first checkout using the bgfx ZIP patch, register missing gitlinks
# through the updater. Once committed, normal clones skip this bootstrap.
bgfx_paths=(external/bx external/bimg external/bgfx)
for path in "${bgfx_paths[@]}"; do
    if [[ "$(git ls-files --stage -- "$path")" != '160000 '* ]]; then
        printf 'Missing bgfx submodule gitlink: %s. Registering bgfx dependencies first...\n' "$path"
        ./git_submodule_update.sh --bgfx
        break
    fi
done

# Sync URLs from .gitmodules and check out exactly the revisions pinned by
# the superproject; do not silently advance existing submodules to --remote.
git submodule sync --recursive
git submodule update --init --recursive -- \
    external/GeographicLib \
    external/AOWIS-SERVER-MODEL \
    external/AOWIS-SERVER-EPANET
# Never recursive: AOWIS needs none of bgfx's own submodules. --depth 1 only
# affects new clones; existing full clones are left as they are.
git submodule update --init --depth 1 -- "${bgfx_paths[@]}"
tools/bgfx_sparse_checkout.sh ${sparse_option}
