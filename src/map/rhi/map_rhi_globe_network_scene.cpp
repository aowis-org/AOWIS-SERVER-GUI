#include "map/rhi/map_rhi_globe_network_scene.h"

#include "map/core/map_model.h"
#include "geo/geo_wgs84_ellipsoid.h"
#include "network/infrastructure_entity_traits.h"
#include "network/network_symbology_rendering.h"

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

constexpr double UndergroundSubdivisionTargetLengthM = 20.0;
constexpr int UndergroundSubdivisionMaximumCount = 32;
// How far below the sampled terrain a point has to sit before it counts as
// "buried" -- small enough to ignore DEM sampling noise/float error for a
// pipe running essentially at grade, large enough to not flag on that noise.
constexpr double UndergroundToleranceM = 0.1;
// See the comment in ecefPosition() -- floor under ground_offset_m so
// network geometry reliably wins its depth test against the terrain mesh
// even when the user hasn't (or doesn't need to have) touched the ground
// offset slider.
constexpr double MinimumAntiZFightingLiftM = 2.0;
}

void MapRhiGlobeNetworkScene::setNetworkSnapshot(const NetworkRenderSnapshot &snapshot)
{
    this->network_snapshot = snapshot;
    rebuildNetworkGeometry();
}

bool MapRhiGlobeNetworkScene::setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids)
{
    if (this->hidden_entity_uuids == hidden_entity_uuids)
        return false;

    this->hidden_entity_uuids = hidden_entity_uuids;
    rebuildNetworkGeometry();
    return true;
}

void MapRhiGlobeNetworkScene::setSymbology(const MapRhiSymbology &symbology)
{
    const bool link_colors_changed = this->symbology.link_colors != symbology.link_colors;
    const bool node_colors_changed = this->symbology.node_colors != symbology.node_colors;
    const bool icon_visibility_changed = this->symbology.show_icons != symbology.show_icons;
    const bool link_thickness_changed =
        this->symbology.link_thickness_unit != symbology.link_thickness_unit
        || this->symbology.link_thickness_px != symbology.link_thickness_px
        || this->symbology.link_thickness_m != symbology.link_thickness_m;
    const bool icon_changed =
        this->symbology.icon_size_unit != symbology.icon_size_unit
        || this->symbology.icon_size_px != symbology.icon_size_px
        || this->symbology.icon_size_m != symbology.icon_size_m
        || this->symbology.show_icons != symbology.show_icons
        || this->symbology.icon_default_fill_color != symbology.icon_default_fill_color
        || this->symbology.visual_node != symbology.visual_node
        || this->symbology.visual_link != symbology.visual_link
        || link_colors_changed || node_colors_changed;

    this->symbology = symbology;

    if (link_colors_changed)
    {
        for (MapRhiScene::LinkVertex &vertex : this->link_vertices)
            applyLinkColor(&vertex);
        for (MapRhiScene::LinkVertex &vertex : this->underground_link_vertices)
            applyLinkColor(&vertex);
    }
    if (node_colors_changed || icon_visibility_changed)
    {
        for (MapRhiScene::NodeVertex &vertex : this->node_vertices)
            applyNodeColor(&vertex);
    }
    if (icon_changed)
        rebuildIcons();
    if (link_thickness_changed)
        rebuildHighlights();
}

void MapRhiGlobeNetworkScene::setSelectedEntity(
    InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (this->selected_entity_type == entity_type && this->selected_entity_uuid == uuid)
        return;

    this->selected_entity_type = entity_type;
    this->selected_entity_uuid = uuid;
    rebuildHighlights();
    rebuildIcons();
}

void MapRhiGlobeNetworkScene::setSimulationErrorEntities(
    const QHash<QUuid, InfrastructureEntity> &error_entities,
    const QSet<QUuid> &stale_entity_uuids)
{
    this->simulation_error_entities = error_entities;
    this->simulation_stale_entity_uuids = stale_entity_uuids;
    rebuildHighlights();
}

