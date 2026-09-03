#ifndef MAP_NETWORK_OVERLAY_WIDGET_H
#define MAP_NETWORK_OVERLAY_WIDGET_H

#include <QHash>
#include <QImage>
#include <QLineF>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSet>
#include <QWidget>
#include <QUuid>

#include <atomic>
#include <memory>

#include "common/_enums_structs.h"
#include "map/core/map_model.h"
#include "network/network_render_snapshot.h"
#include "network/network_symbology.h"

class HydraulicData;
class QColor;
class QEvent;
class QHideEvent;
class QPaintEvent;
class QPainter;
class QShowEvent;

struct NetworkOverlayHit
{
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    QUuid uuid;

    bool isValid() const
    {
        return this->render_id != 0 && this->entity_type != InfrastructureEntity::Unknown;
    }
};

class MapNetworkOverlayWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MapNetworkOverlayWidget(MapModel *map_model, HydraulicData *hydraulic_data, QWidget *parent = nullptr);
    ~MapNetworkOverlayWidget() override;

    int backgroundOpacity() const;
    NetworkOverlayHit hitTest(const QPointF &screen_position);
    void setSelectedEntity(const NetworkOverlayHit &hit);
    void setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid);
    void clearSelectedEntity();

public slots:
    void setBackgroundOpacity(int opacity);
    void setSymbology(const NetworkSymbologySettings &settings,
                      const NetworkSymbologyRanges &ranges);

