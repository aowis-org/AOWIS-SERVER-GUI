#include "map_rhi_scene.h"

#include "map_render_cache_math.h"
#include "map_model.h"
#include "../geo_web_mercator.h"
#include "../infrastructure_entity_traits.h"
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
constexpr qreal FlowDirectionMinimumElevationPixels = 4.0;
constexpr int FlowDirectionMaximumMarkersPerLink = 32;
}

void MapRhiScene::setNetworkSnapshot(const NetworkRenderSnapshot &snapshot)
{
    this->network_snapshot = snapshot;
    rebuildNetworkGeometry();
}

bool MapRhiScene::setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids)
{
    if (this->hidden_entity_uuids == hidden_entity_uuids)
        return false;

    this->hidden_entity_uuids = hidden_entity_uuids;
    rebuildNetworkGeometry();
    return true;
}

void MapRhiScene::rebuildNetworkGeometry()
{
    this->link_vertices.clear();
    this->node_vertices.clear();
    this->selected_link_vertices.clear();
    this->selected_node_vertices.clear();
    this->diagnostic_link_vertices.clear();
    this->diagnostic_node_vertices.clear();
    this->flow_direction_vertices.clear();
    this->icon_vertices.clear();
    this->heatmap_vertices.clear();
    this->junction_instances.clear();
    this->heatmap_markers.clear();
    this->icon_markers.clear();
    this->junction_markers.clear();
    this->link_paths.clear();
    this->entity_keys_by_uuid.clear();
    this->link_vertex_indices_by_entity.clear();
    this->node_vertex_indices_by_entity.clear();
    this->geometry_revision = this->network_snapshot.geometry_revision;
    this->origin_world = chooseOriginWorld(this->network_snapshot);
    this->origin_valid =
        !this->network_snapshot.nodes.isEmpty() || !this->network_snapshot.links.isEmpty();

    if (!this->origin_valid)
        return;

    bool elevation_reference_initialized = false;
    for (const NetworkRenderNode &node : this->network_snapshot.nodes)
    {
        if (!finiteCoordinate(node.coordinate_wgs84) || !std::isfinite(node.elevation_m))
            continue;

        if (!elevation_reference_initialized)
        {
            this->reference_latitude_deg = node.coordinate_wgs84.latitude_deg;
            this->elevation_reference_m = node.elevation_m;
            elevation_reference_initialized = true;
        }
        else
        {
            this->elevation_reference_m = qMin(this->elevation_reference_m, node.elevation_m);
        }
    }
    if (!elevation_reference_initialized)
    {
        this->reference_latitude_deg = 0.0;
        this->elevation_reference_m = 0.0;
    }

    this->node_vertices.reserve(this->network_snapshot.nodes.size() * 6);
    for (const NetworkRenderNode &node : this->network_snapshot.nodes)
    {
        if (this->hidden_entity_uuids.contains(node.uuid) ||
            !finiteCoordinate(node.coordinate_wgs84))
        {
            continue;
        }

        double resolved_x = this->origin_world.x();
        const QPointF center = localWorldPosition(
            node.coordinate_wgs84, this->origin_world.x(), &resolved_x);
        const float center_z = localElevationWorld(node.elevation_m);
        this->entity_keys_by_uuid.insert(
            node.uuid, entityRenderKey(node.entity_type, node.render_id));
        appendNode(node.entity_type, node.render_id, center, center_z);
        HeatmapMarker heatmap_marker;
        heatmap_marker.render_id = node.render_id;
        heatmap_marker.center = center;
        this->heatmap_markers.append(heatmap_marker);
        if (mapRhiHasIcon(node.entity_type))
        {
            IconMarker marker;
            marker.entity_type = node.entity_type;
            marker.render_id = node.render_id;
            marker.center = center;
            marker.z = center_z;
            this->icon_markers.append(marker);
        }
        if (node.entity_type == InfrastructureEntity::Junction)
        {
            JunctionMarker marker;
            marker.render_id = node.render_id;
            marker.center = center;
            marker.z = center_z;
            this->junction_markers.append(marker);
        }
    }

    qsizetype segment_count = 0;
    for (const NetworkRenderLink &link : this->network_snapshot.links)
    {
        if (!this->hidden_entity_uuids.contains(link.uuid))
            segment_count += qMax<qsizetype>(0, link.vertices_wgs84.size() - 1);
    }
    this->link_vertices.reserve(segment_count * 6);

    for (const NetworkRenderLink &link : this->network_snapshot.links)
    {
        if (this->hidden_entity_uuids.contains(link.uuid) ||
            link.vertices_wgs84.size() < 2)
        {
            continue;
        }

        LinkPath link_path;
        link_path.entity_type = link.entity_type;
        link_path.render_id = link.render_id;

        bool have_previous = false;
        QPointF previous;
        float previous_z = 0.0f;
        double wrap_reference_x = this->origin_world.x();
        for (qsizetype vertex_index = 0;
             vertex_index < link.vertices_wgs84.size(); ++vertex_index)
        {
            const CoordinateWGS84 &coordinate = link.vertices_wgs84.at(vertex_index);
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
            const double elevation_m = vertex_index < link.elevations_m.size()
                ? link.elevations_m.at(vertex_index)
                : this->elevation_reference_m;
            const float current_z = localElevationWorld(elevation_m);

            if (have_previous)
            {
                appendLinkSegment(
                    link.entity_type, link.render_id,
                    previous, previous_z, current, current_z);
                SceneSegment segment;
                segment.start = previous;
                segment.end = current;
                segment.start_z = previous_z;
                segment.end_z = current_z;
                link_path.segments.append(segment);
            }

            previous = current;
            previous_z = current_z;
            have_previous = true;
        }

        this->entity_keys_by_uuid.insert(
            link.uuid, entityRenderKey(link.entity_type, link.render_id));
        if (!link_path.segments.isEmpty())
        {
            if (mapRhiHasIcon(link.entity_type))
            {
                qreal total_length = 0.0;
                for (const SceneSegment &segment : link_path.segments)
                    total_length += QLineF(segment.start, segment.end).length();

                if (total_length > 0.0)
                {
                    const qreal target = total_length / 2.0;
                    qreal traversed = 0.0;
                    for (const SceneSegment &segment : link_path.segments)
                    {
                        const QLineF segment_line(segment.start, segment.end);
                        const qreal segment_length = segment_line.length();
                        if (segment_length <= 0.0)
                            continue;
                        if (traversed + segment_length < target)
                        {
                            traversed += segment_length;
                            continue;
                        }

                        const qreal ratio = qBound<qreal>(
                            0.0, (target - traversed) / segment_length, 1.0);
                        IconMarker marker;
                        marker.entity_type = link.entity_type;
                        marker.render_id = link.render_id;
                        marker.center = QPointF(
                            segment.start.x() + (segment.end.x() - segment.start.x()) * ratio,
                            segment.start.y() + (segment.end.y() - segment.start.y()) * ratio);
                        marker.z = segment.start_z + (segment.end_z - segment.start_z) * float(ratio);
                        this->icon_markers.append(marker);
                        break;
                    }
                }
            }
            this->link_paths.append(link_path);
        }
    }

    rebuildHeatmap();
    rebuildIcons();
    rebuildTankInstances();
    rebuildJunctionInstances();
    rebuildFlowDirections();
    rebuildHighlights();
}

