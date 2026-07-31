#!/usr/bin/env bash
set -euo pipefail

if ! command -v zip >/dev/null 2>&1; then
    echo "Error: 'zip' is not installed." >&2
    exit 1
fi

tools_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
gui_dir="$(cd -- "$tools_dir/.." && pwd)"
gui_parent="$(dirname -- "$gui_dir")"
project_parent="$gui_dir/external"
project_name="AOWIS-SERVER-MODEL"
project_dir="$project_parent/$project_name"
archive_path="$gui_parent/model.zip"

if [[ ! -d "$project_dir" ]]; then
    echo "Error: project directory not found: $project_dir" >&2
    exit 1
fi

rm -f -- "$archive_path"

cd -- "$project_parent"

find "$project_name" \
    -type d \( -name "assets" -o -name "build*" \) -prune \
    -o -print |
    zip -q -y "$archive_path" -@

echo "Created: $archive_path"
