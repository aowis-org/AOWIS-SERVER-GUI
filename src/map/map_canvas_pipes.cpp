#include "map_canvas_pipes.h"
#include "map_canvas_widget.h"

#include "../geo_web_mercator.h"
#include "../infrastructure_entity_traits.h"
#ifdef Q_OS_WASM
#include "../widgets/wasm_popup_menu.h"
#endif

#include <QAction>
#include <QHash>
#include <QMenu>
#include <QMessageBox>

#include <algorithm>

namespace
{
constexpr double link_hit_distance = 7.0;
constexpr double pipe_vertex_hit_distance = 9.0;

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

MapCanvasPipes::MapCanvasPipes(MapModel *map_model, MapCanvasWidget *map_canvas,
                               QObject *parent)
    : QObject(parent), map_model(map_model), map_canvas(map_canvas)
{
}

void MapCanvasPipes::setWrapReferenceLongitude(double longitude)
{
    this->wrap_reference_lon = GeoWebMercator::normalizeLongitude(longitude);
}

QPointF MapCanvasPipes::screenFromWgs84(const CoordinateWGS84 &coordinate) const
{
    return this->map_model->screenFromWgs84(
        coordinate, this->map_canvas->size(), this->wrap_reference_lon);
}

void MapCanvasPipes::clear()
{
    clearPlacement();
    cancelPipeVertexMove();
    this->list_pipes.clear();
    this->pipe_indices_by_uuid.clear();
    updateCanvas();
}

void MapCanvasPipes::clearPlacement()
{
    this->pipe_start_node_uuid = QUuid();
    this->pipe_intermediate_vertices.clear();
}

bool MapCanvasPipes::hasStartNode() const
{
    return !this->pipe_start_node_uuid.isNull();
}

QUuid MapCanvasPipes::startNodeUuid() const
{
    return this->pipe_start_node_uuid;
}

void MapCanvasPipes::startPipe(const QUuid &start_node_uuid)
{
    this->pipe_start_node_uuid = start_node_uuid;
    this->pipe_intermediate_vertices.clear();
    updateCanvas();
}

void MapCanvasPipes::appendIntermediateVertex(const CoordinateWGS84 &coordinate)
{
    this->pipe_intermediate_vertices.append(coordinate);
    updateCanvas();
}

QList<CoordinateWGS84> MapCanvasPipes::intermediateVertices() const
{
    return this->pipe_intermediate_vertices;
}

QList<CoordinateWGS84> MapCanvasPipes::intermediateVertices(const QUuid &pipe_uuid) const
{
    const PipeCanvasItem *pipe = pipeByUuid(pipe_uuid);
    if (!pipe)
        return QList<CoordinateWGS84>();
    return pipe->geometry.intermediate_vertices;
}

bool MapCanvasPipes::addPipe(const InfrastructureEntityReference &pipe_reference,
                             const InfrastructureEntityReference &start_node,
                             const InfrastructureEntityReference &end_node,
                             const QList<CoordinateWGS84> &intermediate_vertices)
{
    if (start_node.uuid.isNull() || end_node.uuid.isNull() || start_node.uuid == end_node.uuid ||
        pipe_reference.type != InfrastructureEntity::Pipe || pipe_reference.uuid.isNull() ||
        !InfrastructureEntityTraits::isHydraulicConnectionNode(start_node.type) ||
        !InfrastructureEntityTraits::isHydraulicConnectionNode(end_node.type))
    {
        return false;
    }

    PipeCanvasItem pipe;
    pipe.entity = pipe_reference;
    pipe.geometry.start_node = start_node;
    pipe.geometry.end_node = end_node;
    pipe.geometry.intermediate_vertices = intermediate_vertices;
    this->list_pipes.append(pipe);
    this->pipe_indices_by_uuid.insert(pipe_reference.uuid, this->list_pipes.size() - 1);
    updateCanvas();
    return true;
}

bool MapCanvasPipes::completePipe(const InfrastructureEntityReference &pipe_reference,
                                  const InfrastructureEntityReference &start_node,
                                  const InfrastructureEntityReference &end_node)
{
    if (this->pipe_start_node_uuid.isNull() || this->pipe_start_node_uuid != start_node.uuid)
        return false;

    const bool added = addPipe(pipe_reference, start_node, end_node,
                               this->pipe_intermediate_vertices);
    if (added)
        clearPlacement();
    return added;
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

QList<QUuid> MapCanvasPipes::selectedPipeUuids() const
{
    QList<QUuid> uuids;
    for (const PipeCanvasItem &pipe : this->list_pipes)
    {
        if (pipe.selected)
            uuids.append(pipe.entity.uuid);
    }
    return uuids;
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

std::optional<InfrastructureEntityReference> MapCanvasPipes::togglePipe(const QUuid &pipe_uuid)
{
    PipeCanvasItem *pipe = pipeByUuid(pipe_uuid);
    if (!pipe)
        return std::nullopt;

    pipe->selected = !pipe->selected;
    updateCanvas();
    return pipe->entity;
}

bool MapCanvasPipes::removePipe(const QUuid &pipe_uuid)
{
    const int pipe_index = pipeIndexByUuid(pipe_uuid);
    if (pipe_index < 0)
        return false;

    if (this->pipe_vertex_move_pipe_uuid.has_value() &&
        this->pipe_vertex_move_pipe_uuid.value() == pipe_uuid)
    {
        cancelPipeVertexMove();
    }

    this->list_pipes.removeAt(pipe_index);
    rebuildUuidIndex();
    updateCanvas();
    return true;
}

void MapCanvasPipes::selectPipesWithSelectedEndpoints(
    const QList<QUuid> &selected_marker_uuids)
{
    QSet<QUuid> selected_marker_set;
    selected_marker_set.reserve(selected_marker_uuids.size());
    for (const QUuid &uuid : selected_marker_uuids)
        selected_marker_set.insert(uuid);

    for (PipeCanvasItem &pipe : this->list_pipes)
    {
        if (selected_marker_set.contains(pipe.geometry.start_node.uuid) &&
            selected_marker_set.contains(pipe.geometry.end_node.uuid))
        {
            pipe.selected = true;
        }
    }
}

void MapCanvasPipes::moveIntermediateVertices(const QList<QUuid> &pipe_uuids,
                                               double longitude_delta,
                                               double latitude_delta)
{
    for (const QUuid &pipe_uuid : pipe_uuids)
    {
        PipeCanvasItem *pipe = pipeByUuid(pipe_uuid);
        if (!pipe)
            continue;
        for (CoordinateWGS84 &vertex : pipe->geometry.intermediate_vertices)
        {
            vertex.latitude_deg = std::clamp(
                vertex.latitude_deg + latitude_delta,
                -GeoWebMercator::MaximumLatitude, GeoWebMercator::MaximumLatitude);
            vertex.longitude_deg = GeoWebMercator::normalizeLongitude(
                vertex.longitude_deg + longitude_delta);
        }
    }
}

std::optional<PipeGeometry> MapCanvasPipes::geometryByUuid(const QUuid &pipe_uuid) const
{
    const PipeCanvasItem *pipe = pipeByUuid(pipe_uuid);
    if (!pipe)
        return std::nullopt;
    return pipe->geometry;
}

QList<QUuid> MapCanvasPipes::connectedPipeUuids(const QSet<QUuid> &node_uuids) const
{
    QList<QUuid> result;
    for (const PipeCanvasItem &pipe : this->list_pipes)
    {
        if (node_uuids.contains(pipe.geometry.start_node.uuid) ||
            node_uuids.contains(pipe.geometry.end_node.uuid))
        {
            result.append(pipe.entity.uuid);
        }
    }
    return result;
}

std::optional<InfrastructureEntityReference> MapCanvasPipes::pipeAt(
    const QPointF &position, const QList<MapEntityMarker> &markers) const
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
            const QPointF vertex_point = screenFromWgs84(pipe.geometry.intermediate_vertices[i]);
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
    const QPointF &position, const QList<MapEntityMarker> &markers) const
{
    PipeSegmentHit hit;
    double nearest_distance_squared = link_hit_distance * link_hit_distance;

    QHash<QUuid, const MapEntityMarker *> markers_by_uuid;
    markers_by_uuid.reserve(markers.size());
    for (const MapEntityMarker &marker : markers)
        markers_by_uuid.insert(marker.entity.uuid, &marker);

    const double hit_min_x = position.x() - link_hit_distance;
    const double hit_max_x = position.x() + link_hit_distance;
    const double hit_min_y = position.y() - link_hit_distance;
    const double hit_max_y = position.y() + link_hit_distance;

    for (const PipeCanvasItem &pipe : this->list_pipes)
    {
        const MapEntityMarker *start_marker = markers_by_uuid.value(
            pipe.geometry.start_node.uuid, nullptr);
        const MapEntityMarker *end_marker = markers_by_uuid.value(
            pipe.geometry.end_node.uuid, nullptr);
        if (!start_marker || !end_marker)
            continue;

        QPointF previous_point = screenFromWgs84(start_marker->coord_wgs84);
        for (int i = 0; i < pipe.geometry.intermediate_vertices.size(); i++)
        {
            const QPointF vertex_point = screenFromWgs84(pipe.geometry.intermediate_vertices[i]);
            const double segment_min_x = qMin(previous_point.x(), vertex_point.x());
            const double segment_max_x = qMax(previous_point.x(), vertex_point.x());
            const double segment_min_y = qMin(previous_point.y(), vertex_point.y());
            const double segment_max_y = qMax(previous_point.y(), vertex_point.y());

            if (segment_max_x >= hit_min_x && segment_min_x <= hit_max_x &&
                segment_max_y >= hit_min_y && segment_min_y <= hit_max_y)
            {
                const QPointF nearest_point = nearestPointOnSegment(
                    position, previous_point, vertex_point);
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
            }

            previous_point = vertex_point;
        }

        const QPointF end_point = screenFromWgs84(end_marker->coord_wgs84);
        const double segment_min_x = qMin(previous_point.x(), end_point.x());
        const double segment_max_x = qMax(previous_point.x(), end_point.x());
        const double segment_min_y = qMin(previous_point.y(), end_point.y());
        const double segment_max_y = qMax(previous_point.y(), end_point.y());
        if (segment_max_x < hit_min_x || segment_min_x > hit_max_x ||
            segment_max_y < hit_min_y || segment_min_y > hit_max_y)
        {
            continue;
        }

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

bool MapCanvasPipes::showContextMenuAt(const QPointF &position,
                                       const QPoint &global_position,
                                       const QList<MapEntityMarker> &markers)
{
    const PipeVertexHit vertex_hit = pipeVertexAt(position);
    if (vertex_hit.isValid())
    {
        const QUuid pipe_uuid = vertex_hit.pipe_uuid;
        const int vertex_index = vertex_hit.vertex_index;
        emit pipeSelectionRequested(pipe_uuid);

#ifdef Q_OS_WASM
        WasmPopupMenu *menu = new WasmPopupMenu(this->map_canvas);
        menu->setDeleteOnClose(true);
        menu->addAction(QStringLiteral("Move vertex"), [this, pipe_uuid, vertex_index]
        {
            emit pipeVertexMoveRequested(pipe_uuid, vertex_index);
        });
        menu->addAction(QStringLiteral("Delete vertex"), [this, pipe_uuid, vertex_index]
        {
            emit pipeVertexDeleteRequested(pipe_uuid, vertex_index);
        });
        menu->addAction(QStringLiteral("Convert to junction"), [this, pipe_uuid, vertex_index]
        {
            QMessageBox *message_box = new QMessageBox(
                QMessageBox::Question, QStringLiteral("Convert pipe vertex"),
                QStringLiteral("Do you really want to convert this pipe vertex to a junction?"),
                QMessageBox::Yes | QMessageBox::No, this->map_canvas);
            message_box->setDefaultButton(QMessageBox::No);
            connect(message_box, &QMessageBox::finished, this,
                    [this, pipe_uuid, vertex_index](int result)
            {
                if (result == QMessageBox::Yes)
                    emit pipeVertexConversionRequested(pipe_uuid, vertex_index);
            });
            connect(message_box, &QMessageBox::finished,
                    message_box, &QObject::deleteLater);
            message_box->open();
        });
        menu->popup(global_position);
#else
        QMenu *menu = new QMenu(this->map_canvas);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        QAction *action_move = menu->addAction("Move vertex");
        QAction *action_delete = menu->addAction("Delete vertex");
        QAction *action_convert_to_junction = menu->addAction("Convert to junction");

        connect(action_move, &QAction::triggered, this, [this, pipe_uuid, vertex_index]()
        {
            emit pipeVertexMoveRequested(pipe_uuid, vertex_index);
        });
        connect(action_delete, &QAction::triggered, this, [this, pipe_uuid, vertex_index]()
        {
            emit pipeVertexDeleteRequested(pipe_uuid, vertex_index);
        });
        connect(action_convert_to_junction, &QAction::triggered, this,
                [this, pipe_uuid, vertex_index]()
        {
            QMessageBox *message_box = new QMessageBox(
                QMessageBox::Question, "Convert pipe vertex",
                "Do you really want to convert this pipe vertex to a junction?",
                QMessageBox::Yes | QMessageBox::No, this->map_canvas);
            message_box->setDefaultButton(QMessageBox::No);
            connect(message_box, &QMessageBox::finished, this,
                    [this, pipe_uuid, vertex_index](int result)
            {
                if (result == QMessageBox::Yes)
                    emit pipeVertexConversionRequested(pipe_uuid, vertex_index);
            });
            connect(message_box, &QMessageBox::finished, message_box, &QObject::deleteLater);
            message_box->open();
        });

        menu->popup(global_position);
#endif
        return true;
    }

    const PipeSegmentHit segment_hit = pipeSegmentAt(position, markers);
    if (!segment_hit.isValid())
        return false;

    const CoordinateWGS84 coordinate = this->map_model->wgs84FromScreen(
        segment_hit.nearest_point.toPoint(), this->map_canvas->size());
    emit pipeSelectionRequested(segment_hit.pipe_uuid);
    emit pipeVertexAddRequested(segment_hit.pipe_uuid, segment_hit.insert_index, coordinate);
    return true;
}

bool MapCanvasPipes::addPipeVertex(const QUuid &pipe_uuid, int insert_index,
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

bool MapCanvasPipes::setIntermediateVertices(
    const QUuid &pipe_uuid, const QList<CoordinateWGS84> &intermediate_vertices)
{
    PipeCanvasItem *pipe = pipeByUuid(pipe_uuid);
    if (!pipe)
        return false;

    pipe->geometry.intermediate_vertices = intermediate_vertices;
    updateCanvas();
    return true;
}

std::optional<CoordinateWGS84> MapCanvasPipes::pipeVertexCoordinate(
    const QUuid &pipe_uuid, int vertex_index) const
{
    const PipeCanvasItem *pipe = pipeByUuid(pipe_uuid);
    if (!pipe || vertex_index < 0 || vertex_index >= pipe->geometry.intermediate_vertices.size())
        return std::nullopt;
    return pipe->geometry.intermediate_vertices[vertex_index];
}

bool MapCanvasPipes::splitPipeAtVertex(
    const QUuid &pipe_uuid, int vertex_index,
    const InfrastructureEntityReference &junction_reference,
    const InfrastructureEntityReference &second_pipe_reference)
{
    const int pipe_index = pipeIndexByUuid(pipe_uuid);
    if (pipe_index < 0 || junction_reference.uuid.isNull() ||
        second_pipe_reference.type != InfrastructureEntity::Pipe ||
        second_pipe_reference.uuid.isNull())
    {
        return false;
    }

    const PipeCanvasItem original_pipe = this->list_pipes[pipe_index];
    if (vertex_index < 0 || vertex_index >= original_pipe.geometry.intermediate_vertices.size())
        return false;

    PipeCanvasItem first_pipe = original_pipe;
    first_pipe.geometry.end_node = junction_reference;
    first_pipe.geometry.intermediate_vertices.clear();
    first_pipe.selected = false;
    for (int i = 0; i < vertex_index; i++)
        first_pipe.geometry.intermediate_vertices.append(
            original_pipe.geometry.intermediate_vertices[i]);

    PipeCanvasItem second_pipe;
    second_pipe.entity = second_pipe_reference;
    second_pipe.geometry.start_node = junction_reference;
    second_pipe.geometry.end_node = original_pipe.geometry.end_node;
    for (int i = vertex_index + 1; i < original_pipe.geometry.intermediate_vertices.size(); i++)
        second_pipe.geometry.intermediate_vertices.append(
            original_pipe.geometry.intermediate_vertices[i]);

    this->list_pipes[pipe_index] = first_pipe;
    this->list_pipes.insert(pipe_index + 1, second_pipe);
    rebuildUuidIndex();

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

std::optional<QUuid> MapCanvasPipes::activePipeVertexMoveUuid() const
{
    return this->pipe_vertex_move_pipe_uuid;
}

int MapCanvasPipes::activePipeVertexMoveIndex() const
{
    return this->pipe_vertex_move_index;
}

bool MapCanvasPipes::updatePipeVertexMove(const QPointF &screen_position)
{
    if (!this->pipe_vertex_move_pipe_uuid.has_value())
        return false;

    PipeCanvasItem *pipe = pipeByUuid(this->pipe_vertex_move_pipe_uuid.value());
    if (!pipe || this->pipe_vertex_move_index < 0 ||
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

void MapCanvasPipes::removeConnectedToUuid(const QUuid &uuid)
{
    if (uuid.isNull())
        return;

    for (int i = this->list_pipes.size() - 1; i >= 0; i--)
    {
        const PipeCanvasItem &pipe = this->list_pipes[i];
        if (pipe.geometry.start_node.uuid != uuid && pipe.geometry.end_node.uuid != uuid)
            continue;

        if (this->pipe_vertex_move_pipe_uuid.has_value() &&
            this->pipe_vertex_move_pipe_uuid.value() == pipe.entity.uuid)
        {
            cancelPipeVertexMove();
        }
        this->list_pipes.removeAt(i);
    }
    rebuildUuidIndex();
    updateCanvas();
}

MapCanvasPipes::PipeCanvasItem *MapCanvasPipes::pipeByUuid(const QUuid &pipe_uuid)
{
    const auto iterator = this->pipe_indices_by_uuid.constFind(pipe_uuid);
    if (iterator == this->pipe_indices_by_uuid.cend())
        return nullptr;
    return &this->list_pipes[iterator.value()];
}

const MapCanvasPipes::PipeCanvasItem *MapCanvasPipes::pipeByUuid(const QUuid &pipe_uuid) const
{
    const auto iterator = this->pipe_indices_by_uuid.constFind(pipe_uuid);
    if (iterator == this->pipe_indices_by_uuid.cend())
        return nullptr;
    return &this->list_pipes.at(iterator.value());
}

int MapCanvasPipes::pipeIndexByUuid(const QUuid &pipe_uuid) const
{
    const auto iterator = this->pipe_indices_by_uuid.constFind(pipe_uuid);
    return iterator == this->pipe_indices_by_uuid.cend() ? -1 : iterator.value();
}

void MapCanvasPipes::rebuildUuidIndex()
{
    this->pipe_indices_by_uuid.clear();
    this->pipe_indices_by_uuid.reserve(this->list_pipes.size());
    for (int i = 0; i < this->list_pipes.size(); ++i)
        this->pipe_indices_by_uuid.insert(this->list_pipes.at(i).entity.uuid, i);
}

void MapCanvasPipes::updateCanvas()
{
    emit signalCanvasUpdateRequested();
}