void MapRhiScene::setSymbology(const MapRhiSymbology &symbology)
{
    const bool link_colors_changed = this->symbology.link_colors != symbology.link_colors;
    const bool node_colors_changed = this->symbology.node_colors != symbology.node_colors;
    const bool icon_visibility_changed = this->symbology.show_icons != symbology.show_icons;
    const bool link_thickness_changed =
        this->symbology.link_thickness_unit != symbology.link_thickness_unit
        || this->symbology.link_thickness_px != symbology.link_thickness_px
        || this->symbology.link_thickness_m != symbology.link_thickness_m;
    const bool icon_changed =
        this->symbology.icon_size_percent != symbology.icon_size_percent
        || this->symbology.show_icons != symbology.show_icons
        || link_colors_changed || node_colors_changed;
    const bool heatmap_changed =
        this->symbology.visual_heatmap != symbology.visual_heatmap
        || this->symbology.heatmap_fractions != symbology.heatmap_fractions
        || this->symbology.heatmap_palette != symbology.heatmap_palette
        || this->symbology.heatmap_palette_flipped != symbology.heatmap_palette_flipped;
    const bool flow_direction_changed =
        this->symbology.show_flow_direction != symbology.show_flow_direction
        || this->symbology.flow_direction_size_px != symbology.flow_direction_size_px
        || this->symbology.flow_directions != symbology.flow_directions
        || link_thickness_changed
        || link_colors_changed;
    const bool junction_changed =
        this->symbology.node_size_unit != symbology.node_size_unit
        || this->symbology.node_size_px != symbology.node_size_px
        || this->symbology.node_size_m != symbology.node_size_m
        || node_colors_changed;

    this->symbology = symbology;

    if (link_colors_changed)
    {
        for (LinkVertex &vertex : this->link_vertices)
            applyLinkColor(&vertex);
    }
    if (node_colors_changed || icon_visibility_changed)
    {
        for (NodeVertex &vertex : this->node_vertices)
            applyNodeColor(&vertex);
    }
    if (heatmap_changed)
        rebuildHeatmap();
    if (icon_changed)
    {
        rebuildIcons();
        rebuildTankInstances();
    }
    if (junction_changed)
        rebuildJunctionInstances();
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
    rebuildTankInstances();
    rebuildJunctionInstances();
}

