#ifndef MAP_EDITOR_RENDERER_H
#define MAP_EDITOR_RENDERER_H

#include "map_editor_visual_state.h"
#include "map_entity_pixmap_renderer.h"

#include "../network_render_snapshot.h"

#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSize>

#include <atomic>
#include <memory>

class MapModel;
class QPaintEvent;
class QPainter;
class QWidget;

class MapEditorRenderer : public QObject
{
public:
    explicit MapEditorRenderer(MapModel *map_model, QWidget *canvas);
    ~MapEditorRenderer() override;

    void paint(QPainter &painter, const QPaintEvent &event,
               const NetworkRenderSnapshot &network_snapshot,
               const MapEditorVisualState &visual_state,
               const MapEditorViewportRenderState &viewport_state);
    void paintRhiOverlay(QPainter &painter,
                         const NetworkRenderSnapshot &network_snapshot,
                         const MapEditorVisualState &visual_state,
                         const MapEditorViewportRenderState &viewport_state);
    void setRenderingActive(bool active);
    void setRhiOverlayMode(bool enabled);
    void setRhiFullNetworkMoveState(bool active, const QPointF &translation_pixels);

private:
    struct StaticNode
    {
        QUuid uuid;
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        QPointF world_position;
    };

    struct StaticLink
    {
        QUuid uuid;
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        QList<QPointF> world_vertices;
        QPointF device_center_world_position;
    };

    struct StaticGeometry
    {
        quint64 geometry_revision = 0;
        QList<StaticNode> nodes;
        QList<StaticLink> links;
        QHash<QUuid, int> node_indices_by_uuid;
        QHash<QUuid, int> link_indices_by_uuid;
        QRectF world_bounds;
        QPointF world_origin;
    };

    struct StaticRenderRequest
    {
        quint64 request_id = 0;
        quint64 geometry_revision = 0;
        int zoom = -1;
        int entity_width = 10;
        qreal device_pixel_ratio = 1.0;
        QSize logical_size;
        QRectF coverage_world_bounds;
        std::shared_ptr<const StaticGeometry> geometry;
        QHash<int, QImage> entity_images;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    struct StaticRenderResult
    {
        quint64 request_id = 0;
        quint64 geometry_revision = 0;
        int zoom = -1;
        int entity_width = 10;
        qreal device_pixel_ratio = 1.0;
        QRectF coverage_world_bounds;
        QImage image;
    };

    void prepareProjection(double wrap_reference_longitude);
    QPointF screenFromWgs84(const CoordinateWGS84 &coordinate,
                            double wrap_reference_longitude) const;
    QPointF screenFromReferenceWorld(const QPointF &world_position) const;
    QPointF visibleReferenceWorldCenter() const;
    QRectF visibleReferenceWorldRect() const;
    qreal referenceScaleForCurrentZoom() const;
    const NetworkRenderNode *nodeByUuid(
        const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid,
        const QUuid &uuid) const;
    CoordinateWGS84 deviceLinkCenterCoordinate(const NetworkRenderLink &link) const;

    void syncStaticGeometry(const NetworkRenderSnapshot &network_snapshot);
    void startStaticGeometryBuild();
    static std::shared_ptr<StaticGeometry> buildStaticGeometry(
        const NetworkRenderSnapshot &network_snapshot,
        const std::shared_ptr<std::atomic_bool> &cancelled);
    void applyStaticGeometryBuild(quint64 request_id,
                                  std::shared_ptr<StaticGeometry> geometry);
    void clearStaticRenderedCache();
    void requestStaticCache(int entity_width, bool force = false);
    StaticRenderRequest createStaticRenderRequest(quint64 request_id,
                                                   int entity_width) const;
    static StaticRenderResult renderStaticCache(const StaticRenderRequest &request);
    void applyStaticRenderResult(StaticRenderResult result);
    bool renderedStaticCacheCoversCurrentView(bool include_rebuild_margin) const;
    bool pendingStaticCacheCoversCurrentView() const;
    bool coverageCoversCurrentView(const QRectF &coverage_world_bounds,
                                   int zoom, bool include_rebuild_margin) const;
    bool paintStaticCache(QPainter &painter, const MapEditorVisualState &visual_state);
    QPainterPath moveStaticVisibleClipPath(const MapEditorVisualState &visual_state);

