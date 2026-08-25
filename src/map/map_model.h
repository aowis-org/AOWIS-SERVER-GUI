#ifndef MAP_MODEL_H
#define MAP_MODEL_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>

#include "../_enums_structs.h"
#include "../geo_metric_projection.h"
#include "../geo_web_mercator.h"

enum class MapView3dNavigationState
{
    Pan,
    Rotate
};

class MapModel : public QObject
{
    Q_OBJECT

public:
    explicit MapModel(QObject *parent = nullptr);

    static constexpr int TileSize = GeoWebMercator::TileSize;
    static constexpr int MinZoom = 1;
    static constexpr int MaxZoom = 19;
    static constexpr double MinView3dPitchDeg = 0.0;
    static constexpr double MaxView3dPitchDeg = 90.0;
    static constexpr double DefaultView3dPitchDeg = 55.0;
    static constexpr double MinView3dCameraDistanceM = 150.0;
    static constexpr double MaxView3dCameraDistanceAboveDefaultM = 500.0;
    static constexpr double MinView3dCameraGroundClearanceM = 2.0;

    int zoom() const;
    double centerLon() const;
    double centerLat() const;

    MapProvider provider() const;
    MapViewMode viewMode() const;
    double view3dYawDeg() const;
    double view3dPitchDeg() const;
    double view3dCameraDistanceM() const;
    double view3dNativeCameraDistanceM() const;
    double view3dMaximumCameraDistanceM() const;
    double view3dCameraDistanceWorld() const;
    double view3dCameraCollisionLiftWorld() const;
    double view3dVerticalOffsetWorld() const;
    MapView3dNavigationState view3dNavigationState() const;
    QString tileCacheKey(int x, int y) const;
    QString tileCachePrefix(int zoom) const;
    QString tileEndpoint(int x, int y) const;
    QString tileSourcePath(int zoom) const;

    int tileCount() const;

    QPointF centerTile() const;
    CoordinateWGS84 wgs84FromScreen(const QPoint &pos, const QSize &viewport) const;
    QPointF screenFromWgs84(const CoordinateWGS84 &coord, const QSize &viewport) const;
    QPointF screenFromWgs84(double lon, double lat, const QSize &viewport) const;
    QPointF screenFromWgs84(const CoordinateWGS84 &coord, const QSize &viewport,
                            double wrap_reference_lon) const;
    QPointF screenFromWgs84(double lon, double lat, const QSize &viewport,
                            double wrap_reference_lon) const;

    void setView(double lon, double lat, int zoom, const QSize &viewport = QSize());
    void setCenter(double lon, double lat, const QSize &viewport = QSize());
    void setZoom(int zoom, const QSize &viewport = QSize());
    void zoomIn(const QSize &viewport = QSize());
    void zoomOut(const QSize &viewport = QSize());

    void zoomByAt(int steps, const QPoint &anchorPos, const QSize &viewport);
    void panByPixels(const QPoint &delta, const QSize &viewport);
    void panByPixels3d(const QPoint &delta, const QSize &viewport);
    void panByPixels3dKeyboard(const QPoint &delta, const QSize &viewport);

    void setProvider(MapProvider provider);
    void setViewMode(MapViewMode view_mode);
    void setView3dYawDeg(double yaw_deg);
    void setView3dPitchDeg(double pitch_deg);
    void setView3dCameraDistanceM(double distance_m);
    void setView3dContinuousCameraDistanceM(double distance_m);
    void setView3dTileZoomPreservingCameraDistance(int zoom, const QSize &viewport = QSize());
    void syncView3dNativeCameraDistanceM(double distance_m);
    void setView3dCameraDistanceWorld(double distance_world);
    void setView3dCameraCollisionLiftWorld(double lift_world);
    void setView3dVerticalOffsetWorld(double offset_world);
    void setView3dFocusAnchor(double lon, double lat, double offset_world,
                              double distance_m, const QSize &viewport = QSize());
    void beginView3dRotateInteraction();
    void endView3dRotateInteraction();
    void orbitView3d(double yaw_delta_deg, double pitch_delta_deg);
    void resetView3dCamera();

signals:
    void zoomChanged(int zoom);
    void centerChangedWGS84(CoordinateWGS84 wgs);
    void centerChangedUTM(CoordinateUTM utm);
    void providerChanged(MapProvider provider);
    void viewModeChanged(MapViewMode view_mode);
    void view3dCameraChanged();
    void view3dNavigationStateChanged(MapView3dNavigationState state);

private:
    void clampCenter(const QSize &viewport);
    void emitCenterChanged();
    QPointF groundOffsetFromScreen3d(const QPointF &position, const QSize &viewport) const;
    QPointF screenFromTileOffset3d(const QPointF &offset_pixels, const QSize &viewport) const;
    QString providerPath() const;

    int m_zoom = 18;
    double m_centerLon = 18.19331;
    double m_centerLat = 11.98119;
    /*
    int m_zoom = 16;
    double m_centerLon = 18.2063;
    double m_centerLat = 11.9792;
    */

    MapProvider m_provider = MapProvider::ArcGISSat;
    MapViewMode m_view_mode = MapViewMode::TwoD;
    double m_view_3d_yaw_deg = 0.0;
    double m_view_3d_pitch_deg = DefaultView3dPitchDeg;
    double m_view_3d_camera_distance_m = MinView3dCameraDistanceM;
    double m_view_3d_native_camera_distance_m = MinView3dCameraDistanceM;
    double m_view_3d_extended_camera_distance_maximum_m = MinView3dCameraDistanceM;
    double m_view_3d_camera_distance_world = 0.0;
    double m_view_3d_camera_collision_lift_world = 0.0;
    double m_view_3d_vertical_offset_world = 0.0;
    bool m_view_3d_native_camera_distance_initialized = false;
    bool m_view_3d_preserve_camera_distance_on_next_native_sync = false;
    MapView3dNavigationState m_view_3d_navigation_state = MapView3dNavigationState::Pan;
    int m_view_3d_rotate_interaction_depth = 0;
};

#endif // MAP_MODEL_H