bool MapRhiGlobeNetworkScene::setGroundOffsetM(double offset_m)
{
    if (!std::isfinite(offset_m))
        return false;

    const double bounded_offset_m = qBound(
        MapModel::MinView3dNetworkGroundOffsetM,
        offset_m,
        MapModel::MaxView3dNetworkGroundOffsetM);
    if (qFuzzyCompare(1.0 + this->ground_offset_m, 1.0 + bounded_offset_m))
        return false;

    this->ground_offset_m = bounded_offset_m;
    rebuildNetworkGeometry();
    return true;
}

bool MapRhiGlobeNetworkScene::setVerticalExaggeration(double exaggeration)
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

bool MapRhiGlobeNetworkScene::setTerrainReady(bool ready)
{
    if (this->terrain_ready == ready)
        return false;

    this->terrain_ready = ready;
    rebuildNetworkGeometry();
    return true;
}

void MapRhiGlobeNetworkScene::setTerrainElevationResolver(TerrainElevationResolver resolver)
{
    this->terrain_elevation_resolver = std::move(resolver);
}

bool MapRhiGlobeNetworkScene::setUndergroundXRayEnabled(bool enabled)
{
    if (this->underground_xray_enabled == enabled)
        return false;

    this->underground_xray_enabled = enabled;
    rebuildNetworkGeometry();
    return true;
}

const QVector<MapRhiScene::LinkVertex> &MapRhiGlobeNetworkScene::linkVertices() const
{
    return this->link_vertices;
}

const QVector<MapRhiScene::NodeVertex> &MapRhiGlobeNetworkScene::nodeVertices() const
{
    return this->node_vertices;
}

const QVector<MapRhiScene::LinkVertex> &MapRhiGlobeNetworkScene::selectedLinkVertices() const
{
    return this->selected_link_vertices;
}

const QVector<MapRhiScene::NodeVertex> &MapRhiGlobeNetworkScene::selectedNodeVertices() const
{
    return this->selected_node_vertices;
}

const QVector<MapRhiScene::LinkVertex> &MapRhiGlobeNetworkScene::diagnosticLinkVertices() const
{
    return this->diagnostic_link_vertices;
}

const QVector<MapRhiScene::NodeVertex> &MapRhiGlobeNetworkScene::diagnosticNodeVertices() const
{
    return this->diagnostic_node_vertices;
}

const QVector<MapRhiScene::IconVertex> &MapRhiGlobeNetworkScene::iconVertices() const
{
    return this->icon_vertices;
}

const QVector<MapRhiScene::LinkVertex> &MapRhiGlobeNetworkScene::undergroundLinkVertices() const
{
    return this->underground_link_vertices;
}

quint64 MapRhiGlobeNetworkScene::geometryRevision() const
{
    return this->geometry_revision;
}

bool MapRhiGlobeNetworkScene::hasGeometry() const
{
    return !this->link_vertices.isEmpty() || !this->node_vertices.isEmpty();
}

NetworkSymbologySizeUnit MapRhiGlobeNetworkScene::nodeSizeUnit() const
{
    return this->symbology.node_size_unit;
}

int MapRhiGlobeNetworkScene::nodeSizePx() const
{
    return this->symbology.node_size_px;
}

double MapRhiGlobeNetworkScene::nodeSizeM() const
{
    return this->symbology.node_size_m;
}

NetworkSymbologySizeUnit MapRhiGlobeNetworkScene::iconSizeUnit() const
{
    return this->symbology.icon_size_unit;
}

int MapRhiGlobeNetworkScene::iconSizePx() const
{
    return this->symbology.icon_size_px;
}

double MapRhiGlobeNetworkScene::iconSizeM() const
{
    return this->symbology.icon_size_m;
}

NetworkSymbologySizeUnit MapRhiGlobeNetworkScene::linkThicknessUnit() const
{
    return this->symbology.link_thickness_unit;
}

int MapRhiGlobeNetworkScene::linkThicknessPx() const
{
    return this->symbology.link_thickness_px;
}

double MapRhiGlobeNetworkScene::linkThicknessM() const
{
    return this->symbology.link_thickness_m;
}

