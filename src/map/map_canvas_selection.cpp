#include "map_canvas_selection.h"
#include "map_canvas_pipes.h"

MapCanvasSelection::MapCanvasSelection(MapCanvasPipes *pipes, QObject *parent)
    : QObject(parent),
    pipes(pipes)
{}

const QList<MapEntityMarker> &MapCanvasSelection::selectedMarkers() const
{
    return this->list_selected_markers;
}

int MapCanvasSelection::selectedMarkerCount() const
{
    return this->list_selected_markers.size();
}

bool MapCanvasSelection::isMarkerSelected(MapEntityMarkerLabel *label) const
{
    if (!label)
        return false;
    
    for (const MapEntityMarker &marker : this->list_selected_markers)
    {
        if (marker.label == label)
            return true;
    }
    
    return false;
}

bool MapCanvasSelection::hasSelection() const
{
    return !this->list_selected_markers.isEmpty() ||
           (this->pipes && this->pipes->hasSelection());
}

void MapCanvasSelection::clear()
{
    for (MapEntityMarker &marker : this->list_selected_markers)
    {
        if (marker.label)
            marker.label->clearHighlight();
    }
    
    this->list_selected_markers.clear();
    if (this->pipes)
        this->pipes->clearSelection();
}

void MapCanvasSelection::replaceWithMarker(const MapEntityMarker &marker)
{
    clear();
    addMarker(marker);
}

void MapCanvasSelection::addMarker(const MapEntityMarker &marker)
{
    if (!marker.label || isMarkerSelected(marker.label))
        return;
    
    marker.label->setHighlightSelected();
    this->list_selected_markers.append(marker);
}

void MapCanvasSelection::toggleMarker(const MapEntityMarker &marker)
{
    if (!marker.label)
        return;
    
    for (int i = 0; i < this->list_selected_markers.size(); i++)
    {
        if (this->list_selected_markers[i].label != marker.label)
            continue;
        
        marker.label->clearHighlight();
        this->list_selected_markers.removeAt(i);
        return;
    }
    
    addMarker(marker);
}

void MapCanvasSelection::removeMarker(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    for (int i = this->list_selected_markers.size() - 1; i >= 0; i--)
    {
        if (this->list_selected_markers[i].label != label)
            continue;
        
        label->clearHighlight();
        this->list_selected_markers.removeAt(i);
    }
}

std::optional<InfrastructureEntityReference> MapCanvasSelection::replaceWithPipe(const QUuid &pipe_uuid)
{
    clear();
    if (!this->pipes)
        return std::nullopt;
    
    return this->pipes->selectPipe(pipe_uuid);
}

void MapCanvasSelection::selectInRectangle(const CoordinateWGS84Rect &rect,
                                           const QList<MapEntityMarker> &point_markers,
                                           const QList<MapEntityMarker> &device_link_markers, bool replace)
{
    if (replace)
        clear();
    
    addMarkersInRectangle(rect, point_markers);
    addMarkersInRectangle(rect, device_link_markers);
    
    if (this->pipes)
        this->pipes->selectPipesWithSelectedEndpoints(this->list_selected_markers);
}

void MapCanvasSelection::addMarkersInRectangle(const CoordinateWGS84Rect &rect,
                                               const QList<MapEntityMarker> &markers)
{
    const double north = rect.north_west.lat;
    const double west = rect.north_west.lon;
    const double south = rect.south_east.lat;
    const double east = rect.south_east.lon;
    
    for (const MapEntityMarker &marker : markers)
    {
        if (!marker.label)
            continue;
        
        const CoordinateWGS84 &coordinate = marker.coord_wgs84;
        if (coordinate.lat < south || coordinate.lat > north ||
            coordinate.lon < west || coordinate.lon > east)
        {
            continue;
        }
        
        addMarker(marker);
    }
}
void MapCanvasSelection::setMouseTransparency(bool transparent)
{
    for (const MapEntityMarker &marker : this->list_selected_markers)
    {
        if (marker.label)
            marker.label->setAttribute(Qt::WA_TransparentForMouseEvents, transparent);
    }
}
