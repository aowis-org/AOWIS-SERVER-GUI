#ifndef MAP_RHI_CAMERA_H
#define MAP_RHI_CAMERA_H

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>

#include "../_enums_structs.h"

class MapModel;
class QRhi;

class MapRhiCamera
{
public:
    MapRhiCamera();

    void setSceneOriginWorld(const QPointF &origin_world);
    void setViewportSize(const QSize &viewport_size);
    void syncFromMapModel(const MapModel &map_model);

    QMatrix4x4 viewProjectionMatrix(const QRhi &rhi) const;

private:
    QPointF scene_origin_world;
    QPointF center_world;
    QSize viewport_size;
    int zoom = 0;
    MapViewMode view_mode = MapViewMode::TwoD;
};

#endif // MAP_RHI_CAMERA_H