protected:
    void changeEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    struct HitMarker
    {
        quint32 render_id = 0;
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        QUuid uuid;
        QPointF world_position;
    };

    struct HitSegment
    {
        quint32 render_id = 0;
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        QUuid uuid;
        QPointF start;
        QPointF end;
    };

    struct SpatialCell
    {
        QList<int> marker_indices;
        QList<int> device_segment_indices;
        QList<int> pipe_segment_indices;
    };

    struct RenderGeometry
    {
        struct Marker
        {
            quint32 render_id = 0;
            InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
            QPointF world_position;
        };

        struct Segment
        {
            quint32 render_id = 0;
            InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
            QLineF line;
        };

        QList<Marker> markers;
        QList<Segment> link_segments;
        QHash<quint64, int> marker_indices_by_entity;
        QHash<quint64, QList<int>> segment_indices_by_entity;
        QRectF world_bounds;
        QPointF world_origin;
        quint64 geometry_revision = 0;
    };

    struct RenderSymbology
    {
        quint64 revision = 0;
        VisualNode visual_node = VisualNode::None;
        NetworkSymbologyPalette node_palette = NetworkSymbologyPalette::Viridis;
        bool node_palette_flipped = false;
        VisualLink visual_link = VisualLink::None;
        NetworkSymbologyPalette link_palette = NetworkSymbologyPalette::Viridis;
        bool link_palette_flipped = false;
        VisualHeatmap visual_heatmap = VisualHeatmap::None;
        NetworkSymbologyPalette heatmap_palette = NetworkSymbologyPalette::Viridis;
        bool heatmap_palette_flipped = false;
        NetworkSymbologySizeUnit node_size_unit = NetworkSymbologySizeUnit::Pixels;
        int node_size_px = NetworkSymbologyDefaultNodeSizePx;
        double node_size_m = NetworkSymbologyDefaultNodeSizeM;
        NetworkSymbologySizeUnit icon_size_unit = NetworkSymbologySizeUnit::Pixels;
        int icon_size_px = NetworkSymbologyDefaultIconSizePx;
        double icon_size_m = NetworkSymbologyDefaultIconSizeM;
        NetworkSymbologySizeUnit link_thickness_unit = NetworkSymbologySizeUnit::Pixels;
        int link_thickness_px = NetworkSymbologyDefaultLinkThicknessPx;
        double link_thickness_m = NetworkSymbologyDefaultLinkThicknessM;
        bool show_flow_direction = true;
        qreal flow_direction_size_px = 10.0;
        QHash<quint32, qint8> flow_directions;
        HeatmapRadiusUnit heatmap_radius_unit = HeatmapRadiusUnit::Meters;
        int heatmap_radius_m = 400;
        int heatmap_radius_px = 50;
        int heatmap_solid_center_percent = 70;
        QHash<quint32, QRgb> node_colors;
        QHash<quint32, QRgb> link_colors;
        QHash<quint32, double> heatmap_fractions;
    };

    struct RenderRequest
    {
        quint64 request_id = 0;
        quint64 geometry_revision = 0;
        quint64 symbology_revision = 0;
        int zoom = -1;
        qreal meters_per_pixel = 1.0;
        qreal device_pixel_ratio = 1.0;
        QRgb icon_default_fill_color = qRgb(70, 70, 70);
        QSize logical_size;
        QRectF coverage_world_bounds;
        QRectF image_world_bounds;
        std::shared_ptr<const RenderGeometry> geometry;
        std::shared_ptr<const RenderSymbology> symbology;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    struct RenderResult
    {
        quint64 request_id = 0;
        quint64 geometry_revision = 0;
        quint64 symbology_revision = 0;
        int zoom = -1;
        qreal device_pixel_ratio = 1.0;
        QRectF coverage_world_bounds;
        QRectF image_world_bounds;
        QImage image;
        QImage heatmap_image;
    };

    enum class HitCollection
    {
        Markers,
        DeviceSegments,
        PipeSegments
    };

    struct PreparedGeometry
    {
        quint64 geometry_revision = 0;
        NetworkRenderSnapshot snapshot;
        std::shared_ptr<RenderGeometry> geometry;
        QList<HitMarker> hit_markers;
        QList<HitSegment> device_hit_segments;
        QList<HitSegment> pipe_hit_segments;
        QList<int> global_device_segment_indices;
        QList<int> global_pipe_segment_indices;
        QHash<quint64, SpatialCell> spatial_cells;
    };

    void syncSnapshot();
    void requestGeometryPreparation();
    static PreparedGeometry prepareGeometry(const NetworkRenderSnapshot &snapshot,
                                            const std::shared_ptr<std::atomic_bool> &cancelled);
    void applyPreparedGeometry(quint64 request_id, PreparedGeometry result);
    void requestSymbologyPreparation(bool force_values);
    void applyPreparedSymbology(quint64 request_id, quint64 geometry_revision,
                                std::shared_ptr<RenderSymbology> symbology);
    void clearRenderedCache();
    void requestRenderCache(bool force = false);
    RenderRequest createRenderRequest(quint64 request_id) const;
    static RenderResult renderRequest(const RenderRequest &request);
    static QImage renderHeatmap(const RenderRequest &request, qreal scale, qreal image_left, qreal image_top);
    void applyRenderResult(RenderResult result);
    bool renderedCacheCoversCurrentView() const;
    bool pendingCacheCoversCurrentView() const;
    bool coverageCoversCurrentView(const QRectF &coverage_world_bounds, int zoom) const;
    void paintNetwork(QPainter &painter);
    void paintSelectedEntity(QPainter &painter);
    void paintSimulationErrorEntity(QPainter &painter);
    void paintEntityHighlight(QPainter &painter, const NetworkOverlayHit &entity,
                              const QColor &color, bool outer = false);
    QList<NetworkOverlayHit> simulationErrorEntityHits() const;
    QPointF visibleReferenceWorldCenter() const;
    QRectF visibleReferenceWorldRect() const;
    qreal referenceScaleForCurrentZoom() const;
    QList<int> candidateIndices(qreal point_x, qreal point_y, qreal radius, HitCollection collection) const;
    NetworkOverlayHit nearestMarkerHit(qreal point_x, qreal point_y, qreal node_half_width, qreal icon_half_width) const;
    NetworkOverlayHit nearestSegmentHit(qreal point_x, qreal point_y, qreal hit_distance, HitCollection collection) const;
    QPointF geometryWorldPosition(const QPointF &screen_position) const;

    MapModel *map_model = nullptr;
    HydraulicData *hydraulic_data = nullptr;

    quint64 geometry_revision = 0;
    quint64 symbology_revision = 0;
    bool snapshot_initialized = false;
    NetworkRenderSnapshot snapshot;

    int background_opacity = 0;
    NetworkSymbologySettings symbology_settings;
    NetworkSymbologyRanges symbology_ranges;
    NetworkOverlayHit selected_entity;

    std::shared_ptr<const RenderGeometry> render_geometry;
    std::shared_ptr<const RenderSymbology> render_symbology;
    bool reference_geometry_ready = false;

    quint64 next_geometry_prepare_request_id = 0;
    quint64 active_geometry_prepare_request_id = 0;
    std::shared_ptr<std::atomic_bool> geometry_prepare_cancelled;
    quint64 next_symbology_prepare_request_id = 0;
    quint64 active_symbology_prepare_request_id = 0;
    std::shared_ptr<std::atomic_bool> symbology_prepare_cancelled;

    QImage rendered_network_cache;
    QImage rendered_heatmap_cache;
    QRectF rendered_cache_coverage_world_bounds;
    QRectF rendered_cache_image_world_bounds;
    int rendered_cache_zoom = -1;
    qreal rendered_cache_device_pixel_ratio = 0.0;
    quint64 rendered_cache_symbology_revision = 0;

    quint64 next_render_request_id = 0;
    quint64 pending_render_request_id = 0;
    QRectF pending_cache_coverage_world_bounds;
    int pending_cache_zoom = -1;
    qreal pending_cache_device_pixel_ratio = 0.0;
    quint64 pending_cache_symbology_revision = 0;
    std::shared_ptr<std::atomic_bool> pending_render_cancelled;
    bool rendering_active = false;
    bool render_worker_running = false;
    quint64 active_render_request_id = 0;
    bool render_restart_requested = false;
    bool render_restart_force = false;

    QList<HitMarker> hit_markers;
    QList<HitSegment> device_hit_segments;
    QList<HitSegment> pipe_hit_segments;
    QList<int> global_device_segment_indices;
    QList<int> global_pipe_segment_indices;
    QHash<quint64, SpatialCell> spatial_cells;
};

#endif // MAP_NETWORK_OVERLAY_WIDGET_H
