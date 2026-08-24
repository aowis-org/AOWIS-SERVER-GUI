#include "map_rhi_scene.h"

#include "map_render_cache_math.h"
#include "../geo_web_mercator.h"
#include "../network_symbology_rendering.h"

#include <cmath>

namespace
{
quint64 entityRenderKey(InfrastructureEntity entity_type, quint32 render_id)
{
    return (quint64(quint32(int(entity_type))) << 32) | quint64(render_id);
}

bool finiteCoordinate(const CoordinateWGS84 &coordinate)
{
    return std::isfinite(coordinate.longitude_deg) && std::isfinite(coordinate.latitude_deg);
}

constexpr qreal FlowDirectionMinimumLinkPixels = 18.0;
constexpr qreal FlowDirectionSpacingPixels = 100.0;
constexpr qreal FlowDirectionChevronHalfWidthRatio = 0.4;
constexpr qreal FlowDirectionStrokeWidthRatio = 0.2;
constexpr int FlowDirectionMaximumMarkersPerLink = 32;
}

void MapRhiScene::setNetworkSnapshot(const NetworkRenderSnapshot &snapshot)
{
    this->link_vertices.clear();
    this->node_vertices.clear();
    this->selected_link_vertices.clear();
    this->selected_node_vertices.clear();
    this->diagnostic_link_vertices.clear();
    this->diagnostic_node_vertices.clear();
    this->flow_direction_vertices.clear();
    this->link_paths.clear();
    this->entity_keys_by_uuid.clear();
    this->link_vertex_indices_by_entity.clear();
    this->node_vertex_indices_by_entity.clear();
    this->geometry_revision = snapshot.geometry_revision;
    this->origin_world = chooseOriginWorld(snapshot);
    this->origin_valid = !snapshot.nodes.isEmpty() || !snapshot.links.isEmpty();

    if (!this->origin_valid)
        return;

    this->node_vertices.reserve(snapshot.nodes.size() * 6);
    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        if (!finiteCoordinate(node.coordinate_wgs84))
            continue;

        double resolved_x = this->origin_world.x();
        const QPointF center = localWorldPosition(
            node.coordinate_wgs84, this->origin_world.x(), &resolved_x);
        this->entity_keys_by_uuid.insert(
            node.uuid, entityRenderKey(node.entity_type, node.render_id));
        appendNode(node.entity_type, node.render_id, center);
    }

    qsizetype segment_count = 0;
    for (const NetworkRenderLink &link : snapshot.links)
        segment_count += qMax<qsizetype>(0, link.vertices_wgs84.size() - 1);
    this->link_vertices.reserve(segment_count * 6);

    for (const NetworkRenderLink &link : snapshot.links)
    {
        if (link.vertices_wgs84.size() < 2)
            continue;

        LinkPath link_path;
        link_path.entity_type = link.entity_type;
        link_path.render_id = link.render_id;

        bool have_previous = false;
        QPointF previous;
        double wrap_reference_x = this->origin_world.x();
        for (const CoordinateWGS84 &coordinate : link.vertices_wgs84)
        {
            if (!finiteCoordinate(coordinate))
            {
                have_previous = false;
                wrap_reference_x = this->origin_world.x();
                continue;
            }

            double resolved_x = wrap_reference_x;
            const QPointF current = localWorldPosition(
                coordinate, wrap_reference_x, &resolved_x);
            wrap_reference_x = resolved_x;

            if (have_previous)
            {
                appendLinkSegment(link.entity_type, link.render_id, previous, current);
                link_path.segments.append(QLineF(previous, current));
            }

            previous = current;
            have_previous = true;
        }

        this->entity_keys_by_uuid.insert(
            link.uuid, entityRenderKey(link.entity_type, link.render_id));
        if (!link_path.segments.isEmpty())
            this->link_paths.append(link_path);
    }

    rebuildFlowDirections();
    rebuildHighlights();
}


void MapRhiScene::setSymbology(const MapRhiSymbology &symbology)
{
    const bool link_colors_changed = this->symbology.link_colors != symbology.link_colors;
    const bool node_colors_changed = this->symbology.node_colors != symbology.node_colors;
    const bool link_thickness_changed =
        this->symbology.link_thickness_px != symbology.link_thickness_px;
    const bool flow_direction_changed =
        this->symbology.show_flow_direction != symbology.show_flow_direction
        || this->symbology.flow_direction_size_px != symbology.flow_direction_size_px
        || this->symbology.flow_directions != symbology.flow_directions
        || link_thickness_changed
        || link_colors_changed;

    this->symbology = symbology;

    if (link_colors_changed)
    {
        for (LinkVertex &vertex : this->link_vertices)
            applyLinkColor(&vertex);
    }
    if (node_colors_changed)
    {
        for (NodeVertex &vertex : this->node_vertices)
            applyNodeColor(&vertex);
    }
    if (flow_direction_changed)
        rebuildFlowDirections();
    if (link_thickness_changed)
        rebuildHighlights();
}