QVector3D MapRhiGlobeNetworkScene::ecefPosition(
    const CoordinateWGS84 &coordinate, double elevation_m) const
{
    // Pinned to the bare ellipsoid (0) until terrain relief for the visible
    // area has actually loaded -- see the class comment on
    // setTerrainReady() for why.
    const double base_elevation_m = this->terrain_ready && std::isfinite(elevation_m)
        ? elevation_m
        : 0.0;
    // A network entity's elevation and the terrain's live DEM sample at the
    // same point are two independently-sourced numbers -- close, now that
    // terrain-readiness gating keeps them both meaningful (see
    // setTerrainReady()), but not bit-for-bit identical, and float32 ECEF
    // positions only carry ~0.5-1m of precision at Earth's radius to begin
    // with. Placed with zero lift, that's enough for the two surfaces to
    // z-fight (flicker) rather than cleanly resolve one in front of the
    // other. MinimumAntiZFightingLiftM is a floor under the user-configured
    // ground_offset_m (see setGroundOffsetM()), not stacked on top of it --
    // once the user asks for more clearance than that floor, there's
    // nothing left to fight.
    const double lift_m = qMax(this->ground_offset_m, MinimumAntiZFightingLiftM);
    // Order matches globeTerrainPositionAt() exactly (elevation * exaggeration,
    // ground offset added after): see the class comment on
    // setVerticalExaggeration() for why the two must stay in lockstep.
    const double height_m = base_elevation_m * this->vertical_exaggeration + lift_m;
    return GeoWgs84Ellipsoid::geodeticToEcef(
        coordinate.longitude_deg, coordinate.latitude_deg, height_m);
}

