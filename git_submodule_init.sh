#!/usr/bin/env bash
set -euo pipefail

# Initialize every pinned submodule revision, including bgfx, bx and bimg.
# Resolve paths relative to this script, not the caller's working directory.
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

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
git submodule update --init --recursive