void MapRhiScene::setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (this->selected_entity_type == entity_type && this->selected_entity_uuid == uuid)
        return;

    this->selected_entity_type = entity_type;
    this->selected_entity_uuid = uuid;
    rebuildHighlights();
}

bool MapRhiScene::setViewZoom(int zoom)
{
    if (this->view_zoom == zoom)
        return false;

    this->view_zoom = zoom;
    rebuildFlowDirections();
    return true;
}

void MapRhiScene::setSimulationErrorEntities(
    const QHash<QUuid, InfrastructureEntity> &error_entities,
    const QSet<QUuid> &stale_entity_uuids)
{
    this->simulation_error_entities = error_entities;
    this->simulation_stale_entity_uuids = stale_entity_uuids;
    rebuildHighlights();
}

const QVector<MapRhiScene::LinkVertex> &MapRhiScene::linkVertices() const
{
    return this->link_vertices;
}

const QVector<MapRhiScene::NodeVertex> &MapRhiScene::nodeVertices() const
{
    return this->node_vertices;
}

const QVector<MapRhiScene::LinkVertex> &MapRhiScene::selectedLinkVertices() const
{
    return this->selected_link_vertices;
}

const QVector<MapRhiScene::NodeVertex> &MapRhiScene::selectedNodeVertices() const
{
    return this->selected_node_vertices;
}

const QVector<MapRhiScene::LinkVertex> &MapRhiScene::diagnosticLinkVertices() const
{
    return this->diagnostic_link_vertices;
}

const QVector<MapRhiScene::NodeVertex> &MapRhiScene::diagnosticNodeVertices() const
{
    return this->diagnostic_node_vertices;
}

const QVector<MapRhiScene::LinkVertex> &MapRhiScene::flowDirectionVertices() const
{
    return this->flow_direction_vertices;
}

QPointF MapRhiScene::originWorld() const
{
    return this->origin_world;
}

quint64 MapRhiScene::geometryRevision() const
{
    return this->geometry_revision;
}

bool MapRhiScene::hasGeometry() const
{
    return !this->link_vertices.isEmpty() || !this->node_vertices.isEmpty();
}

int MapRhiScene::nodeSizePercent() const
{
    return this->symbology.node_size_percent;
}

int MapRhiScene::linkThicknessPx() const
{
    return this->symbology.link_thickness_px;
}

QPointF MapRhiScene::chooseOriginWorld(const NetworkRenderSnapshot &snapshot) const
{
    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        if (!finiteCoordinate(node.coordinate_wgs84))
            continue;

        return GeoWebMercator::lonLatToWorldPixel(
            GeoWebMercator::normalizeLongitude(node.coordinate_wgs84.longitude_deg),
            node.coordinate_wgs84.latitude_deg,
            MapRenderCacheMath::ReferenceZoom);
    }

    for (const NetworkRenderLink &link : snapshot.links)
    {
        for (const CoordinateWGS84 &coordinate : link.vertices_wgs84)
        {
            if (!finiteCoordinate(coordinate))
                continue;

            return GeoWebMercator::lonLatToWorldPixel(
                GeoWebMercator::normalizeLongitude(coordinate.longitude_deg),
                coordinate.latitude_deg,
                MapRenderCacheMath::ReferenceZoom);
        }
    }

    return QPointF();
}

QPointF MapRhiScene::localWorldPosition(const CoordinateWGS84 &coordinate,
                                        double wrap_reference_x,
                                        double *resolved_world_x) const
{
    const QPointF raw_world = GeoWebMercator::lonLatToWorldPixel(
        GeoWebMercator::normalizeLongitude(coordinate.longitude_deg),
        coordinate.latitude_deg,
        MapRenderCacheMath::ReferenceZoom);
    const double wrapped_x = GeoWebMercator::nearestWrappedWorldPixelX(
        raw_world.x(), wrap_reference_x, MapRenderCacheMath::ReferenceZoom);

    if (resolved_world_x != nullptr)
        *resolved_world_x = wrapped_x;

    return QPointF(
        wrapped_x - this->origin_world.x(),
        raw_world.y() - this->origin_world.y());
}