bool MapRhiScene::setViewZoom(int zoom)
{
    if (this->view_zoom == zoom)
        return false;

    this->view_zoom = zoom;
    rebuildIcons();
    rebuildTankInstances();
    rebuildJunctionInstances();
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

bool MapRhiScene::setUse3dTankModels(bool enabled)
{
    if (this->use_3d_tank_models == enabled)
        return false;

    this->use_3d_tank_models = enabled;
    rebuildIcons();
    rebuildTankInstances();
    return true;
}

bool MapRhiScene::setUse3dJunctionModels(bool enabled)
{
    if (this->use_3d_junction_models == enabled)
        return false;

    this->use_3d_junction_models = enabled;
    for (NodeVertex &vertex : this->node_vertices)
        applyNodeColor(&vertex);
    rebuildJunctionInstances();
    return true;
}

bool MapRhiScene::setNetworkGroundOffsetM(double offset_m)
{
    if (!std::isfinite(offset_m))
        return false;

    const double bounded_offset_m = qBound(
        MapModel::MinView3dNetworkGroundOffsetM,
        offset_m,
        MapModel::MaxView3dNetworkGroundOffsetM);
    if (qFuzzyCompare(1.0 + this->network_ground_offset_m, 1.0 + bounded_offset_m))
        return false;

    this->network_ground_offset_m = bounded_offset_m;
    rebuildNetworkGeometry();
    return true;
}

bool MapRhiScene::setVerticalExaggeration(double exaggeration)
{
    if (!std::isfinite(exaggeration))
        return false;

    const double bounded_exaggeration = qBound(
        MapModel::MinView3dVerticalExaggeration,
        exaggeration,
        MapModel::MaxView3dVerticalExaggeration);
    if (qFuzzyCompare(1.0 + this->vertical_exaggeration, 1.0 + bounded_exaggeration))
        return false;

    this->vertical_exaggeration = bounded_exaggeration;
    rebuildNetworkGeometry();
    return true;
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

const QVector<MapRhiScene::IconVertex> &MapRhiScene::iconVertices() const
{
    return this->icon_vertices;
}

const QVector<MapRhiScene::HeatmapVertex> &MapRhiScene::heatmapVertices() const
{
    return this->heatmap_vertices;
}

const QVector<MapRhiTankInstance> &MapRhiScene::tankInstances() const
{
    return this->tank_instances;
}

const QVector<MapRhiJunctionInstance> &MapRhiScene::junctionInstances() const
{
    return this->junction_instances;
}

QPointF MapRhiScene::originWorld() const
{
    return this->origin_world;
}

const NetworkRenderSnapshot &MapRhiScene::networkSnapshot() const
{
    return this->network_snapshot;
}

QVector3D MapRhiScene::worldPosition(
    const CoordinateWGS84 &coordinate, double elevation_m,
    double wrap_reference_x, double *resolved_world_x) const
{
    double resolved_x = wrap_reference_x;
    const QPointF position = localWorldPosition(
        coordinate, wrap_reference_x, &resolved_x);
    if (resolved_world_x != nullptr)
        *resolved_world_x = resolved_x;
    return QVector3D(
        float(position.x()), float(position.y()), localElevationWorld(elevation_m));
}

bool MapRhiScene::isEntityHidden(const QUuid &uuid) const
{
    return this->hidden_entity_uuids.contains(uuid);
}

quint64 MapRhiScene::geometryRevision() const
{
    return this->geometry_revision;
}

bool MapRhiScene::hasGeometry() const
{
    return !this->link_vertices.isEmpty() || !this->node_vertices.isEmpty();
}

NetworkSymbologySizeUnit MapRhiScene::nodeSizeUnit() const
{
    return this->symbology.node_size_unit;
}

int MapRhiScene::nodeSizePx() const
{
    return this->symbology.node_size_px;
}

double MapRhiScene::nodeSizeM() const
{
    return this->symbology.node_size_m;
}

NetworkSymbologySizeUnit MapRhiScene::linkThicknessUnit() const
{
    return this->symbology.link_thickness_unit;
}

int MapRhiScene::linkThicknessPx() const
{
    return this->symbology.link_thickness_px;
}

double MapRhiScene::linkThicknessM() const
{
    return this->symbology.link_thickness_m;
}

double MapRhiScene::worldUnitsPerMeter() const
{
    const double meters_per_world_pixel = GeoWebMercator::metersPerPixel(
        this->reference_latitude_deg, MapRenderCacheMath::ReferenceZoom);
    if (!std::isfinite(meters_per_world_pixel) || meters_per_world_pixel <= 0.0)
        return 0.0;

    return 1.0 / meters_per_world_pixel;
}

float MapRhiScene::elevationToWorldZ(double elevation_m) const
{
    return localElevationWorld(elevation_m);
}

float MapRhiScene::terrainElevationToWorldZ(double elevation_m) const
{
    if (!std::isfinite(elevation_m))
        return 1.0f;

    const double meters_per_world_pixel = GeoWebMercator::metersPerPixel(
        this->reference_latitude_deg, MapRenderCacheMath::ReferenceZoom);
    if (!std::isfinite(meters_per_world_pixel) || meters_per_world_pixel <= 0.0)
        return 1.0f;

    return float(1.0
        + (elevation_m - this->elevation_reference_m)
            * this->vertical_exaggeration / meters_per_world_pixel);
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

float MapRhiScene::localElevationWorld(double elevation_m) const
{
    if (!std::isfinite(elevation_m))
        return 1.0f;

    const double meters_per_world_pixel = GeoWebMercator::metersPerPixel(
        this->reference_latitude_deg, MapRenderCacheMath::ReferenceZoom);
    if (!std::isfinite(meters_per_world_pixel) || meters_per_world_pixel <= 0.0)
        return 1.0f;

    return float(1.0
        + ((elevation_m - this->elevation_reference_m) * this->vertical_exaggeration
            + this->network_ground_offset_m) / meters_per_world_pixel);
}

void MapRhiScene::appendLinkSegment(
    InfrastructureEntity entity_type, quint32 render_id,
    const QPointF &start, float start_z,
    const QPointF &end, float end_z)
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
        vertex.start_z = start_z;
        vertex.end_x = float(end.x());
        vertex.end_y = float(end.y());
        vertex.end_z = end_z;
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
    InfrastructureEntity entity_type, quint32 render_id,
    const QPointF &center, float center_z)
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
        vertex.center_z = center_z;
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
    const bool hidden_by_3d_junction =
        this->use_3d_junction_models
        && vertex->entity_type == InfrastructureEntity::Junction;
    vertex->alpha = hidden_by_3d_junction
        || (this->symbology.show_icons && mapRhiHasIcon(vertex->entity_type))
        ? 0.0f
        : qAlpha(color) / 255.0f;
}


void MapRhiScene::rebuildHeatmap()
{
    this->heatmap_vertices.clear();
    if (this->symbology.visual_heatmap == VisualHeatmap::None
        || this->symbology.heatmap_fractions.isEmpty())
    {
        return;
    }

    this->heatmap_vertices.reserve(this->heatmap_markers.size() * 6);
    for (const HeatmapMarker &marker : this->heatmap_markers)
        appendHeatmap(marker);
}

void MapRhiScene::appendHeatmap(const HeatmapMarker &marker)
{
    const QHash<quint32, double>::const_iterator fraction_iterator =
        this->symbology.heatmap_fractions.constFind(marker.render_id);
    if (fraction_iterator == this->symbology.heatmap_fractions.cend())
        return;

    const QColor color = networkSymbologyInterpolatedRampColor(
        fraction_iterator.value(),
        this->symbology.heatmap_palette,
        this->symbology.heatmap_palette_flipped);
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
        HeatmapVertex vertex;
        vertex.center_x = float(marker.center.x());
        vertex.center_y = float(marker.center.y());
        // Keep the heatmap just above the basemap to avoid coplanar depth fighting.
        // The heatmap shader expands this center in the XY world plane, so in 3D it
        // lies flat on the ground instead of facing the camera.
        vertex.center_z = 0.05f;
        vertex.corner_x = corners[index][0];
        vertex.corner_y = corners[index][1];
        vertex.red = color.redF();
        vertex.green = color.greenF();
        vertex.blue = color.blueF();
        this->heatmap_vertices.append(vertex);
    }
}

