#ifndef MAP_BGFX_2D_SCENE_H
#define MAP_BGFX_2D_SCENE_H

#include "map/bgfx/map_bgfx_2d_projection.h"
#include "map/render/map_node_declutter.h"
#include "map/render/map_retained_network_scene.h"

#include <QHash>
#include <QPointF>
#include <QSet>
#include <QVector>

#include <atomic>
#include <cmath>

// Camera-independent preparation of network primitives for the bgfx
// renderer. The retained snapshot remains authoritative; these normalized
// Mercator positions are regenerated only on geometry revisions.
//
// * Every snapshot node is a point, including demand points (entity type
//   DemandPoint). Demand points are NOT taken from the attachment list as
//   well: that drew each of them twice.
// * Coincident nodes (pump/valve inlet+outlet digitized at one coordinate)
//   are spread with the shared computeNodeDeclutterOffsets() at the same
//   1 m separation as the QRhi and Globe scenes. Link endpoints and demand
//   point attachments follow their node's offset so lines stay attached.
// * Links are kept as polylines (x unwrapped continuously along the link),
//   because icons and flow chevrons are placed along the whole link.
struct MapBgfx2dSegment
{
    MapBgfx2d::WorldPoint start;
    MapBgfx2d::WorldPoint end;
    quint32 render_id = 0;
};

struct MapBgfx2dPoint
{
    MapBgfx2d::WorldPoint position;
    quint32 render_id = 0;
    // Symbology input only (junction visibility, icons); never geometry.
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
};

struct MapBgfx2dPolyline
{
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    QVector<MapBgfx2d::WorldPoint> points;
};

namespace MapBgfx2dSceneMath
{
constexpr double EquatorMeters = 40075016.685578488;

// Normalized world units per metre on the ground at a latitude (one world
// unit is the equator circumference; Mercator stretches by 1/cos(lat)).
inline double worldUnitsPerMeter(double latitude_deg)
{
    const double latitude = std::clamp(latitude_deg,
        -MapBgfx2d::MaximumMercatorLatitude, MapBgfx2d::MaximumMercatorLatitude);
    return 1.0 / (EquatorMeters * std::cos(latitude * MapBgfx2d::DegreesToRadians));
}
}

class MapBgfx2dScene
{
public:
    // Key of one rendered entity: render ids are only unique per entity type.
    static quint64 entityKey(InfrastructureEntity entity_type, quint32 render_id)
    {
        return (quint64(quint8(entity_type)) << 32) | quint64(render_id);
    }

    // Spread coincident nodes (default, as QRhi). Toggling it forces the next
    // prepare() to recompute positions even without a geometry revision.
    void setDeclutteringEnabled(bool enabled)
    {
        if (this->declutter_enabled == enabled)
            return;
        this->declutter_enabled = enabled;
        this->initialized = false;
    }
    bool declutteringEnabled() const { return this->declutter_enabled; }

