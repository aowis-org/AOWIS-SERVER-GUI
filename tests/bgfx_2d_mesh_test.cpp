#include "map/bgfx/map_bgfx_2d_scene.h"
#include "map/bgfx/map_bgfx_2d_mesh.h"
#include "map/bgfx/map_bgfx_2d_symbology.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// Chunked bgfx 2D network mesh: chunking, wrap, culling, the line quad
// encoding checked against a CPU mirror of vs_line_2d.sc, node declutter,
// demand-point de-duplication, icons, the zoom-independent flow chevrons
// (checked against a CPU mirror of vs_flow_2d.sc) and the symbology rules.
namespace
{
int fails = 0;

void req(bool ok, const char *what) { if (!ok) { std::printf("FAIL: %s\n", what); ++fails; } }

// CPU mirror of vs_line_2d.sc -> screen pixels (x right, y down).
struct Px { double x, y; };
Px shaderMirror(const MapBgfx2dLineVertex &v, const MapBgfx2dClipCamera &u, double vw, double vh,
                       double half_width, double cap) {
    double cx = u.origin_x + v.x * u.scale_x, cy = u.origin_y + v.y * u.scale_y;
    double dx = v.other_x - v.x, dy = v.other_y - v.y, len = std::sqrt(dx*dx+dy*dy);
    double dirx = len > 0 ? dx/len : 1, diry = len > 0 ? dy/len : 0;
    double nx = -diry, ny = dirx;
    double ox = nx * v.side * half_width - dirx * cap, oy = ny * v.side * half_width - diry * cap;
    cx += ox * u.pixel_x; cy += oy * u.pixel_y;
    return {(cx + 1) * 0.5 * vw, (1 - cy) * 0.5 * vh};
}
double cross(Px a, Px b, Px c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }


CoordinateWGS84 coordinate(double longitude, double latitude)
{
    CoordinateWGS84 result;
    result.longitude_deg = longitude;
    result.latitude_deg = latitude;
    return result;
}

// Builds a MapBgfx2dScene through the real retained-snapshot path.
struct TestScene
{
    NetworkRenderSnapshot snapshot;
    MapRetainedNetworkScene retained;
    MapBgfx2dScene scene;

    quint32 addLink(double lon0, double lat0, double lon1, double lat1,
                    InfrastructureEntity entity_type = InfrastructureEntity::Unknown,
                    quint32 start_node = 0, quint32 end_node = 0)
    {
        NetworkRenderLink link;
        link.render_id = quint32(this->snapshot.links.size() + 1);
        link.entity_type = entity_type;
        link.start_node_render_id = start_node;
        link.end_node_render_id = end_node;
        link.vertices_wgs84.append(coordinate(lon0, lat0));
        link.vertices_wgs84.append(coordinate(lon1, lat1));
        this->snapshot.links.append(link);
        return link.render_id;
    }
    // Polyline link: a list of (longitude, latitude) pairs.
    quint32 addPolyline(const QVector<CoordinateWGS84> &vertices, InfrastructureEntity entity_type)
    {
        NetworkRenderLink link;
        link.render_id = quint32(this->snapshot.links.size() + 1);
        link.entity_type = entity_type;
        for (const CoordinateWGS84 &vertex : vertices)
            link.vertices_wgs84.append(vertex);
        this->snapshot.links.append(link);
        return link.render_id;
    }
    quint32 addNode(double lon, double lat,
                    InfrastructureEntity entity_type = InfrastructureEntity::Unknown)
    {
        NetworkRenderNode node;
        node.render_id = quint32(this->snapshot.nodes.size() + 100000);
        node.entity_type = entity_type;
        node.coordinate_wgs84 = coordinate(lon, lat);
        this->snapshot.nodes.append(node);
        return node.render_id;
    }
    void addAttachment(quint32 demand_point, double lon0, double lat0, double lon1, double lat1)
    {
        NetworkRenderDemandPointAttachment attachment;
        attachment.demand_point_render_id = demand_point;
        attachment.demand_point_coordinate_wgs84 = coordinate(lon0, lat0);
        attachment.attachment_coordinate_wgs84 = coordinate(lon1, lat1);
        this->snapshot.demand_point_attachments.append(attachment);
    }
    const MapBgfx2dScene &prepare()
    {
        this->snapshot.geometry_revision = 1;
        this->retained.setSnapshot(this->snapshot);
        this->scene.prepare(this->retained);
        return this->scene;
    }
};

// CPU mirror of the visibility term in vs_flow_2d.sc.
double chevronVisibility(const MapBgfx2dChevronVertex &vertex, double world_pixels, double sign)
{
    const double threshold = MapBgfx2dFlow::thresholdLevel(world_pixels);
    const double level_visibility = std::clamp(double(vertex.level) - threshold + 1.0, 0.0, 1.0);
    const double long_enough =
        double(vertex.link_length) * world_pixels >= MapBgfx2dFlow::MinimumLinkPixels ? 1.0 : 0.0;
    return level_visibility * long_enough * std::fabs(sign);
}

// Absolute world position of the first vertex of primitive k (vertices per
// primitive: 4 for quads, 6 for markers/icons).
template <typename Vertex>
MapBgfx2d::WorldPoint worldOf(const MapBgfx2dMeshChunk &chunk, const QVector<Vertex> &vertices,
                              qsizetype first_vertex)
{
    return {chunk.origin.x + double(vertices[first_vertex].x),
            chunk.origin.y + double(vertices[first_vertex].y)};
}

// Fake atlas: tanks and pumps have an icon, everything else none.
MapBgfx2dIconSprite fakeSprite(InfrastructureEntity entity_type)
{
    MapBgfx2dIconSprite sprite;
    sprite.valid = entity_type == InfrastructureEntity::Tank
        || entity_type == InfrastructureEntity::Pump;
    sprite.u0 = 0.1f;
    sprite.v0 = 0.2f;
    sprite.u1 = 0.3f;
    sprite.v1 = 0.4f;
    sprite.half_width_ratio = 0.5f;
    sprite.half_height_ratio = 0.25f;
    return sprite;
}
}