void MapRhiScene::appendLinkSegment(
    InfrastructureEntity entity_type, quint32 render_id,
    const QPointF &start, const QPointF &end)
{
    const quint64 entity_key = entityRenderKey(entity_type, render_id);
    const float corners[6][2] = {
        {0.0f, -1.0f},
        {1.0f, -1.0f},
        {1.0f, 1.0f},
        {0.0f, -1.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };

    for (int index = 0; index < 6; ++index)
    {
        LinkVertex vertex;
        vertex.start_x = float(start.x());
        vertex.start_y = float(start.y());
        vertex.start_z = 0.0f;
        vertex.end_x = float(end.x());
        vertex.end_y = float(end.y());
        vertex.end_z = 0.0f;
        vertex.along = corners[index][0];
        vertex.side = corners[index][1];
        vertex.red = 0.05f;
        vertex.green = 0.05f;
        vertex.blue = 0.05f;
        vertex.render_id = render_id;
        vertex.entity_type = entity_type;
        applyLinkColor(&vertex);
        this->link_vertices.append(vertex);
        this->link_vertex_indices_by_entity[entity_key].append(this->link_vertices.size() - 1);
    }
}

void MapRhiScene::appendNode(
    InfrastructureEntity entity_type, quint32 render_id, const QPointF &center)
{
    const quint64 entity_key = entityRenderKey(entity_type, render_id);
    const float corners[6][2] = {
        {-1.0f, -1.0f},
        {1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, 1.0f}
    };

    for (int index = 0; index < 6; ++index)
    {
        NodeVertex vertex;
        vertex.center_x = float(center.x());
        vertex.center_y = float(center.y());
        vertex.center_z = 0.0f;
        vertex.corner_x = corners[index][0];
        vertex.corner_y = corners[index][1];
        vertex.red = 0.02f;
        vertex.green = 0.02f;
        vertex.blue = 0.02f;
        vertex.render_id = render_id;
        vertex.entity_type = entity_type;
        applyNodeColor(&vertex);
        this->node_vertices.append(vertex);
        this->node_vertex_indices_by_entity[entity_key].append(this->node_vertices.size() - 1);
    }
}

void MapRhiScene::applyLinkColor(LinkVertex *vertex) const
{
    if (vertex == nullptr)
        return;

    const QRgb color = this->symbology.link_colors.value(
        vertex->render_id, networkSymbologyDefaultColor());
    vertex->red = qRed(color) / 255.0f;
    vertex->green = qGreen(color) / 255.0f;
    vertex->blue = qBlue(color) / 255.0f;
    vertex->alpha = qAlpha(color) / 255.0f;
}

void MapRhiScene::applyNodeColor(NodeVertex *vertex) const
{
    if (vertex == nullptr)
        return;

    const QRgb color = this->symbology.node_colors.value(
        vertex->render_id, networkSymbologyDefaultColor());
    vertex->red = qRed(color) / 255.0f;
    vertex->green = qGreen(color) / 255.0f;
    vertex->blue = qBlue(color) / 255.0f;
    vertex->alpha = qAlpha(color) / 255.0f;
}

void MapRhiScene::rebuildFlowDirections()
{
    this->flow_direction_vertices.clear();

    if (!this->symbology.show_flow_direction
        || this->symbology.flow_direction_size_px <= 0
        || this->symbology.flow_directions.isEmpty())
    {
        return;
    }

    const qreal scale = GeoWebMercator::zoomScale(
        this->view_zoom, MapRenderCacheMath::ReferenceZoom);
    if (!std::isfinite(scale) || scale <= 0.0)
        return;

    qsizetype estimated_marker_count = 0;
    for (const LinkPath &path : this->link_paths)
    {
        qreal total_world_length = 0.0;
        for (const QLineF &segment : path.segments)
            total_world_length += segment.length();

        const qreal total_screen_length = total_world_length * scale;
        if (total_screen_length < FlowDirectionMinimumLinkPixels)
            continue;

        estimated_marker_count += qBound(
            1, int(std::floor(total_screen_length / FlowDirectionSpacingPixels)),
            FlowDirectionMaximumMarkersPerLink);
    }
    this->flow_direction_vertices.reserve(estimated_marker_count * 12);

    for (const LinkPath &path : this->link_paths)
    {
        const qint8 flow_direction =
            this->symbology.flow_directions.value(path.render_id, 0);
        if (flow_direction == 0 || path.segments.isEmpty())
            continue;

        qreal total_world_length = 0.0;
        for (const QLineF &segment : path.segments)
            total_world_length += segment.length();

        const qreal total_screen_length = total_world_length * scale;
        if (total_screen_length < FlowDirectionMinimumLinkPixels || total_world_length <= 0.0)
            continue;

        const int marker_count = qBound(
            1, int(std::floor(total_screen_length / FlowDirectionSpacingPixels)),
            FlowDirectionMaximumMarkersPerLink);
        const qreal chevron_length_world =
            qreal(this->symbology.flow_direction_size_px) / scale;
        const qreal chevron_half_width_world =
            chevron_length_world * FlowDirectionChevronHalfWidthRatio;
        const qreal stroke_width_px = qMax<qreal>(
            1.0, qreal(this->symbology.flow_direction_size_px)
                * FlowDirectionStrokeWidthRatio);
        const float half_stroke_px = float(stroke_width_px / 2.0);
        const QRgb arrow_color = flowDirectionColor(path.render_id);

        for (int marker_index = 0; marker_index < marker_count; ++marker_index)
        {
            qreal target_world_distance = total_world_length
                * qreal(marker_index + 1) / qreal(marker_count + 1);
            if (marker_count == 1 && path.entity_type != InfrastructureEntity::Pipe)
                target_world_distance = total_world_length * 0.3;

            qreal traversed_world_distance = 0.0;
            for (const QLineF &segment : path.segments)
            {
                const qreal segment_world_length = segment.length();
                if (segment_world_length <= 0.0)
                    continue;
                if (traversed_world_distance + segment_world_length < target_world_distance)
                {
                    traversed_world_distance += segment_world_length;
                    continue;
                }

                const qreal ratio = qBound<qreal>(
                    0.0,
                    (target_world_distance - traversed_world_distance) / segment_world_length,
                    1.0);
                const QPointF direction_base(
                    segment.dx() / segment_world_length,
                    segment.dy() / segment_world_length);
                QPointF direction = direction_base;
                if (flow_direction < 0)
                    direction *= -1.0;

                const QPointF center(
                    segment.x1() + segment.dx() * ratio,
                    segment.y1() + segment.dy() * ratio);
                const QPointF normal(-direction.y(), direction.x());
                const QPointF tip =
                    center + direction * (chevron_length_world / 2.0);
                const QPointF base =
                    center - direction * (chevron_length_world / 2.0);
                const QPointF tail_first =
                    base + normal * chevron_half_width_world;
                const QPointF tail_second =
                    base - normal * chevron_half_width_world;

                appendFlowDirectionStroke(
                    tail_first, tip, arrow_color, half_stroke_px);
                appendFlowDirectionStroke(
                    tail_second, tip, arrow_color, half_stroke_px);
                break;
            }
        }
    }
}

void MapRhiScene::appendFlowDirectionStroke(
    const QPointF &start, const QPointF &end, QRgb color, float half_width_px)
{
    const float corners[6][2] = {
        {0.0f, -1.0f},
        {1.0f, -1.0f},
        {1.0f, 1.0f},
        {0.0f, -1.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };
    const float base_half_width_px = float(this->symbology.link_thickness_px) / 2.0f;

    for (int index = 0; index < 6; ++index)
    {
        LinkVertex vertex;
        vertex.start_x = float(start.x());
        vertex.start_y = float(start.y());
        vertex.start_z = 0.0f;
        vertex.end_x = float(end.x());
        vertex.end_y = float(end.y());
        vertex.end_z = 0.0f;
        vertex.along = corners[index][0];
        vertex.side = corners[index][1];
        vertex.red = qRed(color) / 255.0f;
        vertex.green = qGreen(color) / 255.0f;
        vertex.blue = qBlue(color) / 255.0f;
        vertex.alpha = qAlpha(color) / 255.0f;
        vertex.size_adjust_px = half_width_px - base_half_width_px;
        this->flow_direction_vertices.append(vertex);
    }
}

QRgb MapRhiScene::flowDirectionColor(quint32 render_id) const
{
    const QRgb link_color = this->symbology.link_colors.value(
        render_id, networkSymbologyDefaultColor());
    const int red = qRed(link_color);
    const int green = qGreen(link_color);
    const int blue = qBlue(link_color);
    const double luminance =
        0.2126 * double(red) + 0.7152 * double(green) + 0.0722 * double(blue);
    return luminance >= 150.0 ? qRgb(0, 0, 0) : qRgb(255, 255, 255);
}

void MapRhiScene::rebuildHighlights()
{
    this->selected_link_vertices.clear();
    this->selected_node_vertices.clear();
    this->diagnostic_link_vertices.clear();
    this->diagnostic_node_vertices.clear();

    const bool selected_has_error = !this->selected_entity_uuid.isNull()
        && this->simulation_error_entities.value(
            this->selected_entity_uuid, InfrastructureEntity::Unknown) == this->selected_entity_type;

    if (!this->selected_entity_uuid.isNull()
        && this->selected_entity_type != InfrastructureEntity::Unknown)
    {
        const QHash<QUuid, quint64>::const_iterator selected_iterator =
            this->entity_keys_by_uuid.constFind(this->selected_entity_uuid);
        if (selected_iterator != this->entity_keys_by_uuid.cend())
        {
            const quint64 expected_key = entityRenderKey(
                this->selected_entity_type, quint32(selected_iterator.value() & 0xffffffffULL));
            if (expected_key == selected_iterator.value())
            {
                const float base_link_width = float(this->symbology.link_thickness_px);
                const float selected_link_width = qMax(
                    3.0f, base_link_width + (selected_has_error ? 6.0f : 2.0f));
                appendEntityHighlight(
                    this->selected_entity_type,
                    quint32(selected_iterator.value() & 0xffffffffULL),
                    QColor(0, 190, 255),
                    (selected_link_width - base_link_width) / 2.0f,
                    selected_has_error ? 5.0f : 2.0f,
                    &this->selected_link_vertices,
                    &this->selected_node_vertices);
            }
        }
    }

    for (QHash<QUuid, InfrastructureEntity>::const_iterator error_iterator =
             this->simulation_error_entities.cbegin();
         error_iterator != this->simulation_error_entities.cend(); ++error_iterator)
    {
        const QHash<QUuid, quint64>::const_iterator entity_iterator =
            this->entity_keys_by_uuid.constFind(error_iterator.key());
        if (entity_iterator == this->entity_keys_by_uuid.cend())
            continue;

        const InfrastructureEntity entity_type = error_iterator.value();
        const quint32 render_id = quint32(entity_iterator.value() & 0xffffffffULL);
        if (entityRenderKey(entity_type, render_id) != entity_iterator.value())
            continue;

        const float base_link_width = float(this->symbology.link_thickness_px);
        const float diagnostic_link_width = qMax(3.0f, base_link_width + 2.0f);
        const QColor color = this->simulation_stale_entity_uuids.contains(error_iterator.key())
            ? QColor(128, 128, 128)
            : QColor(255, 0, 0);
        appendEntityHighlight(
            entity_type, render_id, color,
            (diagnostic_link_width - base_link_width) / 2.0f, 2.0f,
            &this->diagnostic_link_vertices,
            &this->diagnostic_node_vertices);
    }
}

void MapRhiScene::appendEntityHighlight(
    InfrastructureEntity entity_type, quint32 render_id, const QColor &color,
    float link_size_adjust_px, float node_size_adjust_px,
    QVector<LinkVertex> *link_target, QVector<NodeVertex> *node_target) const
{
    const quint64 key = entityRenderKey(entity_type, render_id);
    if (link_target != nullptr)
    {
        const QVector<int> link_indices = this->link_vertex_indices_by_entity.value(key);
        link_target->reserve(link_target->size() + link_indices.size());
        for (int vertex_index : link_indices)
        {
            if (vertex_index < 0 || vertex_index >= this->link_vertices.size())
                continue;

            LinkVertex vertex = this->link_vertices.at(vertex_index);
            vertex.red = color.redF();
            vertex.green = color.greenF();
            vertex.blue = color.blueF();
            vertex.alpha = color.alphaF();
            vertex.size_adjust_px = link_size_adjust_px;
            link_target->append(vertex);
        }
    }

    if (node_target != nullptr)
    {
        const QVector<int> node_indices = this->node_vertex_indices_by_entity.value(key);
        node_target->reserve(node_target->size() + node_indices.size());
        for (int vertex_index : node_indices)
        {
            if (vertex_index < 0 || vertex_index >= this->node_vertices.size())
                continue;

            NodeVertex vertex = this->node_vertices.at(vertex_index);
            vertex.red = color.redF();
            vertex.green = color.greenF();
            vertex.blue = color.blueF();
            vertex.alpha = color.alphaF();
            vertex.size_adjust_px = node_size_adjust_px;
            node_target->append(vertex);
        }
    }
}
