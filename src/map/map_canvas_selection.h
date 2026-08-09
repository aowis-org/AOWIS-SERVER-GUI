#ifndef MAP_CANVAS_SELECTION_H
#define MAP_CANVAS_SELECTION_H

#include <optional>
#include <QObject>
#include <QList>
#include <QPointer>
#include <QPointF>
#include <QRect>
#include <QUuid>

#include "map_models.h"

class MapCanvasDeviceLinks;
class MapCanvasMarkers;
class MapCanvasPipes;
class MapCanvasWidget;
class MapModel;

class MapCanvasSelection : public QObject
{
    Q_OBJECT

public:
    explicit MapCanvasSelection(MapModel *map_model, MapCanvasWidget *map_canvas,
                                MapCanvasMarkers *point_markers,
                                MapCanvasDeviceLinks *device_links,
                                MapCanvasPipes *pipes, QObject *parent = nullptr);

    QList<MapEntityMarker> selectedMarkers() const;
    const QList<QUuid> &selectedMarkerUuids() const;
    int selectedMarkerCount() const;
    bool isMarkerSelected(const QUuid &uuid) const;
    bool hasSelection() const;

    void clear();
    void replaceWithMarker(const MapEntityMarker &marker);
    void addMarker(const MapEntityMarker &marker);
    void toggleMarker(const MapEntityMarker &marker);
    void removeMarker(const QUuid &uuid);
    std::optional<InfrastructureEntityReference> replaceWithPipe(const QUuid &pipe_uuid);
    void selectInRectangle(const QRect &rect,
                           const QList<MapEntityMarker> &point_markers,
                           const QList<MapEntityMarker> &device_link_markers,
                           bool replace);
    void moveSelected(const QPointF &from_position, const QPointF &to_position,
                      const QList<QUuid> &translated_pipe_uuids);

private:
    void addPointMarkersInRectangle(const QRect &rect,
                                    const QList<MapEntityMarker> &markers);
    void addDeviceMarkersInRectangle(const QRect &rect,
                                     const QList<MapEntityMarker> &markers);

    MapModel *map_model = nullptr;
    QPointer<MapCanvasWidget> map_canvas;
    MapCanvasMarkers *point_markers = nullptr;
    MapCanvasDeviceLinks *device_links = nullptr;
    MapCanvasPipes *pipes = nullptr;
    QList<QUuid> list_selected_marker_uuids;
};

#endif // MAP_CANVAS_SELECTION_H
