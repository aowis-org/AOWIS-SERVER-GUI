#ifndef MAP_NETWORK_OVERLAY_WIDGET_H
#define MAP_NETWORK_OVERLAY_WIDGET_H

#include <QImage>
#include <QList>
#include <QPointF>
#include <QWidget>

#include "map_model.h"
#include "../network_render_snapshot.h"

class HydraulicData;
class QPaintEvent;

class MapNetworkOverlayWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MapNetworkOverlayWidget(MapModel *map_model, HydraulicData *hydraulic_data, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct ProjectedNode
    {
        quint32 render_id = 0;
        QPointF world_position;
    };

    struct ProjectedLink
    {
        QList<QPointF> world_vertices;
    };

    void syncSnapshot();
    void invalidateCachedImage();
    void rebuildCachedImage();
    QPointF cachedImageScreenPosition() const;

    MapModel *map_model = nullptr;
    HydraulicData *hydraulic_data = nullptr;

    quint64 geometry_revision = 0;
    bool snapshot_initialized = false;
    NetworkRenderSnapshot snapshot;

    QImage cached_image;
    QPointF cached_image_world_top_left;
    QPointF cached_geometry_world_origin;
    int cached_zoom = -1;
    qreal cached_device_pixel_ratio = 0.0;
};

#endif // MAP_NETWORK_OVERLAY_WIDGET_H