void MapRhiScene::rebuildIcons()
{
    this->icon_vertices.clear();
    if (!this->symbology.show_icons)
        return;

    this->icon_vertices.reserve(this->icon_markers.size() * 6);
    for (const IconMarker &marker : this->icon_markers)
        appendIcon(marker);
}

void MapRhiScene::appendIcon(const IconMarker &marker)
{
    if (this->use_3d_tank_models && marker.entity_type == InfrastructureEntity::Tank)
        return;

    const MapRhiIconAtlasEntry atlas_entry = mapRhiIconAtlasEntry(marker.entity_type);
    if (!atlas_entry.valid)
        return;

    const qreal marker_size = networkSymbologyMarkerSizeForZoom(
        this->view_zoom, this->symbology.icon_size_percent);
    const float half_width_px = float(marker_size * atlas_entry.width_ratio / 2.0);
    const float half_height_px = float(marker_size * atlas_entry.height_ratio / 2.0);
    const QRgb color = InfrastructureEntityTraits::isHydraulicConnectionNode(marker.entity_type)
        ? this->symbology.node_colors.value(marker.render_id, networkSymbologyDefaultColor())
        : this->symbology.link_colors.value(marker.render_id, networkSymbologyDefaultColor());

    const float corners[6][2] = {
        {-1.0f, -1.0f},
        {1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, 1.0f}
    };

    const float u_left = float(atlas_entry.uv_rect.left());
    const float u_right = float(atlas_entry.uv_rect.right());
    const float v_top = float(atlas_entry.uv_rect.top());
    const float v_bottom = float(atlas_entry.uv_rect.bottom());
    for (int index = 0; index < 6; ++index)
    {
        IconVertex vertex;
        vertex.center_x = float(marker.center.x());
        vertex.center_y = float(marker.center.y());
        vertex.center_z = marker.z;
        vertex.offset_x_px = corners[index][0] * half_width_px;
        vertex.offset_y_px = corners[index][1] * half_height_px;
        vertex.u = corners[index][0] < 0.0f ? u_left : u_right;
        vertex.v = corners[index][1] < 0.0f ? v_bottom : v_top;
        vertex.red = qRed(color) / 255.0f;
        vertex.green = qGreen(color) / 255.0f;
        vertex.blue = qBlue(color) / 255.0f;
        vertex.alpha = qAlpha(color) / 255.0f;
        vertex.render_id = marker.render_id;
        vertex.entity_type = marker.entity_type;
        this->icon_vertices.append(vertex);
    }
}