void MapRhiGlobeNetworkScene::rebuildNetworkGeometry()
{
    this->link_vertices.clear();
    this->node_vertices.clear();
    this->selected_link_vertices.clear();
    this->selected_node_vertices.clear();
    this->diagnostic_link_vertices.clear();
    this->diagnostic_node_vertices.clear();
    this->icon_vertices.clear();
    this->icon_markers.clear();
    this->link_paths.clear();
    this->underground_link_vertices.clear();
    this->entity_keys_by_uuid.clear();
    this->link_vertex_indices_by_entity.clear();
    this->node_vertex_indices_by_entity.clear();
    this->geometry_revision = this->network_snapshot.geometry_revision;

    // Used only as a placeholder height for vertices whose own elevation is
    // missing/non-finite, so an incomplete dataset still lands near the rest
    // of the network instead of at the bare 0 m ellipsoid surface (which, for
    // a network sitting at real elevation, could be far enough below the
    // globe's DEM-displaced terrain to look buried).
    bool fallback_elevation_initialized = false;
    double fallback_elevation_m = 0.0;
    for (const NetworkRenderNode &node : this->network_snapshot.nodes)
    {
        if (!finiteCoordinate(node.coordinate_wgs84) || !std::isfinite(node.elevation_m))
            continue;

        fallback_elevation_m = fallback_elevation_initialized
            ? qMin(fallback_elevation_m, node.elevation_m)
            : node.elevation_m;
        fallback_elevation_initialized = true;
    }

    this->node_vertices.reserve(this->network_snapshot.nodes.size() * 6);
    for (const NetworkRenderNode &node : this->network_snapshot.nodes)
    {
        if (this->hidden_entity_uuids.contains(node.uuid)
            || !finiteCoordinate(node.coordinate_wgs84))
        {
            continue;
        }

        const double elevation_m =
            std::isfinite(node.elevation_m) ? node.elevation_m : fallback_elevation_m;
        const QVector3D center = ecefPosition(node.coordinate_wgs84, elevation_m);
        this->entity_keys_by_uuid.insert(
            node.uuid, entityRenderKey(node.entity_type, node.render_id));
        appendNode(node.entity_type, node.render_id, center);

        if (mapRhiHasIcon(node.entity_type))
        {
            IconMarker marker;
            marker.entity_type = node.entity_type;
            marker.render_id = node.render_id;
            marker.center = center;
            this->icon_markers.append(marker);
        }
    }

    qsizetype segment_count = 0;
    for (const NetworkRenderLink &link : this->network_snapshot.links)
    {
        if (!this->hidden_entity_uuids.contains(link.uuid))
            segment_count += qMax<qsizetype>(0, link.vertices_wgs84.size() - 1);
    }
    this->link_vertices.reserve(segment_count * 6);

    // Underground detection only means anything once entities sit at their
    // real elevation (see setTerrainReady()) and only costs anything when
    // X-Ray is actually the active mode -- both gates checked once here
    // rather than per-segment below.
    const bool should_detect_underground = this->underground_xray_enabled
        && this->terrain_ready
        && bool(this->terrain_elevation_resolver);

    for (const NetworkRenderLink &link : this->network_snapshot.links)
    {
        if (this->hidden_entity_uuids.contains(link.uuid) || link.vertices_wgs84.size() < 2)
            continue;

        LinkPath link_path;
        link_path.entity_type = link.entity_type;
        link_path.render_id = link.render_id;

        bool have_previous = false;
        QVector3D previous;
        CoordinateWGS84 previous_coordinate;
        double previous_elevation_m = 0.0;
        for (qsizetype vertex_index = 0;
             vertex_index < link.vertices_wgs84.size(); ++vertex_index)
        {
            const CoordinateWGS84 &coordinate = link.vertices_wgs84.at(vertex_index);
            if (!finiteCoordinate(coordinate))
            {
                have_previous = false;
                continue;
            }

            const double raw_elevation_m = vertex_index < link.elevations_m.size()
                ? link.elevations_m.at(vertex_index)
                : fallback_elevation_m;
            const double elevation_m =
                std::isfinite(raw_elevation_m) ? raw_elevation_m : fallback_elevation_m;
            const QVector3D current = ecefPosition(coordinate, elevation_m);

            if (have_previous)
            {
                appendLinkSegment(link.entity_type, link.render_id, previous, current);
                link_path.segments.append({previous, current});

                if (should_detect_underground)
                {
                    appendUndergroundSubdivisions(
                        link.entity_type, link.render_id,
                        previous_coordinate, previous_elevation_m, previous,
                        coordinate, elevation_m, current);
                }
            }

            previous = current;
            previous_coordinate = coordinate;
            previous_elevation_m = elevation_m;
            have_previous = true;
        }

        this->entity_keys_by_uuid.insert(
            link.uuid, entityRenderKey(link.entity_type, link.render_id));

        if (!link_path.segments.isEmpty())
        {
            if (mapRhiHasIcon(link.entity_type))
            {
                float total_length = 0.0f;
                for (const SceneSegment &segment : link_path.segments)
                    total_length += (segment.end - segment.start).length();

                if (total_length > 0.0f)
                {
                    const float target = total_length / 2.0f;
                    float traversed = 0.0f;
                    for (const SceneSegment &segment : link_path.segments)
                    {
                        const float segment_length = (segment.end - segment.start).length();
                        if (segment_length <= 0.0f)
                            continue;
                        if (traversed + segment_length < target)
                        {
                            traversed += segment_length;
                            continue;
                        }

                        const float ratio = qBound(
                            0.0f, (target - traversed) / segment_length, 1.0f);
                        IconMarker marker;
                        marker.entity_type = link.entity_type;
                        marker.render_id = link.render_id;
                        marker.center = segment.start
                            + (segment.end - segment.start) * ratio;
                        this->icon_markers.append(marker);
                        break;
                    }
                }
            }
            this->link_paths.append(link_path);
        }
    }

    rebuildIcons();
    rebuildHighlights();
}

void MapRhiGlobeNetworkScene::appendLinkSegment(
    InfrastructureEntity entity_type, quint32 render_id,
    const QVector3D &start, const QVector3D &end)
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
        MapRhiScene::LinkVertex vertex;
        vertex.start_x = start.x();
        vertex.start_y = start.y();
        vertex.start_z = start.z();
        vertex.end_x = end.x();
        vertex.end_y = end.y();
        vertex.end_z = end.z();
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

