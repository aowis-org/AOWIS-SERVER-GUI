#!/bin/sh
# Removes the QPainter map editor renderer (replaced by the bgfx editor),
# then deletes this script. Run from the AOWIS-SERVER-GUI directory.
cd "$(dirname "$0")" || exit 1
rm -f src/map/editor/map_editor_renderer.h \
      src/map/editor/map_editor_renderer.cpp
echo "Removed src/map/editor/map_editor_renderer.{h,cpp}"
rm -f -- "$0"