void MapRhiScene::rebuildTankInstances()
{
    this->tank_instances.clear();
    if (!this->symbology.show_icons || !this->use_3d_tank_models)
        return;

    const qreal scale = GeoWebMercator::zoomScale(
        this->view_zoom, MapRenderCacheMath::ReferenceZoom);
    if (!std::isfinite(scale) || scale <= 0.0)
        return;

    const qreal marker_size = networkSymbologyMarkerSizeForZoom(
        this->view_zoom, this->symbology.icon_size_percent);
    const float world_marker_size = float(marker_size / scale);
    const float radius_world = world_marker_size * 0.44f;
    const float base_height_world = world_marker_size * 0.20f;
    const float body_height_world = world_marker_size * 0.78f;
    const float roof_height_world = world_marker_size * 0.26f;

    quint32 selected_tank_render_id = 0;
    if (this->selected_entity_type == InfrastructureEntity::Tank
        && !this->selected_entity_uuid.isNull())
    {
        const QHash<QUuid, quint64>::const_iterator selected_iterator =
            this->entity_keys_by_uuid.constFind(this->selected_entity_uuid);
        if (selected_iterator != this->entity_keys_by_uuid.cend())
        {
            const quint32 render_id = quint32(selected_iterator.value() & 0xffffffffULL);
            if (entityRenderKey(InfrastructureEntity::Tank, render_id)
                == selected_iterator.value())
            {
                selected_tank_render_id = render_id;
            }
        }
    }

    for (const IconMarker &marker : this->icon_markers)
    {
        if (marker.entity_type != InfrastructureEntity::Tank)
            continue;

        MapRhiTankInstance instance;
        instance.render_id = marker.render_id;
        instance.base_center = QVector3D(
            float(marker.center.x()),
            float(marker.center.y()),
            marker.z + 0.02f);
        instance.radius_world = radius_world;
        instance.base_height_world = base_height_world;
        instance.body_height_world = body_height_world;
        instance.roof_height_world = roof_height_world;
        instance.selected = marker.render_id == selected_tank_render_id ? 1.0f : 0.0f;
        this->tank_instances.append(instance);
    }
}