    void paintBackground(QPainter &painter,
                         const MapEditorViewportRenderState &viewport_state) const;
    void paintTileSelection(QPainter &painter,
                            const MapEditorViewportRenderState &viewport_state) const;
    void paintRectangleSelection(QPainter &painter,
                                 const MapEditorViewportRenderState &viewport_state) const;
    void paintNetwork(QPainter &painter,
                      const NetworkRenderSnapshot &network_snapshot,
                      const MapEditorVisualState &visual_state);
    void paintRhiStaticDetails(QPainter &painter,
                               const NetworkRenderSnapshot &network_snapshot,
                               const MapEditorVisualState &visual_state,
                               bool include_moving_entities = false);
    void paintDirectNetwork(QPainter &painter,
                            const NetworkRenderSnapshot &network_snapshot,
                            const MapEditorVisualState &visual_state);
    void paintInteractiveNetwork(QPainter &painter,
                                 const NetworkRenderSnapshot &network_snapshot,
                                 const MapEditorVisualState &visual_state);
    void paintMovingNetwork(QPainter &painter,
                            const MapEditorVisualState &visual_state);
    void paintSelectedPipes(QPainter &painter,
                            const MapEditorVisualState &visual_state) const;
    void paintSelectedMarkersAndDeviceLinks(QPainter &painter,
                                            const MapEditorVisualState &visual_state);
    void paintSimulationError(QPainter &painter, const MapEditorVisualState &visual_state);
    void paintPipePlacement(QPainter &painter,
                            const MapEditorVisualState &visual_state,
                            const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid) const;
    void paintDeviceLinkPlacement(
        QPainter &painter,
        const MapEditorVisualState &visual_state,
        const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid);
    void paintPipes(QPainter &painter,
                    const NetworkRenderSnapshot &network_snapshot,
                    const MapEditorVisualState &visual_state,
                    const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid) const;
    void paintDeviceLinks(
        QPainter &painter,
        const NetworkRenderSnapshot &network_snapshot,
        const MapEditorVisualState &visual_state,
        const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid);
    void paintMarkers(QPainter &painter,
                      const NetworkRenderSnapshot &network_snapshot,
                      const MapEditorVisualState &visual_state);
    void paintPlacement(
        QPainter &painter,
        const MapEditorVisualState &visual_state,
        const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid);

    MapModel *map_model = nullptr;
    QWidget *canvas = nullptr;
    MapEntityPixmapRenderer pixmap_renderer;
    QPointF projection_center_tile;
    QSize projection_viewport_size;
    double projection_reference_base_tile_x = 0.0;
    double projection_reference_tile_x = 0.0;
    int projection_zoom = -1;
    bool projection_ready = false;

    bool rendering_active = true;
    bool rhi_overlay_mode = false;
    bool rhi_full_network_move_active = false;
    QPointF rhi_full_network_move_translation;
    quint64 current_geometry_revision = 0;
    int current_entity_width = 10;
    std::shared_ptr<const NetworkRenderSnapshot> pending_geometry_snapshot;
    std::shared_ptr<const StaticGeometry> static_geometry;
    quint64 next_geometry_request_id = 0;
    quint64 active_geometry_request_id = 0;
    std::shared_ptr<std::atomic_bool> geometry_build_cancelled;
    bool geometry_build_running = false;
    bool geometry_build_restart_requested = false;

    QImage rendered_static_cache;
    QRectF rendered_static_cache_coverage_world_bounds;
    quint64 rendered_static_cache_geometry_revision = 0;
    int rendered_static_cache_zoom = -1;
    int rendered_static_cache_entity_width = 10;
    qreal rendered_static_cache_device_pixel_ratio = 0.0;

    quint64 next_render_request_id = 0;
    quint64 active_render_request_id = 0;
    quint64 pending_render_request_id = 0;
    QRectF pending_static_cache_coverage_world_bounds;
    int pending_static_cache_zoom = -1;
    int pending_static_cache_entity_width = 10;
    qreal pending_static_cache_device_pixel_ratio = 0.0;
    std::shared_ptr<std::atomic_bool> pending_render_cancelled;
    bool render_worker_running = false;
    bool render_restart_requested = false;
    bool render_restart_force = false;

    QPainterPath move_static_visible_clip_path;
    quint64 move_static_clip_session_id = 0;
    quint64 move_static_clip_geometry_revision = 0;
    int move_static_clip_zoom = -1;
    QPointF move_static_clip_center_tile;
    QSize move_static_clip_viewport_size;
    int move_static_clip_entity_width = 0;
};

#endif // MAP_EDITOR_RENDERER_H
