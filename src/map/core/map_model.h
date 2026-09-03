#ifndef MAP_MODEL_H
#define MAP_MODEL_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>

#include "common/_enums_structs.h"
#include "geo/geo_metric_projection.h"
#include "geo/geo_web_mercator.h"

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
    static constexpr double MinView3dCameraGroundClearanceM = 2.0;
    static constexpr double MinView3dNetworkGroundOffsetM = 0.0;
    static constexpr double MaxView3dNetworkGroundOffsetM = 50.0;
    static constexpr double MinView3dVerticalExaggeration = 0.5;
    static constexpr double MaxView3dVerticalExaggeration = 5.0;
    static constexpr double DefaultView3dVerticalExaggeration = 1.0;
    static constexpr double View3dOrbitYawDegreesPerPixel = 0.35;
    static constexpr double View3dOrbitPitchDegreesPerPixel = 0.25;
    // "Globe" view mode: orbit camera around the WGS84 ellipsoid, target is
    // always the ordinary map center (centerLon()/centerLat()), so the globe
    // opens centered on wherever 2D/3D already is -- no separate target
    // state needed. Distance is in meters (unlike the 2D/3D fields above,
    // which are in tile pixels/world units); defaults to roughly 3x Earth's
    // radius so the whole planet is comfortably in view on entry.
    static constexpr double MinViewGlobeDistanceM = 500000.0;
    static constexpr double MaxViewGlobeDistanceM = 60000000.0;
    static constexpr double DefaultViewGlobeDistanceM = 20000000.0;
    static constexpr double MinViewGlobePitchDeg = 5.0;
    static constexpr double MaxViewGlobePitchDeg = 90.0;
    static constexpr double DefaultViewGlobePitchDeg = 55.0;
    static constexpr double ViewGlobeOrbitYawDegreesPerPixel = 0.2;
    static constexpr double ViewGlobeOrbitPitchDegreesPerPixel = 0.2;

    int zoom() const;
    double view2dContinuousScale() const;
    double view2dContinuousZoom() const;
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
    double view3dNetworkGroundOffsetM() const;
    double view3dVerticalExaggeration() const;
    MapView3dNavigationState view3dNavigationState() const;
    double viewGlobeYawDeg() const;
    double viewGlobePitchDeg() const;
    double viewGlobeDistanceM() const;
    QString tileCacheKey(int x, int y) const;
    QString tileCachePrefix(int zoom) const;
    QString tileEndpoint(int x, int y) const;
    QString tileSourcePath(int zoom) const;
    // Zoom-independent siblings of tileCacheKey()/tileEndpoint(): those two
    // always resolve against the current 2D/3D zoom (zoom()), which is not
    // useful for the globe view's own fixed, independent imagery zoom level.
    QString tileCacheKeyAtZoom(int x, int y, int zoom) const;
    QString tileEndpointAtZoom(int x, int y, int zoom) const;

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
    void fitViewToBounds(const CoordinateWGS84 &minimum, const CoordinateWGS84 &maximum,
                         const QSize &viewport, double elevation_minimum_m,
                         double elevation_maximum_m, bool allow_continuous_2d_zoom);
    void setCenter(double lon, double lat, const QSize &viewport = QSize());
    void setZoom(int zoom, const QSize &viewport = QSize());
    void zoomIn(const QSize &viewport = QSize());
    void zoomOut(const QSize &viewport = QSize());
    void setView2dContinuousZoom(double continuous_zoom, const QSize &viewport = QSize());
    void resetView2dContinuousZoom(const QSize &viewport = QSize());

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
    void setView3dNetworkGroundOffsetM(double offset_m);
    void setView3dVerticalExaggeration(double exaggeration);
    void setView3dFocusAnchor(double lon, double lat, double offset_world,
                              double distance_m, const QSize &viewport = QSize());
    void beginView3dRotateInteraction();
    void endView3dRotateInteraction();
    void orbitView3d(double yaw_delta_deg, double pitch_delta_deg);
    void orbitView3dByPointerDelta(const QPoint &delta_pixels, bool include_pitch);
    void resetView3dCamera();

    void setViewGlobeYawDeg(double yaw_deg);
    void setViewGlobePitchDeg(double pitch_deg);
    void setViewGlobeDistanceM(double distance_m);
    void orbitViewGlobe(double yaw_delta_deg, double pitch_delta_deg);
    void orbitViewGlobeByPointerDelta(const QPoint &delta_pixels, bool include_pitch);

signals:
    void zoomChanged(int zoom);
    void centerChangedWGS84(CoordinateWGS84 wgs);
    void centerChangedUTM(CoordinateUTM utm);
    void providerChanged(MapProvider provider);
    void viewModeChanged(MapViewMode view_mode);
    void view2dContinuousScaleChanged(double scale);
    void view3dCameraChanged();
    void view3dNavigationStateChanged(MapView3dNavigationState state);
    void view3dNetworkGroundOffsetChanged(double offset_m);
    void viewGlobeCameraChanged();

private:
    void clampCenter(const QSize &viewport);
    void emitCenterChanged();
    QPointF groundOffsetFromScreen3d(const QPointF &position, const QSize &viewport) const;
    QPointF screenFromTileOffset3d(const QPointF &offset_pixels, const QSize &viewport) const;
    QString providerPath() const;

    int m_zoom = 18;
    double m_view_2d_continuous_scale = 1.0;
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
    double m_view_3d_network_ground_offset_m = 0.0;
    double m_view_3d_vertical_exaggeration = DefaultView3dVerticalExaggeration;
    bool m_view_3d_native_camera_distance_initialized = false;
    bool m_view_3d_preserve_camera_distance_on_next_native_sync = false;
    MapView3dNavigationState m_view_3d_navigation_state = MapView3dNavigationState::Pan;
    int m_view_3d_rotate_interaction_depth = 0;

    double m_view_globe_yaw_deg = 0.0;
    double m_view_globe_pitch_deg = DefaultViewGlobePitchDeg;
    double m_view_globe_distance_m = DefaultViewGlobeDistanceM;
};

#endif // MAP_MODEL_H
