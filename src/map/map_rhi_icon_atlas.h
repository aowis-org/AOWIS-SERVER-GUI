#ifndef MAP_RHI_ICON_ATLAS_H
#define MAP_RHI_ICON_ATLAS_H

#include "../_enums_structs.h"

#include <QImage>
#include <QRectF>

struct MapRhiIconAtlasEntry
{
    bool valid = false;
    QRectF uv_rect;
    qreal width_ratio = 1.0;
    qreal height_ratio = 1.0;
};

QImage mapRhiIconAtlasImage();
MapRhiIconAtlasEntry mapRhiIconAtlasEntry(InfrastructureEntity entity_type);
bool mapRhiHasIcon(InfrastructureEntity entity_type);

#endif // MAP_RHI_ICON_ATLAS_H
