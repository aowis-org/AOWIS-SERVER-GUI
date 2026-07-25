#ifndef MAP_CANVAS_SELECTION_H
#define MAP_CANVAS_SELECTION_H

#include <optional>
#include <QObject>
#include <QList>
#include <QUuid>
#include "map_models.h"

class MapCanvasPipes;
class MapCanvasSelection : public QObject
{
    Q_OBJECT
    
public:
    explicit MapCanvasSelection(MapCanvasPipes *pipes, QObject *parent = nullptr);
    
    const QList<MapEntityMarker> &selectedMarkers() const;
    int selectedMarkerCount() const;
    bool isMarkerSelected(MapEntityMarkerLabel *label) const;
    bool hasSelection() const;
    
    void clear();
    void replaceWithMarker(const MapEntityMarker &marker);
    void addMarker(const MapEntityMarker &marker);
    void toggleMarker(const MapEntityMarker &marker);
    void removeMarker(MapEntityMarkerLabel *label);
    std::optional<InfrastructureEntityReference> replaceWithPipe(const QUuid &pipe_uuid);
    void selectInRectangle(const CoordinateWGS84Rect &rect, const QList<MapEntityMarker> &point_markers,
                           const QList<MapEntityMarker> &device_link_markers, bool replace);
    void setMouseTransparency(bool transparent);
    
private:
    void addMarkersInRectangle(const CoordinateWGS84Rect &rect, const QList<MapEntityMarker> &markers);
    
    MapCanvasPipes *pipes = nullptr;
    QList<MapEntityMarker> list_selected_markers;
};

#endif // MAP_CANVAS_SELECTION_H
