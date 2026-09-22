#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
script_path="${script_dir}/$(basename -- "${BASH_SOURCE[0]}")"

cd "$script_dir"
rm -f \
    src/map/rhi/map_rhi_junction_model.h \
    src/map/rhi/map_rhi_junction_model.cpp \
    src/map/rhi/map_rhi_tank_model.h \
    src/map/rhi/map_rhi_tank_model.cpp \
    src/map/rhi/map_rhi_reservoir_model.h \
    src/map/rhi/map_rhi_reservoir_model.cpp

rm -f -- "$script_path"
