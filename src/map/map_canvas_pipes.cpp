#include "map_canvas_pipes.h"
#include "map_canvas_widget.h"

namespace
{
constexpr double link_hit_distance = 7.0;
constexpr double pipe_vertex_radius = 4.0;
constexpr double pipe_vertex_hit_distance = 9.0;

bool isHydraulicConnectionNode(InfrastructureEntity entity)
{
    return entity == InfrastructureEntity::Junction ||
           entity == InfrastructureEntity::Reservoir ||
           entity == InfrastructureEntity::Tank;
}

QPointF nearestPointOnSegment(const QPointF &point,
                              const QPointF &segment_start,
                              const QPointF &segment_end)
{
    const double segment_x = segment_end.x() - segment_start.x();
    const double segment_y = segment_end.y() - segment_start.y();
    const double segment_length_squared = segment_x * segment_x + segment_y * segment_y;
    
    if (segment_length_squared <= 0.0)
        return segment_start;
    
    const double projection = ((point.x() - segment_start.x()) * segment_x +
                               (point.y() - segment_start.y()) * segment_y) /
                              segment_length_squared;
    const double bounded_projection = qBound(0.0, projection, 1.0);
    return QPointF(segment_start.x() + bounded_projection * segment_x,
                   segment_start.y() + bounded_projection * segment_y);
}
}

bool MapCanvasPipes::PipeVertexHit::isValid() const
{
    return !this->pipe_uuid.isNull() && this->vertex_index >= 0;
}

bool MapCanvasPipes::PipeSegmentHit::isValid() const
{
    return !this->pipe_uuid.isNull() && this->insert_index >= 0;
}

MapCanvasPipes::MapCanvasPipes(MapModel *map_model, MapCanvasWidget *map_canvas, QObject *parent)
    : QObject(parent),
    map_model(map_model),
    map_canvas(map_canvas)
{}

void MapCanvasPipes::clearPlacement()
{
    this->pipe_start_label = nullptr;
    this->pipe_intermediate_vertices.clear();
}

bool MapCanvasPipes::hasStartLabel() const
{
    return !this->pipe_start_label.isNull();
}

MapEntityMarkerLabel *MapCanvasPipes::startLabel() const
{
    return this->pipe_start_label.data();
}

void MapCanvasPipes::startPipe(MapEntityMarkerLabel *start_label)
{
    this->pipe_start_label = start_label;
    this->pipe_intermediate_vertices.clear();
    updateCanvas();
}

void MapCanvasPipes::appendIntermediateVertex(const CoordinateWGS84 &coordinate)
{
    this->pipe_intermediate_vertices.append(coordinate);
    updateCanvas();
}

bool MapCanvasPipes::completePipe(const InfrastructureEntityReference &start_node,
                                  const InfrastructureEntityReference &end_node,
                                  MapEntityMarkerLabel *end_label)
{
    if (!this->pipe_start_label || !end_label)
        return false;
    
    PipeCanvasItem pipe;
    pipe.entity.type = InfrastructureEntity::Pipe;
    pipe.entity.uuid = QUuid::createUuid();
    pipe.geometry.start_node = start_node;
    pipe.geometry.end_node = end_node;
    pipe.geometry.intermediate_vertices = this->pipe_intermediate_vertices;
    pipe.start_label = this->pipe_start_label;
    pipe.end_label = end_label;
    this->list_pipes.append(pipe);
    
    clearPlacement();
    updateCanvas();
    return true;
}