int main()
{
    // Tests 1-6 have no atlas: an empty lookup stages no icons.
    const MapBgfx2dMesh::IconLookup no_icons;

    // 1. ~220 km wide synthetic network: many chunks, each primitive once, small locals.
    TestScene wide;
    for (int i = 0; i < 2000; ++i) {
        const double lon0 = 10.0 + i * 0.001;
        wide.addLink(lon0, 50.0, lon0 + 0.001, 50.0);
        wide.addNode(lon0, 50.0);
    }
    const MapBgfx2dScene &scene = wide.prepare();
    MapBgfx2dMesh mesh;
    req(mesh.prepare(scene, no_icons), "prepare");
    req(!mesh.prepare(scene, no_icons), "same revision must not rebuild");
    req(mesh.chunks().size() > 10, "wide network split into chunks");
    req(mesh.linkVertexCount() == 8000 && mesh.linkIndexCount() == 12000, "link counts");
    req(mesh.nodeVertexCount() == 12000, "node counts");
    double max_local = 0;
    for (const MapBgfx2dMeshChunk &c : mesh.chunks()) for (const MapBgfx2dLineVertex &v : c.link_vertices)
        max_local = std::max({max_local, std::fabs(double(v.x)), std::fabs(double(v.y)), std::fabs(double(v.other_x))});
    req(max_local < 1.5 * MapBgfx2dChunking::CellSize, "locals small");
    std::printf("chunks=%zu max_local=%.3g (cell %.3g)\n", size_t(mesh.chunks().size()), max_local, MapBgfx2dChunking::CellSize);

    // 2. Antimeridian segment stays one short segment.
    TestScene am; am.addLink(179.9995, 0.0, -179.9995, 0.0);
    MapBgfx2dMesh amm; amm.prepare(am.prepare(), no_icons);
    // The segment lives in exactly one chunk (its midpoint cell, created
    // first); its chevron is chunked at its own position and may sit in the
    // neighbouring cell across the antimeridian.
    const MapBgfx2dLineVertex &a0 = amm.chunks()[0].link_vertices[0];
    req(amm.linkVertexCount() == 4 && amm.chunks()[0].link_vertices.size() == 4
            && std::fabs(double(a0.other_x - a0.x)) < 1e-5, "antimeridian segment continuous");
    // 3. Culling incl. wrap.
    MapBgfx2d::Camera near_cam = MapBgfx2d::cameraFor(-179.9999, 0.0, 16.0, 1200, 800);
    MapBgfx2d::Camera far_cam = MapBgfx2d::cameraFor(20.0, 0.0, 16.0, 1200, 800);
    req(MapBgfx2dChunking::chunkVisible(amm.chunks()[0], near_cam, 10), "visible across antimeridian");
    req(!MapBgfx2dChunking::chunkVisible(amm.chunks()[0], far_cam, 10), "far chunk culled");
    int visible = 0; MapBgfx2d::Camera mid = MapBgfx2d::cameraFor(11.0, 50.0, 15.0, 1200, 800);
    for (const MapBgfx2dMeshChunk &c : mesh.chunks()) visible += MapBgfx2dChunking::chunkVisible(c, mid, 10) ? 1 : 0;
    std::printf("visible chunks at z15: %d of %zu\n", visible, size_t(mesh.chunks().size()));
    req(visible >= 1 && visible < int(mesh.chunks().size()), "culling keeps only nearby chunks");

    // 4. Shader mirror: horizontal pipe of 100 px at zoom z, half width 4 px, cap 4 px.
    {
        const double z = 17.0;
        // 100 px east of (10E, 0N) at zoom z: longitude span = 100 px * 360 / world px.
        const double span_deg = 100.0 * 360.0 / (256.0 * std::exp2(z));
        TestScene s; s.addLink(10.0, 0.0, 10.0 + span_deg, 0.0);
        MapBgfx2dMesh m; m.prepare(s.prepare(), no_icons);
        const MapBgfx2dMeshChunk &c = m.chunks()[0];
        MapBgfx2d::Camera cam = MapBgfx2d::cameraFor(10.0, 0.0, z, 800, 600);
        MapBgfx2dClipCamera u = MapBgfx2dChunking::cameraUniform(c.origin, cam);
        Px p[4]; for (int i = 0; i < 4; ++i) p[i] = shaderMirror(c.link_vertices[i], u, 800, 600, 4.0, 4.0);
        // A at screen (400,300), B at (500,300).
        std::printf("corners: (%.2f,%.2f) (%.2f,%.2f) (%.2f,%.2f) (%.2f,%.2f)\n", p[0].x,p[0].y,p[1].x,p[1].y,p[2].x,p[2].y,p[3].x,p[3].y);
        double minx=1e9,maxx=-1e9,miny=1e9,maxy=-1e9; for (Px q: p){minx=std::min(minx,q.x);maxx=std::max(maxx,q.x);miny=std::min(miny,q.y);maxy=std::max(maxy,q.y);}
        req(std::fabs(minx-396)<1e-2 && std::fabs(maxx-504)<1e-2, "length incl. caps = 100 + 2*4 px");
        req(std::fabs(miny-296)<1e-2 && std::fabs(maxy-304)<1e-2, "width = 2*4 px");
        const quint32 *ix = c.link_indices.data();
        double t1 = cross(p[ix[0]],p[ix[1]],p[ix[2]]), t2 = cross(p[ix[3]],p[ix[4]],p[ix[5]]);
        req(std::fabs(t1) > 1 && std::fabs(t2) > 1 && (t1 > 0) == (t2 > 0), "two non-degenerate same-winding triangles");
        req(std::fabs(std::fabs(t1)+std::fabs(t2) - 108.0*8.0*2.0) < 0.5, "triangles cover the full quad area");
        // Diagonal segment: still exactly 8 px wide.
        // 60 px east, ~80 px south: exact pixel geometry is not needed here,
        // only that the extrusion width is 8 px on a diagonal.
        const double lat_south = -80.0 * 360.0 / (256.0 * std::exp2(z));
        TestScene d; d.addLink(10.0, 0.0, 10.0 + 0.6 * span_deg, lat_south);
        MapBgfx2dMesh dm; dm.prepare(d.prepare(), no_icons);
        MapBgfx2dClipCamera du = MapBgfx2dChunking::cameraUniform(dm.chunks()[0].origin, cam);
        Px q0 = shaderMirror(dm.chunks()[0].link_vertices[0], du, 800,600,4,0), q1 = shaderMirror(dm.chunks()[0].link_vertices[1], du,800,600,4,0);
        req(std::fabs(std::hypot(q0.x-q1.x,q0.y-q1.y) - 8.0) < 1e-2, "diagonal width 8 px");
    }
    // 5. Metres -> pixels: equator zoom 0 -> 156543.03 m per pixel; continuous in zoom.
    req(std::fabs(MapBgfx2dChunking::pixelsForMeters(156543.03392804097, 0.0, 0.0) - 1.0) < 1e-9, "metres per pixel at z0");
    req(std::fabs(MapBgfx2dChunking::pixelsForMeters(10.0, 0.0, 15.5) / MapBgfx2dChunking::pixelsForMeters(10.0, 0.0, 15.0) - std::sqrt(2.0)) < 1e-12, "metre width continuous in zoom");
    // 6. Symbology -> colour stream (QRhi colour rules) and size uniforms.
    {
        MapNetworkRenderSymbology symbology;
        symbology.link_colors.insert(1, qRgb(0x12, 0x34, 0x56));
        symbology.node_colors.insert(100001, qRgb(0x20, 0x40, 0x60));
        symbology.show_junctions = false;
        req(MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Link, 1,
                                      InfrastructureEntity::Pipe, false) == 0xff563412u,
            "link colour from link_colors as ABGR");
        req(MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Link, 99,
                                      InfrastructureEntity::Pipe, false) == 0xff000000u,
            "missing link colour falls back to the default (black)");
        req(MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Attachment, 100001,
                                      InfrastructureEntity::DemandPoint, false) == 0xff604020u,
            "attachment takes its demand point colour");
        req((MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Node, 100001,
                                       InfrastructureEntity::Junction, false) >> 24) == 0u,
            "hidden junctions get alpha 0");
        req((MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Node, 100001,
                                       InfrastructureEntity::Tank, false) >> 24) == 0xffu,
            "show_junctions does not hide other nodes");
        // Icon rules (QRhi appendIcon / applyNodeColor).
        req((MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Node, 100001,
                                       InfrastructureEntity::Tank, true) >> 24) == 0u,
            "node dot hidden where its icon is shown");
        req(MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::NodeIcon, 100001,
                                      InfrastructureEntity::Tank, true)
                == mapBgfx2dAbgrFromArgb(symbology.icon_default_fill_color),
            "icon uses the default fill without node colourization");
        symbology.visual_node = VisualNode::Pressure;
        req(MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::NodeIcon, 100001,
                                      InfrastructureEntity::Tank, true) == 0xff604020u,
            "icon takes the node colour while node colourization is active");
        req(MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::NodeIcon, 4242,
                                      InfrastructureEntity::Tank, true)
                == mapBgfx2dAbgrFromArgb(networkSymbologyIconUnavailableFillColor()),
            "icon without a value uses the icon-unavailable fill");
        req(MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::LinkIcon, 1,
                                      InfrastructureEntity::Pump, true)
                == mapBgfx2dAbgrFromArgb(symbology.icon_default_fill_color),
            "link icon ignores node colourization");
        symbology.show_icons = false;
        req((MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::NodeIcon, 100001,
                                       InfrastructureEntity::Tank, true) >> 24) == 0u,
            "show_icons off hides icons");
        req((MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Node, 100001,
                                       InfrastructureEntity::Tank, true) >> 24) == 0xffu,
            "show_icons off brings the node dot back");
        symbology.show_icons = true;
        symbology.visual_node = VisualNode::None;

        // Colour stream sizes always match the geometry, and building it
        // never rebuilds geometry.
        const MapBgfx2dMesh::ColorLookup lookup =
            [&symbology](MapBgfx2dPrimitive primitive, quint32 render_id,
                         InfrastructureEntity entity_type)
        {
            return MapBgfx2dSymbology::color(symbology, primitive, render_id, entity_type, false);
        };
        const MapBgfx2dMesh::FlowLookup flow_lookup = [&symbology](quint32 render_id)
        {
            return MapBgfx2dSymbology::flow(symbology, render_id);
        };
        const QVector<MapBgfx2dChunkColors> colors = mesh.buildColors(lookup, flow_lookup);
        bool aligned = colors.size() == mesh.chunks().size();
        for (qsizetype index = 0; aligned && index < colors.size(); ++index)
        {
            aligned = colors[index].link.size() == mesh.chunks()[index].link_vertices.size()
                && colors[index].node.size() == mesh.chunks()[index].node_vertices.size()
                && colors[index].chevron.size() == mesh.chunks()[index].chevron_vertices.size();
        }
        req(aligned, "colour stream not aligned with geometry");
        req(mesh.preparationCount() == 1, "colour build rebuilt geometry");

        MapNetworkRenderSymbology pixels;
        pixels.link_thickness_unit = NetworkSymbologySizeUnit::Pixels;
        pixels.link_thickness_px = 5;
        pixels.node_size_unit = NetworkSymbologySizeUnit::Pixels;
        pixels.node_size_px = 12;
        const MapBgfx2dSymbology::Sizes pixel_sizes = MapBgfx2dSymbology::sizes(pixels, 50.0, 15.0);
        req(pixel_sizes.link_width_px == 5.0 && pixel_sizes.node_half_px == 6.0
                && pixel_sizes.attachment_width_px == MapBgfx2dSymbology::AttachmentWidthPixels,
            "pixel sizes map 1:1 to uniforms");
        MapNetworkRenderSymbology meters;
        meters.link_thickness_unit = NetworkSymbologySizeUnit::Meters;
        meters.link_thickness_m = 10.0;
        const double width_15 = MapBgfx2dSymbology::sizes(meters, 50.0, 15.0).link_width_px;
        const double width_155 = MapBgfx2dSymbology::sizes(meters, 50.0, 15.5).link_width_px;
        req(std::fabs(width_155 / width_15 - std::sqrt(2.0)) < 1e-12,
            "metre widths scale continuously with the zoom");
    }

    // 7. Node declutter: a pump's inlet and outlet digitized at one coordinate
    //    are spread 1 m apart; the connected pipe endpoint and a demand-point
    //    attachment follow their node's offset.
    {
        TestScene t;
        const quint32 far_node = t.addNode(8.0010, 47.0, InfrastructureEntity::Junction);
        const quint32 inlet = t.addNode(8.0, 47.0, InfrastructureEntity::Junction);
        const quint32 outlet = t.addNode(8.0, 47.0, InfrastructureEntity::Junction);
        const quint32 demand = t.addNode(8.0005, 47.0005, InfrastructureEntity::DemandPoint);
        const quint32 demand_twin = t.addNode(8.0005, 47.0005, InfrastructureEntity::DemandPoint);
        t.addLink(8.0010, 47.0, 8.0, 47.0, InfrastructureEntity::Pipe, far_node, inlet);
        t.addAttachment(demand, 8.0005, 47.0005, 8.0005, 47.0);
        const MapBgfx2dScene &declutter = t.prepare();
        const double meter = MapBgfx2dSceneMath::worldUnitsPerMeter(47.0);
        QHash<quint32, MapBgfx2d::WorldPoint> positions;
        for (const MapBgfx2dPoint &point : declutter.networkNodes())
            positions.insert(point.render_id, point.position);
        const MapBgfx2d::WorldPoint a = positions.value(inlet);
        const MapBgfx2d::WorldPoint b = positions.value(outlet);
        const MapBgfx2d::WorldPoint far_position = positions.value(far_node);
        req(std::fabs(std::hypot(a.x - b.x, a.y - b.y) / meter - 1.0) < 1e-3,
            "coincident nodes spread exactly 1 m apart");
        const MapBgfx2dPolyline &pipe = declutter.linkPolylines().first();
        req(std::fabs(pipe.points.last().x - a.x) < 1e-15 && std::fabs(pipe.points.last().y - a.y) < 1e-15,
            "pipe end follows its decluttered node");
        req(std::fabs(pipe.points.first().x - far_position.x) < 1e-15
                && std::fabs(pipe.points.first().y - far_position.y) < 1e-15,
            "pipe start at an undecluttered node is unchanged");
        const MapBgfx2d::WorldPoint d = positions.value(demand);
        const MapBgfx2d::WorldPoint d2 = positions.value(demand_twin);
        req(std::hypot(d.x - d2.x, d.y - d2.y) > 0.5 * meter, "coincident demand points spread");
        req(std::fabs(declutter.attachments().first().start.x - d.x) < 1e-15
                && std::fabs(declutter.attachments().first().start.y - d.y) < 1e-15,
            "attachment start follows its demand point's offset");

        // 8. Demand points are snapshot nodes: one marker each, not a second
        //    one from the attachment list.
        MapBgfx2dMesh declutter_mesh;
        declutter_mesh.prepare(declutter, no_icons);
        req(declutter.networkNodes().size() == 5 && declutter_mesh.nodeVertexCount() == 5 * 6,
            "each node incl. demand points staged exactly once");
        req(declutter_mesh.attachmentVertexCount() == 4, "one attachment quad");
    }

    // 9. Icons: node icons for non-junction nodes with a sprite, link icons
    //    (pumps/valves) at the middle of the link length; never on pipes.
    {
        TestScene t;
        const quint32 tank = t.addNode(9.0, 45.0, InfrastructureEntity::Tank);
        t.addNode(9.001, 45.0, InfrastructureEntity::Junction);
        t.addNode(9.002, 45.0, InfrastructureEntity::Reservoir); // no sprite in the fake atlas
        // Pump polyline with an off-centre bend: its middle is by length, not by vertex.
        QVector<CoordinateWGS84> pump_vertices;
        pump_vertices.append(coordinate(9.010, 45.0));
        pump_vertices.append(coordinate(9.011, 45.0));
        pump_vertices.append(coordinate(9.014, 45.0));
        const quint32 pump = t.addPolyline(pump_vertices, InfrastructureEntity::Pump);
        t.addLink(9.02, 45.0, 9.03, 45.0, InfrastructureEntity::Pipe);
        const MapBgfx2dMesh::IconLookup icons = fakeSprite;
        MapBgfx2dMesh icon_mesh;
        icon_mesh.prepare(t.prepare(), icons);
        req(icon_mesh.iconVertexCount() == 2 * 6, "tank and pump icons only");
        const MapBgfx2d::WorldPoint pump_start = MapBgfx2d::worldFromWgs84(9.010, 45.0);
        const MapBgfx2d::WorldPoint pump_end = MapBgfx2d::worldFromWgs84(9.014, 45.0);
        const MapBgfx2d::WorldPoint tank_world = MapBgfx2d::worldFromWgs84(9.0, 45.0);
        bool pump_icon_ok = false;
        bool tank_icon_ok = false;
        bool sprite_ok = true;
        for (const MapBgfx2dMeshChunk &chunk : icon_mesh.chunks())
        {
            for (qsizetype k = 0; k < chunk.icon_render_ids.size(); ++k)
            {
                const MapBgfx2d::WorldPoint at = worldOf(chunk, chunk.icon_vertices, k * 6);
                if (chunk.icon_on_link[k])
                    pump_icon_ok = chunk.icon_render_ids[k] == pump
                        && std::fabs(at.x - 0.5 * (pump_start.x + pump_end.x)) < 1e-10
                        && std::fabs(at.y - pump_start.y) < 1e-10;
                else
                    tank_icon_ok = chunk.icon_render_ids[k] == tank
                        && std::fabs(at.x - tank_world.x) < 1e-10 && std::fabs(at.y - tank_world.y) < 1e-10;
                // Corner 0 is the upper-left: (-half_w, -half_h) and (u0, v0);
                // corner 2 the lower-right: (u1, v1).
                const MapBgfx2dIconVertex &upper_left = chunk.icon_vertices[k * 6];
                const MapBgfx2dIconVertex &lower_right = chunk.icon_vertices[k * 6 + 2];
                sprite_ok = sprite_ok && upper_left.offset_x == -0.5f && upper_left.offset_y == -0.25f
                    && upper_left.u == 0.1f && upper_left.v == 0.2f
                    && lower_right.offset_x == 0.5f && lower_right.offset_y == 0.25f
                    && lower_right.u == 0.3f && lower_right.v == 0.4f;
            }
        }
        req(pump_icon_ok, "pump icon at the middle of the link length");
        req(tank_icon_ok, "tank icon at the tank");
        req(sprite_ok, "icon corners carry the sprite rectangle and aspect");
    }

    // 10. Flow chevrons: bounded per link, one always-visible centre, fully
    //     visible chevrons >= SpacingPixels apart at every zoom, and
    //     visibility continuous in the zoom (no popping at level boundaries).
    {
        TestScene t;
        const double lengths_m[4] = {30.0, 300.0, 3000.0, 30000.0};
        QVector<quint32> pipes;
        for (int i = 0; i < 4; ++i)
        {
            const double latitude = 50.0 + double(i) * 0.5;
            // Longitude span for a length in metres at this latitude.
            const double span_deg = 360.0 * lengths_m[i] * MapBgfx2dSceneMath::worldUnitsPerMeter(latitude);
            QVector<CoordinateWGS84> vertices;
            vertices.append(coordinate(10.0, latitude));
            vertices.append(coordinate(10.0 + 0.5 * span_deg, latitude));
            vertices.append(coordinate(10.0 + span_deg, latitude));
            pipes.append(t.addPolyline(vertices, InfrastructureEntity::Pipe));
        }
        const double pump_span_deg = 360.0 * 10.0 * MapBgfx2dSceneMath::worldUnitsPerMeter(52.5);
        const quint32 pump = t.addLink(11.0, 52.5, 11.0 + pump_span_deg, 52.5, InfrastructureEntity::Pump);
        MapBgfx2dMesh flow_mesh;
        flow_mesh.prepare(t.prepare(), no_icons);

        QHash<quint32, QVector<MapBgfx2dChevronVertex>> per_link;
        QHash<quint32, QVector<MapBgfx2d::WorldPoint>> per_link_world;
        for (const MapBgfx2dMeshChunk &chunk : flow_mesh.chunks())
        {
            for (qsizetype k = 0; k < chunk.chevron_render_ids.size(); ++k)
            {
                per_link[chunk.chevron_render_ids[k]].append(chunk.chevron_vertices[k * 4]);
                per_link_world[chunk.chevron_render_ids[k]].append(
                    worldOf(chunk, chunk.chevron_vertices, k * 4));
            }
        }
        bool bounded = per_link.size() == 5;
        for (QHash<quint32, QVector<MapBgfx2dChevronVertex>>::const_iterator it = per_link.cbegin();
             it != per_link.cend(); ++it)
        {
            int centres = 0;
            for (const MapBgfx2dChevronVertex &vertex : it.value())
                centres += vertex.level == MapBgfx2dFlow::AlwaysLevel ? 1 : 0;
            bounded = bounded && centres == 1
                && it.value().size() <= 2 * MapBgfx2dFlow::MaximumPerSide + 1;
        }
        req(bounded, "chevrons per link bounded, exactly one centre chevron");

        // Pump: the always-visible chevron sits at 30 % (the icon owns 50 %).
        {
            const MapBgfx2d::WorldPoint start = MapBgfx2d::worldFromWgs84(11.0, 52.5);
            const MapBgfx2d::WorldPoint end = MapBgfx2d::worldFromWgs84(11.0 + pump_span_deg, 52.5);
            bool centre_ok = false;
            for (qsizetype k = 0; k < per_link[pump].size(); ++k)
                if (per_link[pump][k].level == MapBgfx2dFlow::AlwaysLevel)
                    // 1e-10 world: far below the 2 m (~8e-8) between 30 % and 50 %,
                    // above the float error of chunk-local positions.
                    centre_ok = std::fabs(per_link_world[pump][k].x - (start.x + 0.3 * (end.x - start.x))) < 1e-10;
            req(centre_ok, "pump chevron at 30 % of the link");
        }

        double maximum_jump = 0.0;
        bool spacing_ok = true;
        bool density_ok = true;
        for (const quint32 pipe : pipes)
        {
            const QVector<MapBgfx2dChevronVertex> &chevrons = per_link[pipe];
            const QVector<MapBgfx2d::WorldPoint> &world = per_link_world[pipe];
            // The link's base level: its finest non-centre level.
            double base_level = double(MapBgfx2dFlow::AlwaysLevel);
            for (const MapBgfx2dChevronVertex &vertex : chevrons)
                if (vertex.level != MapBgfx2dFlow::AlwaysLevel)
                    base_level = std::min(base_level, double(vertex.level));
            for (double zoom = 10.0; zoom <= 19.0; zoom += 0.01)
            {
                const double world_pixels = 256.0 * std::exp2(zoom);
                const double next_pixels = 256.0 * std::exp2(zoom + 0.01);
                std::vector<double> shown;
                for (qsizetype k = 0; k < chevrons.size(); ++k)
                {
                    const double now = chevronVisibility(chevrons[k], world_pixels, 1.0);
                    const double next = chevronVisibility(chevrons[k], next_pixels, 1.0);
                    // The minimum-length cut is a deliberate step (same as QRhi).
                    if (double(chevrons[k].link_length) * world_pixels >= MapBgfx2dFlow::MinimumLinkPixels
                        && double(chevrons[k].link_length) * next_pixels >= MapBgfx2dFlow::MinimumLinkPixels)
                    {
                        maximum_jump = std::max(maximum_jump, std::fabs(now - next));
                    }
                    if (now >= 1.0)
                        shown.push_back(world[k].x * world_pixels);
                }
                std::sort(shown.begin(), shown.end());
                for (size_t index = 1; index < shown.size(); ++index)
                    spacing_ok = spacing_ok
                        && shown[index] - shown[index - 1] >= MapBgfx2dFlow::SpacingPixels - 0.01;
                // Density: unless the per-link cap coarsened the base level
                // (threshold below it), the hierarchy keeps fully visible
                // chevrons less than 2 * SpacingPixels apart, so a link of
                // L px shows at least 2 * floor((L/2 - S) / 2S) + 1 of them.
                const double link_pixels = double(chevrons.first().link_length) * world_pixels;
                if (MapBgfx2dFlow::thresholdLevel(world_pixels) >= base_level
                    && link_pixels * 0.5 >= MapBgfx2dFlow::SpacingPixels)
                {
                    const double required = 2.0 * std::floor((0.5 * link_pixels - MapBgfx2dFlow::SpacingPixels - 0.01)
                                                             / (2.0 * MapBgfx2dFlow::SpacingPixels)) + 1.0;
                    density_ok = density_ok && double(shown.size()) >= required;
                }
            }
        }
        req(spacing_ok, "fully visible chevrons are at least SpacingPixels apart");
        req(density_ok, "chevron hierarchy keeps links densely marked at every zoom");
        req(maximum_jump <= 0.0105, "chevron visibility is continuous in the zoom");
        std::printf("chevrons=%lld max visibility change per 0.01 zoom=%.4f\n",
                    static_cast<long long>(flow_mesh.chevronCount()), maximum_jump);

        // 11. Flow attribute: contrast colour against the link colour, sign
        //     from flow_directions; direction 0 or the switch off hides.
        MapNetworkRenderSymbology symbology;
        symbology.link_colors.insert(pipes[0], qRgb(250, 250, 250));
        symbology.link_colors.insert(pipes[1], qRgb(10, 20, 30));
        symbology.flow_directions.insert(pipes[0], 1);
        symbology.flow_directions.insert(pipes[1], -1);
        symbology.flow_directions.insert(pipes[2], 0);
        const MapBgfx2dFlowAttribute light = MapBgfx2dSymbology::flow(symbology, pipes[0]);
        const MapBgfx2dFlowAttribute dark = MapBgfx2dSymbology::flow(symbology, pipes[1]);
        req(light.sign == 1.0f && light.abgr == 0xff000000u, "black chevron on a light link, forward");
        req(dark.sign == -1.0f && dark.abgr == 0xffffffffu, "white chevron on a dark link, reverse");
        req(MapBgfx2dSymbology::flow(symbology, pipes[2]).sign == 0.0f, "no flow hides chevrons");
        req(MapBgfx2dSymbology::flow(symbology, pipes[3]).sign == 0.0f, "unknown flow hides chevrons");
        symbology.show_flow_direction = false;
        req(MapBgfx2dSymbology::flow(symbology, pipes[0]).sign == 0.0f, "switch off hides chevrons");
        symbology.show_flow_direction = true;

        const MapBgfx2dMesh::ColorLookup lookup =
            [&symbology](MapBgfx2dPrimitive primitive, quint32 render_id, InfrastructureEntity entity_type)
        {
            return MapBgfx2dSymbology::color(symbology, primitive, render_id, entity_type, false);
        };
        const MapBgfx2dMesh::FlowLookup flow_lookup = [&symbology](quint32 render_id)
        {
            return MapBgfx2dSymbology::flow(symbology, render_id);
        };
        const QVector<MapBgfx2dChunkColors> colors = flow_mesh.buildColors(lookup, flow_lookup);
        bool chevron_stream_ok = colors.size() == flow_mesh.chunks().size();
        for (qsizetype index = 0; chevron_stream_ok && index < colors.size(); ++index)
        {
            const MapBgfx2dMeshChunk &chunk = flow_mesh.chunks()[index];
            chevron_stream_ok = colors[index].chevron.size() == chunk.chevron_vertices.size();
            for (qsizetype k = 0; chevron_stream_ok && k < chunk.chevron_render_ids.size(); ++k)
            {
                const float expected = MapBgfx2dSymbology::flow(symbology, chunk.chevron_render_ids[k]).sign;
                for (int vertex = 0; vertex < 4; ++vertex)
                    chevron_stream_ok = chevron_stream_ok
                        && colors[index].chevron[k * 4 + vertex].sign == expected;
            }
        }
        req(chevron_stream_ok, "chevron attribute stream carries each link's flow per vertex");
    }

    // 12. Heatmap (slice 4c): one spot per node on the node quads, colour
    //     from the heatmap palette, no spot without a value, radius uniform
    //     continuous in the zoom, QRhi opacity/solid-centre bounds.
    {
        TestScene t;
        const quint32 with_value = t.addNode(7.0, 46.0, InfrastructureEntity::Junction);
        const quint32 without_value = t.addNode(7.01, 46.0, InfrastructureEntity::DemandPoint);
        const quint32 top_value = t.addNode(7.02, 46.0, InfrastructureEntity::Tank);
        MapBgfx2dMesh heatmap_mesh;
        heatmap_mesh.prepare(t.prepare(), no_icons);

        MapNetworkRenderSymbology symbology;
        symbology.visual_heatmap = VisualHeatmap::Pressure;
        symbology.heatmap_palette = NetworkSymbologyPalette::Viridis;
        symbology.heatmap_fractions.insert(with_value, 0.0);
        symbology.heatmap_fractions.insert(top_value, 1.0);
        // Viridis ends #440154 / #fde725 as ABGR.
        req(MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Heatmap, with_value,
                                      InfrastructureEntity::Junction, false) == 0xff540144u,
            "heatmap spot takes the palette start colour");
        req(MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Heatmap, top_value,
                                      InfrastructureEntity::Tank, false) == 0xff25e7fdu,
            "heatmap spot takes the palette end colour");
        req(MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Heatmap, without_value,
                                      InfrastructureEntity::DemandPoint, false) == 0u,
            "node without a heatmap value has no spot");
        symbology.heatmap_palette_flipped = true;
        req(MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Heatmap, with_value,
                                      InfrastructureEntity::Junction, false) == 0xff25e7fdu,
            "flipped palette reverses the spot colour");
        symbology.heatmap_palette_flipped = false;

        const MapBgfx2dMesh::ColorLookup lookup =
            [&symbology](MapBgfx2dPrimitive primitive, quint32 render_id, InfrastructureEntity entity_type)
        {
            return MapBgfx2dSymbology::color(symbology, primitive, render_id, entity_type, false);
        };
        const QVector<MapBgfx2dChunkColors> colors =
            heatmap_mesh.buildColors(lookup, MapBgfx2dMesh::FlowLookup());
        bool heatmap_stream_ok = colors.size() == heatmap_mesh.chunks().size();
        int spots = 0;
        for (qsizetype index = 0; heatmap_stream_ok && index < colors.size(); ++index)
        {
            const MapBgfx2dMeshChunk &chunk = heatmap_mesh.chunks()[index];
            heatmap_stream_ok = colors[index].heatmap.size() == chunk.node_vertices.size();
            for (qsizetype k = 0; heatmap_stream_ok && k < chunk.node_render_ids.size(); ++k)
            {
                const quint32 expected = MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Heatmap,
                    chunk.node_render_ids[k], chunk.node_entity_types[k], false);
                spots += (expected >> 24) != 0u ? 1 : 0;
                for (int vertex = 0; vertex < 6; ++vertex)
                    heatmap_stream_ok = heatmap_stream_ok && colors[index].heatmap[k * 6 + vertex] == expected;
            }
        }
        req(heatmap_stream_ok && spots == 2, "heatmap stream parallel to the node quads, one spot per valued node");

        // Sizes: metre radius continuous in the zoom, pixel radius fixed,
        // bounds as QRhi, pass disabled without a heatmap.
        symbology.heatmap_radius_unit = HeatmapRadiusUnit::Meters;
        symbology.heatmap_radius_m = 400;
        symbology.heatmap_opacity = 60;
        symbology.heatmap_solid_center_percent = 100;
        const MapBgfx2dSymbology::Sizes at_15 = MapBgfx2dSymbology::sizes(symbology, 46.0, 15.0);
        const MapBgfx2dSymbology::Sizes at_155 = MapBgfx2dSymbology::sizes(symbology, 46.0, 15.5);
        req(std::fabs(at_155.heatmap_radius_px / at_15.heatmap_radius_px - std::sqrt(2.0)) < 1e-12,
            "metre heatmap radius scales continuously with the zoom");
        req(std::fabs(at_15.heatmap_radius_px - MapBgfx2dChunking::pixelsForMeters(400.0, 46.0, 15.0)) < 1e-9,
            "metre heatmap radius in pixels");
        req(std::fabs(at_15.heatmap_opacity - 0.6) < 1e-12 && at_15.heatmap_solid_fraction == 0.9,
            "heatmap opacity and solid centre bounds");
        symbology.heatmap_radius_unit = HeatmapRadiusUnit::Pixels;
        symbology.heatmap_radius_px = 50;
        req(MapBgfx2dSymbology::sizes(symbology, 46.0, 12.0).heatmap_radius_px == 50.0
                && MapBgfx2dSymbology::sizes(symbology, 46.0, 18.3).heatmap_radius_px == 50.0,
            "pixel heatmap radius is zoom independent");
        symbology.visual_heatmap = VisualHeatmap::None;
        req(MapBgfx2dSymbology::sizes(symbology, 46.0, 15.0).heatmap_radius_px == 0.0
                && MapBgfx2dSymbology::color(symbology, MapBgfx2dPrimitive::Heatmap, top_value,
                                             InfrastructureEntity::Tank, false) == 0u,
            "no heatmap selected: pass disabled, no spots");
        symbology.visual_heatmap = VisualHeatmap::Pressure;
        symbology.heatmap_opacity = 0;
        req(MapBgfx2dSymbology::sizes(symbology, 46.0, 15.0).heatmap_radius_px == 0.0,
            "zero opacity disables the heatmap pass");
    }

    std::printf(fails ? "FAILED %d\n" : "mesh verification passed\n", fails);
    return fails ? 1 : 0;
}
