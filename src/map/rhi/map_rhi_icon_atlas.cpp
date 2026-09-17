#include "map/rhi/map_rhi_icon_atlas.h"

QImage mapRhiIconAtlasImage()
{
    return mapIconAtlasImage();
}

MapRhiIconAtlasEntry mapRhiIconAtlasEntry(InfrastructureEntity entity_type)
{
    return mapIconAtlasEntry(entity_type);
}

bool mapRhiHasIcon(InfrastructureEntity entity_type)
{
    return mapHasIcon(entity_type);
}
