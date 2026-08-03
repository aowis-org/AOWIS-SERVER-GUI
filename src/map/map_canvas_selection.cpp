#include "map_canvas_selection.h"
#include "map_canvas_devicelinks.h"
#include "map_canvas_markers.h"
#include "map_canvas_pipes.h"
#include "map_canvas_widget.h"
#include "map_model.h"

#include "../geo_web_mercator.h"

MapCanvasSelection::MapCanvasSelection(MapModel *map_model, MapCanvasWidget *map_canvas,
                                       MapCanvasMarkers *point_markers,
                                       MapCanvasDeviceLinks *device_links,
                                       MapCanvasPipes *pipes, QObject *parent)
    : QObject(parent), map_model(map_model), map_canvas(map_canvas),
      point_markers(point_markers), device_links(device_links), pipes(pipes)
{
}

QList<MapEntityMarker> MapCanvasSelection::selectedMarkers() const
{
    QList<MapEntityMarker> markers;
    markers.reserve(this->list_selected_marker_uuids.size());
    for (const QUuid &uuid : this->list_selected_marker_uuids)
    {
        std::optional<MapEntityMarker> marker;
        if (this->point_markers)
            marker = this->point_markers->markerByUuid(uuid);
        if (!marker.has_value() && this->device_links)
            marker = this->device_links->markerByUuid(uuid);
        if (marker.has_value())
            markers.append(marker.value());
    }
    return markers;
}

const QList<QUuid> &MapCanvasSelection::selectedMarkerUuids() const
{
    return this->list_selected_marker_uuids;
}

int MapCanvasSelection::selectedMarkerCount() const
{
    return this->list_selected_marker_uuids.size();
}

bool MapCanvasSelection::isMarkerSelected(const QUuid &uuid) const
{
    return !uuid.isNull() && this->list_selected_marker_uuids.contains(uuid);
}

bool MapCanvasSelection::hasSelection() const
{
    return !this->list_selected_marker_uuids.isEmpty() ||
           (this->pipes && this->pipes->hasSelection());
}

void MapCanvasSelection::clear()
{
    this->list_selected_marker_uuids.clear();
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
    if (marker.entity.uuid.isNull() || isMarkerSelected(marker.entity.uuid))
        return;
    this->list_selected_marker_uuids.append(marker.entity.uuid);
}

void MapCanvasSelection::toggleMarker(const MapEntityMarker &marker)
{
    if (marker.entity.uuid.isNull())
        return;

    const int index = this->list_selected_marker_uuids.indexOf(marker.entity.uuid);
    if (index >= 0)
    {
        this->list_selected_marker_uuids.removeAt(index);
        return;
    }
    addMarker(marker);
}

void MapCanvasSelection::removeMarker(const QUuid &uuid)
{
    this->list_selected_marker_uuids.removeAll(uuid);
}

std::optional<InfrastructureEntityReference> MapCanvasSelection::replaceWithPipe(
    const QUuid &pipe_uuid)
{
    clear();
    if (!this->pipes)
        return std::nullopt;
    return this->pipes->selectPipe(pipe_uuid);
}

void MapCanvasSelection::selectInRectangle(
    const QRect &rect,
    const QList<MapEntityMarker> &point_markers,
    const QList<MapEntityMarker> &device_link_markers,
    bool replace)
{
    if (replace)
        clear();

    addPointMarkersInRectangle(rect, point_markers);
    addDeviceMarkersInRectangle(rect, device_link_markers);
    if (this->pipes)
        this->pipes->selectPipesWithSelectedEndpoints(this->list_selected_marker_uuids);
}

void MapCanvasSelection::moveSelected(const QPointF &from_position,
                                      const QPointF &to_position)
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
    const double latitude_delta = to_coordinate.latitude_deg - from_coordinate.latitude_deg;
    const double longitude_delta = GeoWebMercator::normalizeLongitude(
        to_coordinate.longitude_deg - from_coordinate.longitude_deg);

    for (const QUuid &uuid : this->list_selected_marker_uuids)
    {
        if (!this->point_markers->moveByDelta(uuid, longitude_delta, latitude_delta))
            this->device_links->moveCenterByDelta(uuid, longitude_delta, latitude_delta);
    }

    this->pipes->moveIntermediateVerticesWithSelectedEndpoints(
        this->list_selected_marker_uuids, longitude_delta, latitude_delta);
}

void MapCanvasSelection::addPointMarkersInRectangle(
    const QRect &rect, const QList<MapEntityMarker> &markers)
{
    if (!this->point_markers)
        return;

    for (const MapEntityMarker &marker : markers)
    {
        if (rect.contains(this->point_markers->markerRect(marker).center().toPoint()))
            addMarker(marker);
    }
}

void MapCanvasSelection::addDeviceMarkersInRectangle(
    const QRect &rect, const QList<MapEntityMarker> &markers)
{
    if (!this->device_links)
        return;

    for (const MapEntityMarker &marker : markers)
    {
        if (rect.contains(this->device_links->markerRect(marker).center().toPoint()))
            addMarker(marker);
    }
}
