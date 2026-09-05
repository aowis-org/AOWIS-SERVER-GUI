#!/usr/bin/env bash
#
# fix-future-timestamps.sh
#
# Finds every file under a directory whose mtime is in the future relative
# to this machine's real clock, and re-stamps it to the current time.
#
# Why this exists: a hardcoded `touch -d "YYYY-MM-DD HH:MM:SS"` timestamp
# that lands even slightly ahead of the build machine's real clock makes
# CMakeLists.txt (or any tracked source file) permanently "newer than"
# whatever build.ninja Ninja just regenerated. Ninja then reruns CMake every
# single invocation, which regenerates build.ninja with the *real* current
# time, which is still older than the future-dated file, forever. That's the
# `[0/1] Re-running CMake...` / `[0/2] Re-running CMake...` loop that never
# reaches an actual build step.
#
# Usage:
#   ./fix-future-timestamps.sh [directory] [-n|--dry-run]
#
#   directory   Where to look (default: current directory).
#   -n/--dry-run  List the offending files without touching them.
#
# Requires GNU find (the -newermt predicate). On Linux this is the default;
# on macOS with BSD find, install findutils (`brew install findutils`) and
# use `gfind` instead, or run this from WSL/a Linux container.

set -euo pipefail

TARGET_DIR="."
DRY_RUN=0

for arg in "$@"; do
    case "$arg" in
        -n|--dry-run)
            DRY_RUN=1
            ;;
        -h|--help)
            sed -n '2,26p' "$0"
            exit 0
            ;;
        *)
            TARGET_DIR="$arg"
            ;;
    esac
done

if [ ! -d "$TARGET_DIR" ]; then
    echo "error: '$TARGET_DIR' is not a directory" >&2
    exit 1
fi

# Skip VCS metadata and build output directories -- their timestamps don't
# matter for this problem and there's no reason to touch generated files.
mapfile -t FUTURE_FILES < <(
    find "$TARGET_DIR" \
        \( -path '*/.git' -o -name 'build*' \) -prune -o \
        -type f -newermt now -print
)

if [ "${#FUTURE_FILES[@]}" -eq 0 ]; then
    echo "No future-dated files found under '$TARGET_DIR'. Nothing to do."
    exit 0
fi

echo "Found ${#FUTURE_FILES[@]} file(s) with a timestamp ahead of this machine's clock:"
printf '  %s\n' "${FUTURE_FILES[@]}"

if [ "$DRY_RUN" -eq 1 ]; then
    echo "(dry run: nothing touched)"
    exit 0
fi

touch -- "${FUTURE_FILES[@]}"
echo "Re-stamped to the current time ($(date +"%Y-%m-%d %H:%M:%S"))."
echo "Re-run your build now (e.g. ./compile_linux.sh)."
