#ifndef MAP_EDITOR_RENDERER_H
#define MAP_EDITOR_RENDERER_H

#include "map_editor_render_state.h"
#include "map_entity_pixmap_renderer.h"

class MapModel;
class QPaintEvent;
class QPainter;
class QWidget;

class MapEditorRenderer
{
public:
    explicit MapEditorRenderer(MapModel *map_model, QWidget *canvas);

    void paint(QPainter &painter, const QPaintEvent &event,
               const MapEditorRenderState &state,
               const MapEditorViewportRenderState &viewport_state);

private:
    QPointF screenFromWgs84(const CoordinateWGS84 &coordinate,
                            double wrap_reference_longitude) const;
    const MapEditorRenderMarker *markerByUuid(const QList<MapEditorRenderMarker> &markers,
                                              const QUuid &uuid) const;
    void paintBackground(QPainter &painter,
                         const MapEditorViewportRenderState &viewport_state) const;
    void paintTileSelection(QPainter &painter,
                            const MapEditorViewportRenderState &viewport_state) const;
    void paintRectangleSelection(QPainter &painter,
                                 const MapEditorViewportRenderState &viewport_state) const;
    void paintNetwork(QPainter &painter, const MapEditorRenderState &state);
    void paintPipes(QPainter &painter, const MapEditorRenderState &state) const;
    void paintDeviceLinks(QPainter &painter, const MapEditorRenderState &state);
    void paintMarkers(QPainter &painter, const MapEditorRenderState &state);
    void paintPlacement(QPainter &painter, const MapEditorRenderState &state);

    MapModel *map_model = nullptr;
    QWidget *canvas = nullptr;
    MapEntityPixmapRenderer pixmap_renderer;
};

#endif // MAP_EDITOR_RENDERER_H