void MapRhiScene::rebuildJunctionInstances()
{
    this->junction_instances.clear();
    if (!this->use_3d_junction_models || this->junction_markers.isEmpty())
        return;

    float radius_world = 1.0f;
    if (this->symbology.node_size_unit == NetworkSymbologySizeUnit::Meters)
    {
        const double units_per_meter = worldUnitsPerMeter();
        if (std::isfinite(units_per_meter) && units_per_meter > 0.0)
            radius_world = float(this->symbology.node_size_m * units_per_meter * 0.5);
    }

    quint32 selected_junction_render_id = 0;
    if (this->selected_entity_type == InfrastructureEntity::Junction
        && !this->selected_entity_uuid.isNull())
    {
        const QHash<QUuid, quint64>::const_iterator selected_iterator =
            this->entity_keys_by_uuid.constFind(this->selected_entity_uuid);
        if (selected_iterator != this->entity_keys_by_uuid.cend())
        {
            const quint32 render_id = quint32(selected_iterator.value() & 0xffffffffULL);
            if (entityRenderKey(InfrastructureEntity::Junction, render_id)
                == selected_iterator.value())
            {
                selected_junction_render_id = render_id;
            }
        }
    }

    this->junction_instances.reserve(this->junction_markers.size());
    for (const JunctionMarker &marker : this->junction_markers)
    {
        const QRgb color = marker.render_id == selected_junction_render_id
            ? QColor(0, 190, 255).rgba()
            : this->symbology.node_colors.value(
                  marker.render_id, networkSymbologyDefaultColor());

        MapRhiJunctionInstance instance;
        instance.render_id = marker.render_id;
        instance.center_x = float(marker.center.x());
        instance.center_y = float(marker.center.y());
        instance.center_z = marker.z;
        instance.radius_world = radius_world;
        instance.red = qRed(color) / 255.0f;
        instance.green = qGreen(color) / 255.0f;
        instance.blue = qBlue(color) / 255.0f;
        instance.alpha = qAlpha(color) / 255.0f;
        instance.selected = marker.render_id == selected_junction_render_id ? 1.0f : 0.0f;
        this->junction_instances.append(instance);
    }
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
        for (const SceneSegment &segment : path.segments)
            total_world_length += QLineF(segment.start, segment.end).length();

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
        for (const SceneSegment &segment : path.segments)
            total_world_length += QLineF(segment.start, segment.end).length();

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
            for (const SceneSegment &segment : path.segments)
            {
                const QLineF segment_line(segment.start, segment.end);
                const qreal segment_world_length = segment_line.length();
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
                    (segment.end.x() - segment.start.x()) / segment_world_length,
                    (segment.end.y() - segment.start.y()) / segment_world_length);
                QPointF direction = direction_base;
                if (flow_direction < 0)
                    direction *= -1.0;

                const QPointF center(
                    segment.start.x() + (segment.end.x() - segment.start.x()) * ratio,
                    segment.start.y() + (segment.end.y() - segment.start.y()) * ratio);
                const qreal elevation_pixels = qMax<qreal>(
                    FlowDirectionMinimumElevationPixels,
                    qreal(this->symbology.link_thickness_px) / 2.0 + 2.0);
                const float elevation_world = float(elevation_pixels / scale);
                const float center_z = segment.start_z
                    + (segment.end_z - segment.start_z) * float(ratio)
                    + elevation_world;
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
                    tail_first, tip, center_z, arrow_color, half_stroke_px);
                appendFlowDirectionStroke(
                    tail_second, tip, center_z, arrow_color, half_stroke_px);
                break;
            }
        }
    }
}

