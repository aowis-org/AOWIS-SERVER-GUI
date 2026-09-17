#ifndef MAP_RHI_ICON_ATLAS_H
#define MAP_RHI_ICON_ATLAS_H

#include "map/render/map_icon_atlas.h"

// Compatibility names for existing QRhi/2D call sites. The atlas layout and
// image generation themselves are backend-neutral and live under map/render.
using MapRhiIconAtlasEntry = MapIconAtlasEntry;

QImage mapRhiIconAtlasImage();
MapRhiIconAtlasEntry mapRhiIconAtlasEntry(InfrastructureEntity entity_type);
bool mapRhiHasIcon(InfrastructureEntity entity_type);

#endif // MAP_RHI_ICON_ATLAS_H