    bool prepare(const MapRetainedNetworkScene &retained_scene)
    {
        if (!retained_scene.hasSnapshot())
            return false;
        const NetworkRenderSnapshot &snapshot = retained_scene.snapshot();
        if (this->initialized && this->geometry_revision == snapshot.geometry_revision)
            return false;

        this->link_polylines.clear();
        this->nodes.clear();
        this->attachment_segments.clear();
        this->nodes.reserve(snapshot.nodes.size());
        this->attachment_segments.reserve(snapshot.demand_point_attachments.size());

        // Nodes and the declutter reference latitude (first valid node, as QRhi).
        QVector<MapNodeDeclutterInput> declutter_inputs;
        declutter_inputs.reserve(snapshot.nodes.size());
        bool have_reference = false;
        double reference_latitude_deg = 0.0;
        for (const NetworkRenderNode &node : snapshot.nodes)
        {
            if (!MapBgfx2d::coordinateIsValid(node.coordinate_wgs84.longitude_deg,
                                              node.coordinate_wgs84.latitude_deg))
                continue;
            if (!have_reference)
            {
                reference_latitude_deg = node.coordinate_wgs84.latitude_deg;
                have_reference = true;
            }
            const MapBgfx2d::WorldPoint position = MapBgfx2d::worldFromWgs84(
                node.coordinate_wgs84.longitude_deg, node.coordinate_wgs84.latitude_deg);
            this->nodes.append({position, node.render_id, node.entity_type});
            declutter_inputs.append({node.render_id, QPointF(position.x, position.y)});
        }
        const QHash<quint32, QPointF> offsets = this->declutter_enabled
            ? computeNodeDeclutterOffsets(
                  declutter_inputs, MapNodeDeclutterMinimumSeparationMeters
                      * MapBgfx2dSceneMath::worldUnitsPerMeter(reference_latitude_deg))
            : QHash<quint32, QPointF>();
        for (MapBgfx2dPoint &point : this->nodes)
        {
            const QPointF offset = offsets.value(point.render_id);
            point.position.x += offset.x();
            point.position.y += offset.y();
        }

        for (const NetworkRenderLink &link : snapshot.links)
        {
            MapBgfx2dPolyline polyline;
            polyline.render_id = link.render_id;
            polyline.entity_type = link.entity_type;
            const qsizetype last_index = link.vertices_wgs84.size() - 1;
            for (qsizetype index = 0; index <= last_index; ++index)
            {
                const CoordinateWGS84 &vertex = link.vertices_wgs84.at(index);
                if (!MapBgfx2d::coordinateIsValid(vertex.longitude_deg, vertex.latitude_deg))
                {
                    // An invalid vertex splits the link, like QRhi.
                    appendPolyline(polyline);
                    polyline.points.clear();
                    continue;
                }
                MapBgfx2d::WorldPoint point =
                    MapBgfx2d::worldFromWgs84(vertex.longitude_deg, vertex.latitude_deg);
                // First/last vertex is the start/end node's coordinate: apply
                // that node's declutter offset so the link stays attached.
                QPointF offset;
                if (index == 0)
                    offset = offsets.value(link.start_node_render_id);
                else if (index == last_index)
                    offset = offsets.value(link.end_node_render_id);
                point.x += offset.x();
                point.y += offset.y();
                if (!polyline.points.isEmpty())
                    point.x = MapBgfx2d::wrapNearest(point.x, polyline.points.last().x);
                polyline.points.append(point);
            }
            appendPolyline(polyline);
        }

        for (const NetworkRenderDemandPointAttachment &attachment
             : snapshot.demand_point_attachments)
        {
            const CoordinateWGS84 &demand = attachment.demand_point_coordinate_wgs84;
            const CoordinateWGS84 &anchor = attachment.attachment_coordinate_wgs84;
            if (!MapBgfx2d::coordinateIsValid(demand.longitude_deg, demand.latitude_deg)
                || !MapBgfx2d::coordinateIsValid(anchor.longitude_deg, anchor.latitude_deg))
                continue;
            MapBgfx2d::WorldPoint start =
                MapBgfx2d::worldFromWgs84(demand.longitude_deg, demand.latitude_deg);
            const QPointF offset = offsets.value(attachment.demand_point_render_id);
            start.x += offset.x();
            start.y += offset.y();
            this->attachment_segments.append({start,
                MapBgfx2d::worldFromWgs84(anchor.longitude_deg, anchor.latitude_deg),
                attachment.demand_point_render_id});
        }
        this->geometry_revision = snapshot.geometry_revision;
        this->initialized = true;
        ++this->preparation_count;
        this->preparation_id = nextPreparationId();
        return true;
    }

    // The nodes and links whose entityKey() is in keys (no attachments), as
    // a scene of its own for highlight meshes. Positions are copied, so a
    // highlight lies exactly on the drawn (decluttered) primitive.
    MapBgfx2dScene filtered(const QSet<quint64> &keys) const
    {
        MapBgfx2dScene result;
        result.declutter_enabled = this->declutter_enabled;
        for (const MapBgfx2dPolyline &polyline : this->link_polylines)
        {
            if (keys.contains(entityKey(polyline.entity_type, polyline.render_id)))
                result.link_polylines.append(polyline);
        }
        for (const MapBgfx2dPoint &point : this->nodes)
        {
            if (keys.contains(entityKey(point.entity_type, point.render_id)))
                result.nodes.append(point);
        }
        result.geometry_revision = this->geometry_revision;
        result.initialized = this->initialized;
        result.preparation_count = 1;
        result.preparation_id = nextPreparationId();
        return result;
    }

    const QVector<MapBgfx2dPolyline> &linkPolylines() const { return this->link_polylines; }
    const QVector<MapBgfx2dSegment> &attachments() const { return this->attachment_segments; }
    const QVector<MapBgfx2dPoint> &networkNodes() const { return this->nodes; }
    qsizetype linkSegmentCount() const
    {
        qsizetype count = 0;
        for (const MapBgfx2dPolyline &polyline : this->link_polylines)
            count += polyline.points.size() - 1;
        return count;
    }
    quint64 preparationCount() const { return this->preparation_count; }
    // Unique per preparation (and per filtered() copy), process-wide. GPU
    // staging rebuilds when it changes: geometry revisions, declutter toggles.
    quint64 preparationId() const { return this->preparation_id; }
    bool isPrepared() const { return this->initialized; }
    quint64 geometryRevision() const { return this->geometry_revision; }

private:
    static quint64 nextPreparationId()
    {
        static std::atomic<quint64> counter{0};
        return ++counter;
    }

    void appendPolyline(const MapBgfx2dPolyline &polyline)
    {
        if (polyline.points.size() >= 2)
            this->link_polylines.append(polyline);
    }

    QVector<MapBgfx2dPolyline> link_polylines;
    QVector<MapBgfx2dSegment> attachment_segments;
    QVector<MapBgfx2dPoint> nodes;
    quint64 geometry_revision = 0;
    quint64 preparation_count = 0;
    quint64 preparation_id = 0;
    bool declutter_enabled = true;
    bool initialized = false;
};

#endif // MAP_BGFX_2D_SCENE_H