void MapRhiScene::appendFlowDirectionStroke(
    const QPointF &start, const QPointF &end, float z,
    QRgb color, float half_width_px)
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
        vertex.start_z = z;
        vertex.end_x = float(end.x());
        vertex.end_y = float(end.y());
        vertex.end_z = z;
        vertex.along = corners[index][0];
        vertex.side = corners[index][1];
        vertex.red = qRed(color) / 255.0f;
        vertex.green = qGreen(color) / 255.0f;
        vertex.blue = qBlue(color) / 255.0f;
        vertex.alpha = qAlpha(color) / 255.0f;
        vertex.size_adjust_px = this->symbology.link_thickness_unit
                == NetworkSymbologySizeUnit::Meters
            ? -half_width_px
            : half_width_px - base_half_width_px;
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
                const bool selected_is_3d_model =
                    (this->use_3d_junction_models
                        && this->selected_entity_type == InfrastructureEntity::Junction)
                    || (this->use_3d_tank_models
                        && this->selected_entity_type == InfrastructureEntity::Tank);
                QVector<NodeVertex> *selected_node_target = selected_is_3d_model
                    ? nullptr
                    : &this->selected_node_vertices;
                appendEntityHighlight(
                    this->selected_entity_type,
                    quint32(selected_iterator.value() & 0xffffffffULL),
                    QColor(0, 190, 255),
                    (selected_link_width - base_link_width) / 2.0f,
                    selected_has_error ? 5.0f : 2.0f,
                    &this->selected_link_vertices,
                    selected_node_target);
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
