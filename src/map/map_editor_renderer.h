#ifndef MAP_EDITOR_RENDERER_H
#define MAP_EDITOR_RENDERER_H

#include "map_editor_visual_state.h"
#include "map_entity_pixmap_renderer.h"

#include "../network_render_snapshot.h"

#include <QHash>

class MapModel;
class QPaintEvent;
class QPainter;
class QWidget;

class MapEditorRenderer
{
public:
    explicit MapEditorRenderer(MapModel *map_model, QWidget *canvas);

    void paint(QPainter &painter, const QPaintEvent &event,
               const NetworkRenderSnapshot &network_snapshot,
               const MapEditorVisualState &visual_state,
               const MapEditorViewportRenderState &viewport_state);

private:
    QPointF screenFromWgs84(const CoordinateWGS84 &coordinate,
                            double wrap_reference_longitude) const;
    const NetworkRenderNode *nodeByUuid(
        const QHash<QUuid, const NetworkRenderNode *> &nodes_by_uuid,
        const QUuid &uuid) const;
    CoordinateWGS84 deviceLinkCenterCoordinate(const NetworkRenderLink &link) const;
    void paintBackground(QPainter &painter,
                         const MapEditorViewportRenderState &viewport_state) const;
    void paintTileSelection(QPainter &painter,
                            const MapEditorViewportRenderState &viewport_state) const;
    void paintRectangleSelection(QPainter &painter,
                                 const MapEditorViewportRenderState &viewport_state) const;
    void paintNetwork(QPainter &painter,
                      const NetworkRenderSnapshot &network_snapshot,
                      const MapEditorVisualState &visual_state);
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
};

#endif // MAP_EDITOR_RENDERER_H
