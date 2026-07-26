#include "map_canvas_selection.h"
#include "map_canvas_devicelinks.h"
#include "map_canvas_markers.h"
#include "map_canvas_pipes.h"
#include "map_canvas_widget.h"
#include "map_model.h"

MapCanvasSelection::MapCanvasSelection(MapModel *map_model, MapCanvasWidget *map_canvas,
                                       MapCanvasMarkers *point_markers,
                                       MapCanvasDeviceLinks *device_links,
                                       MapCanvasPipes *pipes, QObject *parent)
    : QObject(parent), map_model(map_model), map_canvas(map_canvas),
    point_markers(point_markers), device_links(device_links), pipes(pipes)
{}

const QList<MapEntityMarker> &MapCanvasSelection::selectedMarkers() const
{
    return this->list_selected_markers;
}

QList<MapEntityMarkerLabel *> MapCanvasSelection::selectedLabels() const
{
    QList<MapEntityMarkerLabel *> labels;
    labels.reserve(this->list_selected_markers.size());
    for (const MapEntityMarker &marker : this->list_selected_markers)
    {
        if (marker.label)
            labels.append(marker.label);
    }
    return labels;
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

std::optional<InfrastructureEntityReference> MapCanvasSelection::replaceWithPipe(
    const QUuid &pipe_uuid)
{
    clear();
    if (!this->pipes)
        return std::nullopt;
    return this->pipes->selectPipe(pipe_uuid);
}

void MapCanvasSelection::selectInRectangle(const CoordinateWGS84Rect &rect,
                                           const QList<MapEntityMarker> &point_markers,
                                           const QList<MapEntityMarker> &device_link_markers,
                                           bool replace)
{
    if (replace)
        clear();
    
    addMarkersInRectangle(rect, point_markers);
    addMarkersInRectangle(rect, device_link_markers);
    if (this->pipes)
        this->pipes->selectPipesWithSelectedEndpoints(this->list_selected_markers);
}

void MapCanvasSelection::setMouseTransparency(bool transparent)
{
    for (const MapEntityMarker &marker : this->list_selected_markers)
    {
        if (marker.label)
            marker.label->setAttribute(Qt::WA_TransparentForMouseEvents, transparent);
    }
}

void MapCanvasSelection::moveSelected(const QPointF &from_position, const QPointF &to_position)
{
    if (!this->map_model || !this->map_canvas || !this->point_markers ||
        !this->device_links || !this->pipes)
    {
        return;
    }
    
    const CoordinateWGS84 from_coordinate = this->map_model->wgs84FromScreen(
        from_position.toPoint(), this->map_canvas->size());
    const CoordinateWGS84 to_coordinate = this->map_model->wgs84FromScreen(
        to_position.toPoint(), this->map_canvas->size());
    const double longitude_delta = to_coordinate.lon - from_coordinate.lon;
    const double latitude_delta = to_coordinate.lat - from_coordinate.lat;
    
    for (const MapEntityMarker &selected_marker : this->list_selected_markers)
    {
        if (!selected_marker.label)
            continue;
        
        if (!this->point_markers->moveByDelta(
                selected_marker.label, longitude_delta, latitude_delta))
        {
            this->device_links->moveCenterByDelta(
                selected_marker.label, longitude_delta, latitude_delta);
        }
    }
    
    this->pipes->moveIntermediateVerticesWithSelectedEndpoints(
        this->list_selected_markers, longitude_delta, latitude_delta);
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