void MapCanvasPipes::paint(QPainter &paint,
                           const QList<MapEntityMarker> &markers,
                           bool placing_pipe,
                           const QPointF &mouse_position,
                           MapEntityMarkerLabel *connection_target_label) const
{
    paint.save();
    
    for (const PipeCanvasItem &pipe : this->list_pipes)
    {
        if (!pipe.start_label || !pipe.end_label)
            continue;
        
        const MapEntityMarker start_marker = markerByLabel(pipe.start_label.data(), markers);
        const MapEntityMarker end_marker = markerByLabel(pipe.end_label.data(), markers);
        if (!isHydraulicConnectionNode(start_marker.entity.type) ||
            !isHydraulicConnectionNode(end_marker.entity.type))
        {
            continue;
        }
        
        QPen pipe_pen(pipe.selected ? QColor(0, 190, 255) : QColor(Qt::black));
        pipe_pen.setWidthF(3.0);
        pipe_pen.setCapStyle(Qt::RoundCap);
        pipe_pen.setJoinStyle(Qt::RoundJoin);
        paint.setPen(pipe_pen);
        
        QPointF previous_point = this->map_model->screenFromWgs84(start_marker.coord_wgs84,
                                                                  this->map_canvas->size());
        for (const CoordinateWGS84 &vertex : pipe.geometry.intermediate_vertices)
        {
            const QPointF vertex_point = this->map_model->screenFromWgs84(vertex,
                                                                          this->map_canvas->size());
            paint.drawLine(previous_point, vertex_point);
            previous_point = vertex_point;
        }
        
        const QPointF end_point = this->map_model->screenFromWgs84(end_marker.coord_wgs84,
                                                                   this->map_canvas->size());
        paint.drawLine(previous_point, end_point);
        
        paint.setPen(Qt::NoPen);
        paint.setBrush(pipe.selected ? QColor(0, 190, 255) : QColor(Qt::black));
        for (const CoordinateWGS84 &vertex : pipe.geometry.intermediate_vertices)
        {
            const QPointF vertex_point = this->map_model->screenFromWgs84(vertex,
                                                                          this->map_canvas->size());
            paint.drawEllipse(vertex_point, pipe_vertex_radius, pipe_vertex_radius);
        }
    }
    
    if (placing_pipe && this->pipe_start_label)
    {
        const MapEntityMarker start_marker = markerByLabel(this->pipe_start_label.data(), markers);
        if (isHydraulicConnectionNode(start_marker.entity.type))
        {
            QPen preview_pen(QColor(0, 140, 255));
            preview_pen.setWidthF(3.0);
            preview_pen.setCapStyle(Qt::RoundCap);
            preview_pen.setJoinStyle(Qt::RoundJoin);
            paint.setPen(preview_pen);
            
            QPointF previous_point = this->map_model->screenFromWgs84(start_marker.coord_wgs84,
                                                                      this->map_canvas->size());
            for (const CoordinateWGS84 &vertex : this->pipe_intermediate_vertices)
            {
                const QPointF vertex_point = this->map_model->screenFromWgs84(vertex,
                                                                              this->map_canvas->size());
                paint.drawLine(previous_point, vertex_point);
                previous_point = vertex_point;
            }
            
            QPointF preview_end = mouse_position;
            if (connection_target_label)
            {
                const MapEntityMarker end_marker = markerByLabel(connection_target_label, markers);
                if (isHydraulicConnectionNode(end_marker.entity.type))
                {
                    preview_end = this->map_model->screenFromWgs84(end_marker.coord_wgs84,
                                                                   this->map_canvas->size());
                }
            }
            
            paint.drawLine(previous_point, preview_end);
        }
    }
    
    paint.restore();
}

bool MapCanvasPipes::hasSelection() const
{
    for (const PipeCanvasItem &pipe : this->list_pipes)
    {
        if (pipe.selected)
            return true;
    }
    
    return false;
}

void MapCanvasPipes::clearSelection()
{
    for (PipeCanvasItem &pipe : this->list_pipes)
        pipe.selected = false;
}

std::optional<InfrastructureEntityReference> MapCanvasPipes::selectPipe(const QUuid &pipe_uuid)
{
    PipeCanvasItem *pipe = pipeByUuid(pipe_uuid);
    if (!pipe)
        return std::nullopt;
    
    pipe->selected = true;
    updateCanvas();
    return pipe->entity;
}

void MapCanvasPipes::deleteSelected()
{
    for (int i = this->list_pipes.size() - 1; i >= 0; i--)
    {
        if (!this->list_pipes[i].selected)
            continue;
        
        if (this->pipe_vertex_move_pipe_uuid.has_value() &&
            this->pipe_vertex_move_pipe_uuid.value() == this->list_pipes[i].entity.uuid)
        {
            cancelPipeVertexMove();
        }
        
        this->list_pipes.removeAt(i);
    }
    
    updateCanvas();
}

