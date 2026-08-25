#ifndef MAP_RHI_CAMERA_H
#define MAP_RHI_CAMERA_H

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector3D>

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
    QPointF projectWorldToScreen(const QVector3D &world_position) const;
    QPointF cameraGroundWorldPixel() const;
    QPointF cameraGroundWorldPixelForDistance(double distance_world) const;
    double nativeOrbitDistanceWorld() const;
    double orbitDistanceWorld() const;
    bool crosshairRay(QVector3D *eye_world, QVector3D *direction_world) const;

private:
    QPointF scene_origin_world;
    QPointF center_world;
    QSize viewport_size;
    int zoom = 0;
    double view_2d_continuous_scale = 1.0;
    MapViewMode view_mode = MapViewMode::TwoD;
    double view_3d_yaw_deg = 0.0;
    double view_3d_pitch_deg = 55.0;
    double view_3d_camera_distance_world = 0.0;
    double view_3d_camera_collision_lift_world = 0.0;
    double view_3d_vertical_offset_world = 0.0;
};

#endif // MAP_RHI_CAMERA_H