void MapRhiGlobeNetworkScene::appendUndergroundLinkSegment(
    InfrastructureEntity entity_type, quint32 render_id,
    const QVector3D &start, const QVector3D &end)
{
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
        MapRhiScene::LinkVertex vertex;
        vertex.start_x = start.x();
        vertex.start_y = start.y();
        vertex.start_z = start.z();
        vertex.end_x = end.x();
        vertex.end_y = end.y();
        vertex.end_z = end.z();
        vertex.along = corners[index][0];
        vertex.side = corners[index][1];
        vertex.render_id = render_id;
        vertex.entity_type = entity_type;
        // link_xray_pipeline reuses the same map_rhi_link.vert as the normal
        // link_pipeline, so it still expects a tinted vertex color to blend
        // its dashed pattern against -- reuse the entity's normal color
        // rather than inventing an xray-specific one.
        applyLinkColor(&vertex);
        this->underground_link_vertices.append(vertex);
    }
}

void MapRhiGlobeNetworkScene::appendUndergroundSubdivisions(
    InfrastructureEntity entity_type, quint32 render_id,
    const CoordinateWGS84 &start_coordinate, double start_elevation_m,
    const QVector3D &start_ecef,
    const CoordinateWGS84 &end_coordinate, double end_elevation_m,
    const QVector3D &end_ecef)
{
    const float segment_length_m = (end_ecef - start_ecef).length();
    const int subdivision_count = qBound(
        1,
        int(std::ceil(double(segment_length_m) / UndergroundSubdivisionTargetLengthM)),
        UndergroundSubdivisionMaximumCount);

    bool buried_run_active = false;
    QVector3D buried_run_start = start_ecef;
    QVector3D previous_point = start_ecef;
    for (int subdivision = 0; subdivision <= subdivision_count; ++subdivision)
    {
        const double ratio = double(subdivision) / double(subdivision_count);
        // Linear interpolation in lon/lat: fine for subdivisions this
        // short (a few tens of metres at most), no antimeridian handling
        // needed at that scale.
        CoordinateWGS84 sample_coordinate = start_coordinate;
        sample_coordinate.longitude_deg = start_coordinate.longitude_deg
            + (end_coordinate.longitude_deg - start_coordinate.longitude_deg) * ratio;
        sample_coordinate.latitude_deg = start_coordinate.latitude_deg
            + (end_coordinate.latitude_deg - start_coordinate.latitude_deg) * ratio;
        const double sample_elevation_m =
            start_elevation_m + (end_elevation_m - start_elevation_m) * ratio;
        const QVector3D sample_point = subdivision == 0
            ? start_ecef
            : (subdivision == subdivision_count
                ? end_ecef
                : start_ecef + (end_ecef - start_ecef) * float(ratio));

        double terrain_elevation_m = 0.0;
        const bool buried = this->terrain_elevation_resolver
            && this->terrain_elevation_resolver(sample_coordinate, &terrain_elevation_m)
            && sample_elevation_m < terrain_elevation_m - UndergroundToleranceM;

        if (buried && !buried_run_active)
        {
            buried_run_active = true;
            buried_run_start = previous_point;
        }
        else if (!buried && buried_run_active)
        {
            appendUndergroundLinkSegment(
                entity_type, render_id, buried_run_start, previous_point);
            buried_run_active = false;
        }

        previous_point = sample_point;
    }

    if (buried_run_active)
        appendUndergroundLinkSegment(entity_type, render_id, buried_run_start, end_ecef);
}

void MapRhiGlobeNetworkScene::appendNode(
    InfrastructureEntity entity_type, quint32 render_id, const QVector3D &center)
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
        MapRhiScene::NodeVertex vertex;
        vertex.center_x = center.x();
        vertex.center_y = center.y();
        vertex.center_z = center.z();
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

void MapRhiGlobeNetworkScene::applyLinkColor(MapRhiScene::LinkVertex *vertex) const
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