void MapCanvasPipes::selectPipesWithSelectedEndpoints(
    const QList<MapEntityMarker> &selected_markers)
{
    for (PipeCanvasItem &pipe : this->list_pipes)
    {
        if (!pipe.start_label || !pipe.end_label)
            continue;
        
        if (markerIsSelected(pipe.start_label.data(), selected_markers) &&
            markerIsSelected(pipe.end_label.data(), selected_markers))
            pipe.selected = true;
    }
}

void MapCanvasPipes::moveIntermediateVerticesWithSelectedEndpoints(
    const QList<MapEntityMarker> &selected_markers,
    double longitude_delta,
    double latitude_delta)
{
    for (PipeCanvasItem &pipe : this->list_pipes)
    {
        if (!markerIsSelected(pipe.start_label.data(), selected_markers) ||
            !markerIsSelected(pipe.end_label.data(), selected_markers))
            continue;
        
        for (CoordinateWGS84 &vertex : pipe.geometry.intermediate_vertices)
        {
            vertex.lon += longitude_delta;
            vertex.lat += latitude_delta;
        }
    }
}

std::optional<InfrastructureEntityReference> MapCanvasPipes::pipeAt(
    const QPointF &position,
    const QList<MapEntityMarker> &markers) const
{
    const PipeSegmentHit hit = pipeSegmentAt(position, markers);
    if (!hit.isValid())
        return std::nullopt;
    
    const PipeCanvasItem *pipe = pipeByUuid(hit.pipe_uuid);
    if (!pipe)
        return std::nullopt;
    
    return pipe->entity;
}

MapCanvasPipes::PipeVertexHit MapCanvasPipes::pipeVertexAt(const QPointF &position) const
{
    PipeVertexHit hit;
    double nearest_distance_squared = pipe_vertex_hit_distance * pipe_vertex_hit_distance;
    
    for (const PipeCanvasItem &pipe : this->list_pipes)
    {
        for (int i = 0; i < pipe.geometry.intermediate_vertices.size(); i++)
        {
            const QPointF vertex_point = this->map_model->screenFromWgs84(
                pipe.geometry.intermediate_vertices[i], this->map_canvas->size());
            const double distance_x = position.x() - vertex_point.x();
            const double distance_y = position.y() - vertex_point.y();
            const double distance_squared = distance_x * distance_x + distance_y * distance_y;
            if (distance_squared > nearest_distance_squared)
                continue;
            
            nearest_distance_squared = distance_squared;
            hit.pipe_uuid = pipe.entity.uuid;
            hit.vertex_index = i;
        }
    }
    
    return hit;
}

MapCanvasPipes::PipeSegmentHit MapCanvasPipes::pipeSegmentAt(
    const QPointF &position,
    const QList<MapEntityMarker> &markers) const
{
    PipeSegmentHit hit;
    double nearest_distance_squared = link_hit_distance * link_hit_distance;
    
    for (const PipeCanvasItem &pipe : this->list_pipes)
    {
        if (!pipe.start_label || !pipe.end_label)
            continue;
        
        const MapEntityMarker start_marker = markerByLabel(pipe.start_label.data(), markers);
        const MapEntityMarker end_marker = markerByLabel(pipe.end_label.data(), markers);
        if (!isHydraulicConnectionNode(start_marker.entity.type) ||
            !isHydraulicConnectionNode(end_marker.entity.type))
        {
            continue;
        }
        
        QPointF previous_point = this->map_model->screenFromWgs84(start_marker.coord_wgs84,
                                                                  this->map_canvas->size());
        for (int i = 0; i < pipe.geometry.intermediate_vertices.size(); i++)
        {
            const QPointF vertex_point = this->map_model->screenFromWgs84(
                pipe.geometry.intermediate_vertices[i], this->map_canvas->size());
            const QPointF nearest_point = nearestPointOnSegment(position, previous_point, vertex_point);
            const double distance_x = position.x() - nearest_point.x();
            const double distance_y = position.y() - nearest_point.y();
            const double distance_squared = distance_x * distance_x + distance_y * distance_y;
            
            if (distance_squared <= nearest_distance_squared)
            {
                nearest_distance_squared = distance_squared;
                hit.pipe_uuid = pipe.entity.uuid;
                hit.insert_index = i;
                hit.nearest_point = nearest_point;
            }
            
            previous_point = vertex_point;
        }
        
        const QPointF end_point = this->map_model->screenFromWgs84(end_marker.coord_wgs84,
                                                                   this->map_canvas->size());
        const QPointF nearest_point = nearestPointOnSegment(position, previous_point, end_point);
        const double distance_x = position.x() - nearest_point.x();
        const double distance_y = position.y() - nearest_point.y();
        const double distance_squared = distance_x * distance_x + distance_y * distance_y;
        
        if (distance_squared <= nearest_distance_squared)
        {
            nearest_distance_squared = distance_squared;
            hit.pipe_uuid = pipe.entity.uuid;
            hit.insert_index = pipe.geometry.intermediate_vertices.size();
            hit.nearest_point = nearest_point;
        }
    }
    
    return hit;
}

