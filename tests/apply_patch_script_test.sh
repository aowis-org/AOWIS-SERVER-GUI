#!/usr/bin/env bash
set -euo pipefail

# Run independently of Qt/CMake: bash tests/apply_patch_script_test.sh
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source_root="$(cd -- "$script_dir/.." && pwd -P)"
fixture="$(mktemp -d)"
trap 'rm -rf -- "$fixture"' EXIT

project_dir="$fixture/AOWIS-SERVER-GUI"
mkdir -p -- "$project_dir/tools" "$project_dir/external/AOWIS-SERVER-MODEL" \
    "$project_dir/external/AOWIS-SERVER-EPANET"
cp -- "$source_root/tools/apply_patch.sh" \
    "$source_root/tools/zip_gui_project.sh" \
    "$source_root/tools/zip_model_project.sh" \
    "$source_root/tools/zip_epanet_project.sh" "$project_dir/tools/"
printf 'baseline\n' > "$project_dir/README.md"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

make_patch() {
    local label="$1"
    shift
    local stage="$fixture/stage-$label"
    local archive="$fixture/aowis-server-gui-$label.zip"
    local path
    mkdir -p -- "$stage"
    for path in "$@"; do
        mkdir -p -- "$stage/$(dirname -- "$path")"
        printf '%s\n' "$label" > "$stage/$path"
    done
    (cd -- "$stage" && zip -qr "$archive" AOWIS-SERVER-GUI)
    printf '%s\n' "$archive"
}

check_archives() {
    local expected="$1"
    local actual=''
    local archive
    for archive in gui model epanet; do
        if [[ -e "$fixture/$archive.zip" ]]; then
            [[ -z "$actual" ]] || actual+=','
            actual+="$archive"
        fi
    done
    [[ "$actual" == "$expected" ]] || fail "expected [$expected], found [$actual]"
}

clear_archives() {
    rm -f -- "$fixture/gui.zip" "$fixture/model.zip" "$fixture/epanet.zip"
}

# GUI-only patch refreshes only gui.zip, and contents reflect the applied patch.
gui_file='AOWIS-SERVER-GUI/src/gui/file.cpp'
gui_patch="$(make_patch gui-only "$gui_file")"
touch -t 202001010000 -- "$fixture/stage-gui-only/$gui_file"
rm -f -- "$gui_patch"
(cd -- "$fixture/stage-gui-only" && zip -qr "$gui_patch" AOWIS-SERVER-GUI)
bash "$project_dir/tools/apply_patch.sh" "$gui_patch" >/dev/null
check_archives 'gui'
unzip -Z -1 "$fixture/gui.zip" | grep -Fqx "$gui_file" || fail 'gui.zip missing GUI file'
[[ "$(stat -c %Y "$fixture/$gui_file")" -gt 1577836800 ]] || fail 'file timestamp not updated'

# Submodule payloads only refresh the matching snapshots.
clear_archives
model_file='AOWIS-SERVER-GUI/external/AOWIS-SERVER-MODEL/src/model.cpp'
model_patch="$(make_patch model-only "$model_file")"
bash "$project_dir/tools/apply_patch.sh" "$model_patch" >/dev/null
check_archives 'model'
unzip -Z -1 "$fixture/model.zip" | grep -Fqx 'AOWIS-SERVER-MODEL/src/model.cpp' || fail 'model.zip missing model file'

clear_archives
epanet_file='AOWIS-SERVER-GUI/external/AOWIS-SERVER-EPANET/src/epanet.cpp'
epanet_patch="$(make_patch epanet-only "$epanet_file")"
bash "$project_dir/tools/apply_patch.sh" "$epanet_patch" >/dev/null
check_archives 'epanet'
unzip -Z -1 "$fixture/epanet.zip" | grep -Fqx 'AOWIS-SERVER-EPANET/src/epanet.cpp' || fail 'epanet.zip missing EPANET file'

# A mixed patch runs all three, once each.
clear_archives
mixed_patch="$(make_patch mixed "$gui_file" "$model_file" "$epanet_file")"
mixed_log="$fixture/mixed.log"
bash "$project_dir/tools/apply_patch.sh" "$mixed_patch" > "$mixed_log"
check_archives 'gui,model,epanet'
for archive in gui model epanet; do
    [[ "$(grep -Fc "Refreshing $archive.zip..." "$mixed_log")" == 1 ]] || \
        fail "$archive.zip was not refreshed exactly once"
done

# ZIPs with only directory entries do not count as source changes.
clear_archives
mkdir -p -- "$fixture/empty-stage/AOWIS-SERVER-GUI/new-empty-folder"
(cd -- "$fixture/empty-stage" && zip -qr "$fixture/aowis-server-gui-directories.zip" AOWIS-SERVER-GUI)
bash "$project_dir/tools/apply_patch.sh" "$fixture/aowis-server-gui-directories.zip" >/dev/null
check_archives ''

# A failed snapshot leaves files applied; independent refreshes still finish.
printf '#!/usr/bin/env bash\nexit 19\n' > "$project_dir/tools/zip_model_project.sh"
clear_archives
failure_patch="$(make_patch snapshot-failure "$model_file" "$epanet_file")"
if bash "$project_dir/tools/apply_patch.sh" "$failure_patch" > "$fixture/failure.log" 2>&1; then
    fail 'expected a nonzero exit code for failed snapshot generation'
fi
[[ -f "$fixture/$model_file" ]] || fail 'patch should have been applied despite snapshot failure'
check_archives 'epanet'
grep -Fq 'patch files are already applied' "$fixture/failure.log" || \
    fail 'failure must say that patch files were already applied'

printf 'All apply_patch.sh snapshot-routing checks passed.\n'
