#!/bin/sh
# Post-migration cleanup (2026-10-17): deletes what a patch zip cannot.
# Run once from anywhere after extracting; the script deletes itself.
# Running it again is harmless (everything is already gone).
set -e
cd "$(dirname "$0")"
# The bgfx stacking probe (development tool of the migration).
rm -rf tests/bgfx_probe
rm -f tools/qt-emscripten/bgfx-probe.html tools/qt-emscripten/bgfx-probe-stack.js
# Unused map code: the QPainter banded CPU renderer, old QRhi render
# packets and helpers, and the renderer interface (now map_render_types.h).
rm -f src/map/render/map_retained_vector_renderer.cpp src/map/render/map_retained_vector_renderer.h \
      src/map/render/map_vector_document.cpp src/map/render/map_vector_document.h \
      src/map/render/map_network_scene_diagnostics.h \
      src/map/render/map_globe_junction_model.cpp src/map/render/map_globe_junction_model.h \
      src/map/render/map_globe_surface_render_frame.cpp src/map/render/map_globe_surface_render_frame.h \
      src/map/render/map_render_surface.h
# Stray EPANET server fragment and the unused simulation diagnostics dialog.
rm -rf src/lib
rm -f src/simulation/simulation_diagnostics_dialog.cpp src/simulation/simulation_diagnostics_dialog.h \
      src/simulation/simulation_diagnostic.h
echo "Cleanup files removed."
# After the cd above, "$0" may be a path relative to the old directory.
rm -f "$(basename "$0")"
