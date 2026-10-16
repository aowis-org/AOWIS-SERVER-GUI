#ifndef MAP_BGFX_LEGEND_CONTENT_H
#define MAP_BGFX_LEGEND_CONTENT_H

#include "map/bgfx/map_bgfx_hud.h"
#include "map/bgfx/map_bgfx_labels.h"
#include "network/network_symbology.h"

class HydraulicData;

// Content of the native bgfx map legend (slice 6b), computed exactly like
// EntityMapLegendHud (entity_map_legend_dock.cpp) does for its widgets:
//
// - the combo entries of node / link / heatmap metrics in the same order,
//   titled "Node · Pressure [m]" with the unit of the active unit profile;
//   the selected link entry uses the ramp's unit (km/mi for long lengths),
//   Roughness follows the network's head-loss formula;
// - per chosen metric the ramp range converted to the displayed unit
//   (MapLegendUnitPresentation::range()), the palette stops with the flip
//   applied, the palette selection and its default for Reset;
// - the palette menu's choices (Viridis ... Red/Blue, sequential first).
//
// ranges are the canonical symbology ranges of settings
// (HydraulicData::symbologyRanges()), which the caller already has from
// resolving the map symbology. Legend::visible is left false; the caller
// shows the legend while its surface is active.
MapBgfxHud::Legend mapBgfxLegendContent(const HydraulicData &hydraulic_data,
                                        const NetworkSymbologySettings &settings,
                                        const NetworkSymbologyRanges &ranges);

// Value labels (map labels): canonical metric value -> the number the
// legend ramp shows (same unit choice, km/mi for long lengths included).
// links: the link metric, otherwise the node metric. invalid without a
// metric.
MapBgfxLabelValueFormat mapBgfxLabelValueFormat(const HydraulicData &hydraulic_data,
                                                const NetworkSymbologySettings &settings,
                                                const NetworkSymbologyRanges &ranges,
                                                bool links);

#endif // MAP_BGFX_LEGEND_CONTENT_H