bool MapCanvasPipes::addPipeVertex(const QUuid &pipe_uuid,
                                   int insert_index,
                                   const CoordinateWGS84 &coordinate)
{
    PipeCanvasItem *pipe = pipeByUuid(pipe_uuid);
    if (!pipe || insert_index < 0 || insert_index > pipe->geometry.intermediate_vertices.size())
        return false;
    
    pipe->geometry.intermediate_vertices.insert(insert_index, coordinate);
    updateCanvas();
    return true;
}

bool MapCanvasPipes::deletePipeVertex(const QUuid &pipe_uuid, int vertex_index)
{
    PipeCanvasItem *pipe = pipeByUuid(pipe_uuid);
    if (!pipe || vertex_index < 0 || vertex_index >= pipe->geometry.intermediate_vertices.size())
        return false;
    
    pipe->geometry.intermediate_vertices.removeAt(vertex_index);
    updateCanvas();
    return true;
}

std::optional<CoordinateWGS84> MapCanvasPipes::pipeVertexCoordinate(const QUuid &pipe_uuid,
                                                                    int vertex_index) const
{
    const PipeCanvasItem *pipe = pipeByUuid(pipe_uuid);
    if (!pipe || vertex_index < 0 || vertex_index >= pipe->geometry.intermediate_vertices.size())
        return std::nullopt;
    
    return pipe->geometry.intermediate_vertices[vertex_index];
}

bool MapCanvasPipes::splitPipeAtVertex(const QUuid &pipe_uuid,
                                       int vertex_index,
                                       const InfrastructureEntityReference &junction_reference,
                                       MapEntityMarkerLabel *junction_label)
{
    const int pipe_index = pipeIndexByUuid(pipe_uuid);
    if (pipe_index < 0 || !junction_label)
        return false;
    
    const PipeCanvasItem original_pipe = this->list_pipes[pipe_index];
    if (vertex_index < 0 ||
        vertex_index >= original_pipe.geometry.intermediate_vertices.size() ||
        !original_pipe.start_label ||
        !original_pipe.end_label)
    {
        return false;
    }
    
    PipeCanvasItem first_pipe = original_pipe;
    first_pipe.geometry.end_node = junction_reference;
    first_pipe.geometry.intermediate_vertices.clear();
    first_pipe.end_label = junction_label;
    first_pipe.selected = false;
    
    for (int i = 0; i < vertex_index; i++)
        first_pipe.geometry.intermediate_vertices.append(original_pipe.geometry.intermediate_vertices[i]);
    
    PipeCanvasItem second_pipe;
    second_pipe.entity.type = InfrastructureEntity::Pipe;
    second_pipe.entity.uuid = QUuid::createUuid();
    second_pipe.geometry.start_node = junction_reference;
    second_pipe.geometry.end_node = original_pipe.geometry.end_node;
    second_pipe.start_label = junction_label;
    second_pipe.end_label = original_pipe.end_label;
    
    for (int i = vertex_index + 1; i < original_pipe.geometry.intermediate_vertices.size(); i++)
        second_pipe.geometry.intermediate_vertices.append(original_pipe.geometry.intermediate_vertices[i]);
    
    this->list_pipes[pipe_index] = first_pipe;
    this->list_pipes.insert(pipe_index + 1, second_pipe);
    
    if (this->pipe_vertex_move_pipe_uuid.has_value() &&
        this->pipe_vertex_move_pipe_uuid.value() == pipe_uuid)
    {
        cancelPipeVertexMove();
    }
    
    updateCanvas();
    return true;
}