void MapRhiGlobeNetworkScene::applyNodeColor(MapRhiScene::NodeVertex *vertex) const
{
    if (vertex == nullptr)
        return;

    const QRgb color = this->symbology.node_colors.value(
        vertex->render_id, networkSymbologyDefaultColor());
    vertex->red = qRed(color) / 255.0f;
    vertex->green = qGreen(color) / 255.0f;
    vertex->blue = qBlue(color) / 255.0f;
    vertex->alpha = this->symbology.show_icons && mapRhiHasIcon(vertex->entity_type)
        ? 0.0f
        : qAlpha(color) / 255.0f;
}

void MapRhiGlobeNetworkScene::rebuildIcons()
{
    this->icon_vertices.clear();
    if (!this->symbology.show_icons)
        return;

    this->icon_vertices.reserve(this->icon_markers.size() * 6);
    for (const IconMarker &marker : this->icon_markers)
        appendIcon(marker);
}

void MapRhiGlobeNetworkScene::appendIcon(const IconMarker &marker)
{
    const MapRhiIconAtlasEntry atlas_entry = mapRhiIconAtlasEntry(marker.entity_type);
    if (!atlas_entry.valid)
        return;

    const float half_width_ratio = float(atlas_entry.width_ratio / 2.0);
    const float half_height_ratio = float(atlas_entry.height_ratio / 2.0);
    const bool node_entity =
        InfrastructureEntityTraits::isHydraulicConnectionNode(marker.entity_type);
    const bool colorization_active = node_entity
        ? this->symbology.visual_node != VisualNode::None
        : this->symbology.visual_link != VisualLink::None;
    QRgb color = this->symbology.icon_default_fill_color;
    if (colorization_active)
    {
        color = node_entity
            ? this->symbology.node_colors.value(
                marker.render_id, networkSymbologyUnavailableColor())
            : this->symbology.link_colors.value(
                marker.render_id, networkSymbologyUnavailableColor());
        if (color == networkSymbologyUnavailableColor())
            color = networkSymbologyIconUnavailableFillColor();
    }

    if (this->selected_entity_type == marker.entity_type
        && !this->selected_entity_uuid.isNull())
    {
        const QHash<QUuid, quint64>::const_iterator selected_iterator =
            this->entity_keys_by_uuid.constFind(this->selected_entity_uuid);
        if (selected_iterator != this->entity_keys_by_uuid.cend()
            && selected_iterator.value() == entityRenderKey(marker.entity_type, marker.render_id))
        {
            color = QColor(0, 190, 255).rgba();
        }
    }

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
        MapRhiScene::IconVertex vertex;
        vertex.center_x = marker.center.x();
        vertex.center_y = marker.center.y();
        vertex.center_z = marker.center.z();
        vertex.offset_x_ratio = corners[index][0] * half_width_ratio;
        vertex.offset_y_ratio = corners[index][1] * half_height_ratio;
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

void MapRhiGlobeNetworkScene::rebuildHighlights()
{
    this->selected_link_vertices.clear();
    this->selected_node_vertices.clear();
    this->diagnostic_link_vertices.clear();
    this->diagnostic_node_vertices.clear();

    const bool selected_has_error = !this->selected_entity_uuid.isNull()
        && this->simulation_error_entities.value(
            this->selected_entity_uuid, InfrastructureEntity::Unknown)
                == this->selected_entity_type;

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

void MapRhiGlobeNetworkScene::appendEntityHighlight(
    InfrastructureEntity entity_type, quint32 render_id, const QColor &color,
    float link_size_adjust_px, float node_size_adjust_px,
    QVector<MapRhiScene::LinkVertex> *link_target,
    QVector<MapRhiScene::NodeVertex> *node_target) const
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

            MapRhiScene::LinkVertex vertex = this->link_vertices.at(vertex_index);
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

            MapRhiScene::NodeVertex vertex = this->node_vertices.at(vertex_index);
            vertex.red = color.redF();
            vertex.green = color.greenF();
            vertex.blue = color.blueF();
            vertex.alpha = color.alphaF();
            vertex.size_adjust_px = node_size_adjust_px;
            node_target->append(vertex);
        }
    }
}
