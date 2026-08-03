#ifndef MAP_NETWORK_OVERLAY_WIDGET_H
#define MAP_NETWORK_OVERLAY_WIDGET_H

#include <QHash>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QWidget>

#include "map_model.h"
#include "../network_render_snapshot.h"

class HydraulicData;
class QPaintEvent;

struct NetworkOverlayHit
{
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;

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

    int backgroundOpacity() const;
    NetworkOverlayHit hitTest(const QPointF &screen_position);

public slots:
    void setBackgroundOpacity(int opacity);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct ProjectedNode
    {
        quint32 render_id = 0;
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        QPointF world_position;
    };

    struct ProjectedLink
    {
        quint32 render_id = 0;
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        QList<QPointF> world_vertices;
    };

    struct HitMarker
    {
        quint32 render_id = 0;
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        QPointF world_position;
    };

    struct HitSegment
    {
        quint32 render_id = 0;
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        QPointF start;
        QPointF end;
    };

    struct SpatialCell
    {
        QList<int> marker_indices;
        QList<int> device_segment_indices;
        QList<int> pipe_segment_indices;
    };

    enum class HitCollection
    {
        Markers,
        DeviceSegments,
        PipeSegments
    };

    void syncSnapshot();
    void invalidateCache();
    void ensureCache();
    void rebuildCache();
    void rebuildSpatialIndex();
    QList<int> candidateIndices(qreal point_x, qreal point_y, qreal radius, HitCollection collection) const;
    NetworkOverlayHit nearestMarkerHit(qreal point_x, qreal point_y, qreal marker_half_width) const;
    NetworkOverlayHit nearestSegmentHit(qreal point_x, qreal point_y, HitCollection collection) const;
    QPointF cachedImageScreenPosition() const;
    QPointF geometryWorldPosition(const QPointF &screen_position) const;

    MapModel *map_model = nullptr;
    HydraulicData *hydraulic_data = nullptr;

    quint64 geometry_revision = 0;
    bool snapshot_initialized = false;
    NetworkRenderSnapshot snapshot;

    int background_opacity = 0;

    QImage cached_image;
    QPointF cached_image_world_top_left;
    QPointF cached_geometry_world_origin;
    int cached_zoom = -1;
    qreal cached_device_pixel_ratio = 0.0;
    bool cache_initialized = false;
    bool cached_geometry_ready = false;

    QList<HitMarker> hit_markers;
    QList<HitSegment> device_hit_segments;
    QList<HitSegment> pipe_hit_segments;
    QList<int> global_device_segment_indices;
    QList<int> global_pipe_segment_indices;
    QHash<quint64, SpatialCell> spatial_cells;
};

#endif // MAP_NETWORK_OVERLAY_WIDGET_H
