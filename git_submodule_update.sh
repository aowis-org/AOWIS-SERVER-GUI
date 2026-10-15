#!/usr/bin/env bash
set -euo pipefail

# Resolve paths relative to this script, not the caller's working directory.
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

usage() {
    cat <<'HELP'
Usage: ./git_submodule_update.sh [--bgfx | --all | --help]

  (no option)  Update existing AOWIS and GeographicLib submodules from
               their tracked remote branches (original behavior).
  --bgfx       Initialize/update bgfx, bx and bimg to their upstream master
               (shallow, sparse; see tools/bgfx_sparse_checkout.sh).
               Does not touch the AOWIS and GeographicLib submodules.
               Afterwards regenerate the committed bgfx shaders with
               ./compile_linux.sh --shader-tools and commit them together
               with the new gitlinks: shader binaries must match bgfx.
  --all        Update both groups.
  --help       Show this help.

On the first --bgfx run, any missing bgfx/bx/bimg Git submodules are
registered via 'git submodule add'. Commit the resulting .gitmodules and
submodule gitlinks together so other clones receive their pinned versions.
HELP
}

standard_paths=(
    external/GeographicLib
    external/AOWIS-SERVER-EPANET
    external/AOWIS-SERVER-MODEL
)

bgfx_paths=(
    external/bx
    external/bimg
    external/bgfx
)

bgfx_urls=(
    https://github.com/bkaradzic/bx.git
    https://github.com/bkaradzic/bimg.git
    https://github.com/bkaradzic/bgfx.git
)

# A .gitmodules entry by itself does not register a submodule: the index also
# needs a gitlink (mode 160000). The source ZIP cannot carry Git index entries,
# so bootstrap them on the first --bgfx invocation in the user's actual clone.
ensure_bgfx_submodules_registered() {
    local i path url tracked
    for i in "${!bgfx_paths[@]}"; do
        path="${bgfx_paths[$i]}"
        url="${bgfx_urls[$i]}"
        tracked="$(git ls-files --stage -- "$path")"

        if [[ "$tracked" == '160000 '* ]]; then
            continue
        fi

        if [[ -e "$path" ]] && [[ -n "$(ls -A -- "$path")" ]]; then
            printf 'Cannot register %s: path exists and is not a tracked submodule.\n' "$path" >&2
            return 1
        fi

        printf 'Registering %s as a Git submodule...\n' "$path"
        git submodule add --depth 1 -- "$url" "$path"
        git config -f .gitmodules "submodule.$path.shallow" true
    done
}

update_standard() {
    git submodule update --init --recursive --remote -- "${standard_paths[@]}"
}

update_bgfx() {
    ensure_bgfx_submodules_registered
    # Never recursive: AOWIS needs none of bgfx's own submodules.
    git submodule update --init --remote --depth 1 -- "${bgfx_paths[@]}"
    tools/bgfx_sparse_checkout.sh
    printf 'bgfx updated. Regenerate the shader binaries: ./compile_linux.sh --shader-tools\n'
}

case "${1:-}" in
    '')
        [[ $# -eq 0 ]] || { usage >&2; exit 2; }
        update_standard
        ;;
    --bgfx)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        update_bgfx
        ;;
    --all)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        update_standard
        update_bgfx
        ;;
    --help|-h)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        usage
        ;;
    *)
        printf 'Unknown option: %s\n' "$1" >&2
        usage >&2
        exit 2
        ;;
esac
