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
project_name="AOWIS-SERVER-EPANET"
project_dir="$project_parent/$project_name"
archive_path="$gui_parent/epanet.zip"

if [[ ! -d "$project_dir" ]]; then
    echo "Error: project directory not found: $project_dir" >&2
    exit 1
fi

rm -f -- "$archive_path"
cd -- "$project_parent"

find "$project_name" \
    \( -type d \( \
        -name "assets" -o \
        -name "build*" -o \
        -name ".git" -o \
        -name ".idea" -o \
        -name ".vscode" -o \
        -name ".vs" -o \
        -name ".qtcreator" -o \
        -name ".cache" -o \
        -name "CMakeFiles" -o \
        -name "Testing" -o \
        -name "_deps" -o \
        -name "__pycache__" -o \
        -name "*.dSYM" \
    \) \) -prune -o \
    \( -type f \( \
        -name ".git" -o \
        -name "*.user" -o \
        -name "*.user.*" -o \
        -name "CMakeCache.txt" -o \
        -name "cmake_install.cmake" -o \
        -name "install_manifest.txt" -o \
        -name "compile_commands.json" -o \
        -name "build.ninja" -o \
        -name "rules.ninja" -o \
        -name ".ninja_deps" -o \
        -name ".ninja_log" -o \
        -name "*.o" -o \
        -name "*.obj" -o \
        -name "*.a" -o \
        -name "*.so" -o \
        -name "*.so.*" -o \
        -name "*.dll" -o \
        -name "*.dylib" -o \
        -name "*.exe" -o \
        -name "*.pdb" -o \
        -name "*.ilk" -o \
        -name "*.wasm" -o \
        -name "*.log" -o \
        -name "core" -o \
        -name "core.*" -o \
        -name "*.tmp" -o \
        -name "*.temp" -o \
        -name "*.swp" -o \
        -name "*.swo" -o \
        -name "*~" -o \
        -name ".DS_Store" -o \
        -name "Thumbs.db" \
    \) \) -prune -o \
    -print |
    zip -q -y "$archive_path" -@

echo "Created: $archive_path"
