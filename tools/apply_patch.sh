#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf 'Usage: %s /path/to/patch.zip\n' "${0##*/}" >&2
    exit 2
fi

# AOWIS-SERVER-GUI accepts its own patches only, named aowis-server-gui-*.zip. Check
# the archive's basename before resolving or opening it, so a patch for another
# AOWIS project, or a renamed or misplaced one, is rejected without changing
# the worktree.
patch_name="${1##*/}"
case "$patch_name" in
    aowis-server-gui-?*.zip) ;;
    *)
        printf 'Error: patch ZIP filename must start with aowis-server-gui- and end with .zip: %s\n' "$patch_name" >&2
        exit 2
        ;;
esac

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
project_root="$(cd -- "$script_dir/.." && pwd -P)"
parent="$(dirname -- "$project_root")"
project_name="$(basename -- "$project_root")"
patch="$(realpath -- "$1")"

# Inspect the exact patch contents before changing anything. Directory entries do
# not count as upgraded files. The MODEL and EPANET sources live inside the GUI
# checkout, but have separate snapshot ZIPs and must not trigger gui.zip.
if ! unzip -tq "$patch" >/dev/null; then
    printf 'Error: invalid patch ZIP: %s\n' "$patch" >&2
    exit 1
fi

refresh_gui=false
refresh_model=false
refresh_epanet=false
while IFS= read -r entry; do
    if [[ "$entry" != "$project_name/"* || "$entry" == *'/../'* ||
          "$entry" == *'/./'* || "$entry" == *'\\'* ]]; then
        printf 'Invalid patch path: %s\n' "$entry" >&2
        exit 1
    fi

    [[ "$entry" == */ ]] && continue
    case "$entry" in
        "$project_name/external/AOWIS-SERVER-MODEL/"*) refresh_model=true ;;
        "$project_name/external/AOWIS-SERVER-EPANET/"*) refresh_epanet=true ;;
        *) refresh_gui=true ;;
    esac
done < <(unzip -Z -1 "$patch")

unzip -q -o "$patch" -d "$parent"

# ZIP timestamps reflect archive creation, not application. Refresh both
# sources and directories here so Ninja sees changes when applying a patch.
while IFS= read -r entry; do
    touch -c -m -- "$parent/$entry"
done < <(unzip -Z -1 "$patch")

printf 'Applied %s with current file and directory timestamps.\n' "$patch"

# Refresh the snapshots affected by the patch. Run the scripts from the updated
# checkout (not from the ZIP) so changes to the scripts themselves take effect.
# Once extraction succeeds, snapshot failures are reported as failures without
# implying the already-applied source files were rolled back.
snapshot_failed=false
refresh_snapshot() {
    local archive_name="$1"
    local script_name="$2"
    printf 'Refreshing %s...\n' "$archive_name"
    if ! bash "$project_root/tools/$script_name"; then
        printf 'Error: failed to refresh %s; patch files are already applied.\n' \
            "$archive_name" >&2
        snapshot_failed=true
    fi
}

if "$refresh_gui"; then
    refresh_snapshot gui.zip zip_gui_project.sh
fi
if "$refresh_model"; then
    refresh_snapshot model.zip zip_model_project.sh
fi
if "$refresh_epanet"; then
    refresh_snapshot epanet.zip zip_epanet_project.sh
fi

if "$snapshot_failed"; then
    exit 1
fi
