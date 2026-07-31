#!/usr/bin/env bash
set -euo pipefail

if ! command -v zip >/dev/null 2>&1; then
    echo "Error: 'zip' is not installed." >&2
    exit 1
fi

tools_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$tools_dir/.." && pwd)"
project_parent="$(dirname -- "$project_dir")"
project_name="$(basename -- "$project_dir")"
archive_path="$project_parent/gui.zip"

rm -f -- "$archive_path"

cd -- "$project_parent"

find "$project_name" \
    -type d \( -name "assets" -o -name "build*" \) -prune \
    -o -print |
    zip -q -y "$archive_path" -@

echo "Created: $archive_path"
