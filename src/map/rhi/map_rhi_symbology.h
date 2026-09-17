#ifndef MAP_RHI_SYMBOLOGY_H
#define MAP_RHI_SYMBOLOGY_H

#include "map/render/map_network_render_symbology.h"
#include "network/network_symbology.h"

class HydraulicData;

// Compatibility name for current QRhi call sites. The resolved symbology
// itself is renderer-independent and is also consumed by MapGlobeNetworkScene.
using MapRhiSymbology = MapNetworkRenderSymbology;

MapRhiSymbology resolveMapRhiSymbology(
    const HydraulicData &hydraulic_data,
    const NetworkSymbologySettings &settings,
    const NetworkSymbologyRanges &ranges);

#endif // MAP_RHI_SYMBOLOGY_H