bool MapCanvasPipes::startPipeVertexMove(const QUuid &pipe_uuid, int vertex_index)
{
    PipeCanvasItem *pipe = pipeByUuid(pipe_uuid);
    if (!pipe || vertex_index < 0 || vertex_index >= pipe->geometry.intermediate_vertices.size())
        return false;
    
    this->pipe_vertex_move_pipe_uuid = pipe_uuid;
    this->pipe_vertex_move_index = vertex_index;
    return true;
}

bool MapCanvasPipes::isPipeVertexMoveActive() const
{
    return this->pipe_vertex_move_pipe_uuid.has_value();
}

bool MapCanvasPipes::updatePipeVertexMove(const QPointF &screen_position)
{
    if (!this->pipe_vertex_move_pipe_uuid.has_value())
        return false;
    
    PipeCanvasItem *pipe = pipeByUuid(this->pipe_vertex_move_pipe_uuid.value());
    if (!pipe ||
        this->pipe_vertex_move_index < 0 ||
        this->pipe_vertex_move_index >= pipe->geometry.intermediate_vertices.size())
    {
        cancelPipeVertexMove();
        return false;
    }
    
    pipe->geometry.intermediate_vertices[this->pipe_vertex_move_index] =
        this->map_model->wgs84FromScreen(screen_position.toPoint(), this->map_canvas->size());
    updateCanvas();
    return true;
}

bool MapCanvasPipes::finishPipeVertexMove(const QPointF &screen_position)
{
    const bool updated = updatePipeVertexMove(screen_position);
    cancelPipeVertexMove();
    return updated;
}

void MapCanvasPipes::cancelPipeVertexMove()
{
    this->pipe_vertex_move_pipe_uuid.reset();
    this->pipe_vertex_move_index = -1;
}

void MapCanvasPipes::removeConnectedToLabel(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    for (int i = this->list_pipes.size() - 1; i >= 0; i--)
    {
        const PipeCanvasItem &pipe = this->list_pipes[i];
        if (pipe.start_label != label && pipe.end_label != label)
            continue;
        
        if (this->pipe_vertex_move_pipe_uuid.has_value() &&
            this->pipe_vertex_move_pipe_uuid.value() == pipe.entity.uuid)
        {
            cancelPipeVertexMove();
        }
        
        this->list_pipes.removeAt(i);
    }
    
    updateCanvas();
}

MapCanvasPipes::PipeCanvasItem *MapCanvasPipes::pipeByUuid(const QUuid &pipe_uuid)
{
    for (PipeCanvasItem &pipe : this->list_pipes)
    {
        if (pipe.entity.uuid == pipe_uuid)
            return &pipe;
    }
    
    return nullptr;
}

const MapCanvasPipes::PipeCanvasItem *MapCanvasPipes::pipeByUuid(const QUuid &pipe_uuid) const
{
    for (const PipeCanvasItem &pipe : this->list_pipes)
    {
        if (pipe.entity.uuid == pipe_uuid)
            return &pipe;
    }
    
    return nullptr;
}

int MapCanvasPipes::pipeIndexByUuid(const QUuid &pipe_uuid) const
{
    for (int i = 0; i < this->list_pipes.size(); i++)
    {
        if (this->list_pipes[i].entity.uuid == pipe_uuid)
            return i;
    }
    
    return -1;
}

MapEntityMarker MapCanvasPipes::markerByLabel(
    MapEntityMarkerLabel *label,
    const QList<MapEntityMarker> &markers) const
{
    for (const MapEntityMarker &marker : markers)
    {
        if (marker.label == label)
            return marker;
    }
    
    InfrastructureEntityReference reference;
    reference.type = InfrastructureEntity::Unknown;
    MapEntityMarker marker;
    marker.entity = reference;
    return marker;
}

bool MapCanvasPipes::markerIsSelected(
    MapEntityMarkerLabel *label,
    const QList<MapEntityMarker> &selected_markers) const
{
    if (!label)
        return false;
    
    for (const MapEntityMarker &marker : selected_markers)
    {
        if (marker.label == label)
            return true;
    }
    
    return false;
}

void MapCanvasPipes::updateCanvas()
{
    if (this->map_canvas)
        this->map_canvas->update();
}
