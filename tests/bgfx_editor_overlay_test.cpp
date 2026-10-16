#include "map/bgfx/map_bgfx_editor_overlay.h"
#include "map/bgfx/map_bgfx_hud_canvas.h"
#include "map/bgfx/map_bgfx_hud_text_cache.h"

#include <QGuiApplication>
#include <QImage>
#include <QVector>

#include <cmath>
#include <cstdio>
#include <functional>

// Map editor on bgfx (roadmap Phase 2) without a GPU: what the overlay
// builder draws for each editor state, placed with the continuous camera
// exactly where the GPU network is (icons, pipe vertices, selection,
// placement, move preview, full-network move, diagnostics, selection frames,
// culling, antimeridian).
namespace
{
int fails = 0;

void req(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++fails;
    }
}

constexpr double ViewportWidth = 800.0;
constexpr double ViewportHeight = 600.0;
constexpr int EntityWidth = 20;
constexpr double IconHeight = 24.0;

using Highlight = MapBgfxEditorOverlay::Highlight;

struct IconCall
{
    InfrastructureEntity entity = InfrastructureEntity::Unknown;
    int width = 0;
    Highlight highlight = Highlight::None;
};

// A provider that hands out one solid sprite per (entity, highlight) and
// records every request.
struct IconProbe
{
    QVector<IconCall> calls;

    MapBgfxEditorOverlay::Icon icon(MapBgfxHudTextCache *cache, InfrastructureEntity entity,
                                    int width, Highlight highlight)
    {
        calls.append(IconCall{entity, width, highlight});
        MapBgfxEditorOverlay::Icon result;
        result.width = double(width);
        result.height = IconHeight;
        result.padding = highlight == Highlight::None ? 0.0 : 9.0;
        QImage image(int(result.width + 2.0 * result.padding),
                     int(result.height + 2.0 * result.padding), QImage::Format_ARGB32);
        image.fill(qRgba(10 * int(entity), 20, 30 * int(highlight) % 256, 255));
        result.sprite = cache->image(QStringLiteral("probe-%1-%2").arg(int(entity)).arg(int(highlight)),
                                     image, QSizeF(image.width(), image.height()));
        return result;
    }
    int count(Highlight highlight) const
    {
        int result = 0;
        for (const IconCall &call : calls)
            result += call.highlight == highlight ? 1 : 0;
        return result;
    }
};

CoordinateWGS84 coordinate(double longitude, double latitude)
{
    CoordinateWGS84 result;
    result.longitude_deg = longitude;
    result.latitude_deg = latitude;
    return result;
}

struct Scene
{
    NetworkRenderSnapshot snapshot;
    MapEditorVisualState state;
    MapEditorViewportRenderState viewport;
    QUuid junction_a;
    QUuid junction_b;
    QUuid pipe;
    QUuid pump;
};

// Two junctions, a pipe between them with one intermediate vertex, a pump
// from B to a third junction, around (9.0, 47.0).
Scene scene()
{
    Scene result;
    result.snapshot.geometry_revision = 7;
    const double lons[3] = {8.9995, 9.0005, 9.0010};
    for (int index = 0; index < 3; ++index)
    {
        NetworkRenderNode node;
        node.render_id = quint32(index + 1);
        node.uuid = QUuid::createUuid();
        node.entity_type = InfrastructureEntity::Junction;
        node.coordinate_wgs84 = coordinate(lons[index], 47.0);
        result.snapshot.nodes.append(node);
    }
    result.junction_a = result.snapshot.nodes.at(0).uuid;
    result.junction_b = result.snapshot.nodes.at(1).uuid;
    NetworkRenderLink pipe;
    pipe.render_id = 1;
    pipe.uuid = QUuid::createUuid();
    pipe.entity_type = InfrastructureEntity::Pipe;
    pipe.vertices_wgs84.append(coordinate(8.9995, 47.0));
    pipe.vertices_wgs84.append(coordinate(9.0, 47.0002));
    pipe.vertices_wgs84.append(coordinate(9.0005, 47.0));
    result.snapshot.links.append(pipe);
    result.pipe = pipe.uuid;
    NetworkRenderLink pump;
    pump.render_id = 2;
    pump.uuid = QUuid::createUuid();
    pump.entity_type = InfrastructureEntity::Pump;
    pump.vertices_wgs84.append(coordinate(9.0005, 47.0));
    pump.vertices_wgs84.append(coordinate(9.0010, 47.0));
    result.snapshot.links.append(pump);
    result.pump = pump.uuid;
    result.state.entity_width = EntityWidth;
    return result;
}

MapBgfx2d::Camera camera(double longitude = 9.0, double latitude = 47.0)
{
    return MapBgfx2d::cameraFor(longitude, latitude, 18.0, ViewportWidth, ViewportHeight);
}

MapBgfxHudPoint project(const MapBgfx2d::Camera &view, double longitude, double latitude)
{
    const MapBgfx2d::ScreenPoint point =
        MapBgfx2d::screenFromWorld(view, MapBgfx2d::worldFromWgs84(longitude, latitude));
    return MapBgfxHudPoint{point.x, point.y};
}

bool hasColorNear(const MapBgfxHudCanvas &canvas, quint32 argb, double x, double y, double radius)
{
    const quint32 abgr = MapBgfxHudCanvas::premultipliedAbgr(argb);
    for (const MapBgfxHudVertex &vertex : canvas.vertices())
    {
        if (vertex.abgr == abgr && std::fabs(vertex.x - x) <= radius
            && std::fabs(vertex.y - y) <= radius)
            return true;
    }
    return false;
}

// A sprite vertex (white tint, not the white texel) at (x, y).
bool hasSpriteCorner(const MapBgfxHudCanvas &canvas, const MapBgfxHudTextCache &cache,
                     double x, double y)
{
    for (const MapBgfxHudVertex &vertex : canvas.vertices())
    {
        const bool white_texel = vertex.u == cache.whiteU() && vertex.v == cache.whiteV();
        if (!white_texel && vertex.abgr == 0xffffffffu && std::fabs(vertex.x - x) < 0.01
            && std::fabs(vertex.y - y) < 0.01)
            return true;
    }
    return false;
}

struct Harness
{
    MapBgfxEditorOverlay overlay;
    IconProbe probe;
    MapBgfxHudTextCache cache{1024};
    MapBgfxHudCanvas canvas;

    Harness()
    {
        overlay.setIconProvider([this](MapBgfxHudTextCache *atlas, InfrastructureEntity entity,
                                       const QString &, int width, Highlight highlight)
        {
            return probe.icon(atlas, entity, width, highlight);
        });
    }
    void build(const Scene &scene, const MapBgfx2d::Camera &view, bool full_move = false,
               QPointF translation = QPointF())
    {
        probe.calls.clear();
        MapBgfxEditorOverlay::Inputs inputs;
        inputs.viewport = QSizeF(ViewportWidth, ViewportHeight);
        inputs.camera = view;
        inputs.snapshot = &scene.snapshot;
        inputs.visual_state = &scene.state;
        inputs.viewport_state = &scene.viewport;
        inputs.full_network_move = full_move;
        inputs.full_network_move_translation = translation;
        overlay.build(&canvas, &cache, inputs);
    }
};

void testStaticDetails()
{
    Harness harness;
    Scene s = scene();
    const MapBgfx2d::Camera view = camera();
    harness.build(s, view);
    req(harness.probe.calls.size() == 4, "one icon per node and one for the pump");
    req(harness.probe.count(Highlight::None) == 4, "unselected icons have no glow");
    for (const IconCall &call : harness.probe.calls)
        req(call.width == EntityWidth, "icons use the marker width");

    // Bottom-anchored at the rounded node position (no padding without glow).
    const MapBgfxHudPoint a = project(view, 8.9995, 47.0);
    req(hasSpriteCorner(harness.canvas, harness.cache, double(qRound(a.x)), double(qRound(a.y))),
        "node icon's bottom-left corner sits on the rounded node position");
    req(hasSpriteCorner(harness.canvas, harness.cache, double(qRound(a.x)),
                        double(qRound(a.y)) - IconHeight),
        "node icon extends upwards by its height");

    // The pipe's intermediate vertex gets a black dot; the end nodes do not.
    const MapBgfxHudPoint vertex = project(view, 9.0, 47.0002);
    req(hasColorNear(harness.canvas, MapBgfxEditorOverlay::BlackColor, vertex.x, vertex.y,
                     MapBgfxEditorOverlay::PipeVertexRadiusPx + 0.01),
        "pipe vertex dot at the intermediate vertex");
    // The pump icon is centred at the link middle.
    const MapBgfxHudPoint pump_center = project(view, 0.5 * (9.0005 + 9.0010), 47.0);
    req(hasSpriteCorner(harness.canvas, harness.cache, pump_center.x - 0.5 * EntityWidth,
                        pump_center.y - 0.5 * IconHeight),
        "device link icon is centred on the link");

    // Culling: far away from the camera nothing is requested.
    harness.build(s, camera(20.0, 47.0));
    req(harness.probe.calls.isEmpty(), "off-screen icons are culled before the provider");
    req(!harness.canvas.overflowed(), "overlay draw list stays within budget");
}

void testSelection()
{
    Harness harness;
    Scene s = scene();
    const MapBgfx2d::Camera view = camera();
    s.state.selected_pipe_uuids.append(s.pipe);
    s.state.selected_marker_uuids.append(s.junction_b);
    s.state.selected_marker_uuids.append(s.pump);
    harness.build(s, view);
    const MapBgfxHudPoint vertex = project(view, 9.0, 47.0002);
    req(hasColorNear(harness.canvas, MapBgfxEditorOverlay::SelectedColor, vertex.x, vertex.y, 4.1),
        "selected pipe is drawn blue with its vertices");
    req(harness.probe.count(Highlight::Selected) == 2, "selected node and pump glow");
    const MapBgfxHudPoint b = project(view, 9.0005, 47.0);
    // Glowing icon: padded by 9 around the bottom-anchored rect.
    req(hasSpriteCorner(harness.canvas, harness.cache, double(qRound(b.x)) - 9.0,
                        double(qRound(b.y)) + 9.0),
        "selected icon carries its glow padding");
}

void testPlacement()
{
    Harness harness;
    Scene s = scene();
    const MapBgfx2d::Camera view = camera();
    s.state.placement.creating = true;
    s.state.placement.entity = InfrastructureEntity::Pipe;
    s.state.placement.pipe_start_node_uuid = s.junction_a;
    s.state.placement.mouse_position = QPointF(700.0, 500.0);
    s.state.placement.floating_marker_visible = false;
    harness.build(s, view);
    req(hasColorNear(harness.canvas, MapBgfxEditorOverlay::PreviewColor, 700.0, 500.0, 2.0),
        "pipe rubber band ends at the pointer");
    const MapBgfxHudPoint a = project(view, 8.9995, 47.0);
    req(hasColorNear(harness.canvas, MapBgfxEditorOverlay::PreviewColor, a.x, a.y, 2.0),
        "pipe rubber band starts at the start node");

    // Snapped to a target node: ends there, and the target is ringed.
    s.state.placement.connection_target_uuid = s.junction_b;
    harness.build(s, view);
    const MapBgfxHudPoint b = project(view, 9.0005, 47.0);
    req(hasColorNear(harness.canvas, MapBgfxEditorOverlay::PreviewColor, b.x + 9.0, b.y, 0.6),
        "connection target gets the 9 px circle");
    req(!hasColorNear(harness.canvas, MapBgfxEditorOverlay::PreviewColor, 700.0, 500.0, 2.0),
        "a snapped rubber band no longer follows the pointer");

    // Floating marker of a new junction under the pointer.
    Scene floating = scene();
    floating.state.placement.creating = true;
    floating.state.placement.entity = InfrastructureEntity::Tank;
    floating.state.placement.floating_marker_visible = true;
    floating.state.placement.floating_width = 30;
    floating.state.placement.mouse_position = QPointF(400.5, 300.5);
    harness.build(floating, view);
    bool tank = false;
    for (const IconCall &call : harness.probe.calls)
        tank = tank || (call.entity == InfrastructureEntity::Tank && call.width == 30);
    req(tank, "floating marker uses the floating width");
    req(hasSpriteCorner(harness.canvas, harness.cache, 400.5, 300.5),
        "floating marker is bottom-anchored at the unrounded pointer");
}

void testMove()
{
    Harness harness;
    Scene s = scene();
    const MapBgfx2d::Camera view = camera();
    s.state.move.active = true;
    s.state.move.session_id = 3;
    MapEditorDynamicMarkerVisualState moved;
    moved.entity = InfrastructureEntity::Junction;
    moved.uuid = s.junction_a;
    moved.coordinate_wgs84 = coordinate(8.9992, 47.0003);
    s.state.move.markers.append(moved);
    harness.build(s, view);
    const MapBgfxHudPoint old_position = project(view, 8.9995, 47.0);
    const MapBgfxHudPoint new_position = project(view, 8.9992, 47.0003);
    req(!hasSpriteCorner(harness.canvas, harness.cache, double(qRound(old_position.x)),
                         double(qRound(old_position.y))),
        "the moved node is not drawn at its old place");
    req(hasSpriteCorner(harness.canvas, harness.cache, double(qRound(new_position.x)),
                        double(qRound(new_position.y))),
        "the moved node's icon follows the drag");
    req(hasColorNear(harness.canvas, MapBgfxEditorOverlay::BlackColor, new_position.x,
                     new_position.y, MapBgfxEditorOverlay::MarkerDotRadiusPx + 0.01),
        "the moved node gets its dot (hidden in the GPU network)");

    // Full-network move: everything shifted by the translation.
    Scene all = scene();
    all.state.move.active = true;
    harness.build(all, view, true, QPointF(30.0, -10.0));
    const MapBgfxHudPoint a = project(view, 8.9995, 47.0);
    req(hasSpriteCorner(harness.canvas, harness.cache, double(qRound(a.x + 30.0)),
                        double(qRound(a.y - 10.0))),
        "full-network move shifts the icons by the network translation");
    req(!hasSpriteCorner(harness.canvas, harness.cache, double(qRound(a.x)), double(qRound(a.y))),
        "nothing remains at the unshifted position");
}

void testDiagnostics()
{
    Harness harness;
    Scene s = scene();
    const MapBgfx2d::Camera view = camera();
    s.state.simulation_error_entities.insert(s.pipe, InfrastructureEntity::Pipe);
    s.state.simulation_error_entities.insert(s.junction_b, InfrastructureEntity::Junction);
    s.state.simulation_stale_diagnostic_entity_uuids.insert(s.junction_b);
    harness.build(s, view);
    const MapBgfxHudPoint vertex = project(view, 9.0, 47.0002);
    req(hasColorNear(harness.canvas, MapBgfxEditorOverlay::ErrorColor, vertex.x, vertex.y, 2.0),
        "pipe with a simulation error is drawn red");
    req(harness.probe.count(Highlight::Stale) == 1, "stale node error glows grey");

    s.state.selected_pipe_uuids.append(s.pipe);
    harness.build(s, view);
    req(hasColorNear(harness.canvas, MapBgfxEditorOverlay::SelectedColor, vertex.x, vertex.y, 4.0),
        "a selected erroneous pipe gets the blue outline");
}

void testFramesAndAttachments()
{
    Harness harness;
    Scene s = scene();
    const MapBgfx2d::Camera view = camera();
    s.viewport.rectangle_selection_visible = true;
    s.viewport.rectangle_selection = QRect(100, 100, 200, 120);
    harness.build(s, view);
    bool inside = true;
    int frame_vertices = 0;
    for (const MapBgfxHudVertex &vertex : harness.canvas.vertices())
    {
        if (vertex.x > 60.0 && vertex.x < 340.0 && vertex.y > 60.0 && vertex.y < 260.0)
            ++frame_vertices;
        else if (vertex.x < 300.0 && vertex.y < 300.0)
            inside = false;
    }
    req(frame_vertices > 50, "rectangle selection draws its frame");
    req(inside, "rectangle selection stays around its rectangle (glow included)");

    // Tile selection: the tile under the camera at zoom 18, drawn green.
    Scene tiles = scene();
    const double tiles_per_axis = std::exp2(18.0);
    const MapBgfx2d::WorldPoint center = MapBgfx2d::worldFromWgs84(9.0, 47.0);
    tiles.viewport.tile_selection_visible = true;
    tiles.viewport.tile_selection_zoom = 18;
    tiles.viewport.tile_x_min = int(std::floor(center.x * tiles_per_axis));
    tiles.viewport.tile_x_max = tiles.viewport.tile_x_min + 1;
    tiles.viewport.tile_y_min = int(std::floor(center.y * tiles_per_axis));
    tiles.viewport.tile_y_max = tiles.viewport.tile_y_min;
    harness.build(tiles, view);
    const double tile_left = ViewportWidth * 0.5
        + (double(tiles.viewport.tile_x_min) / tiles_per_axis - center.x) * view.world_pixels;
    req(hasColorNear(harness.canvas, 0xe69bff91u, tile_left, 300.0, 300.0),
        "tile selection border is drawn");
    // The inner grid line between the two tiles.
    req(hasColorNear(harness.canvas, 0x6968ff68u, tile_left + 256.0, 300.0, 300.0),
        "tile selection grid line between tiles");

    // Demand-point attachment: dashed (several separate dashes).
    Scene attachment = scene();
    MapEditorDemandPointAttachmentVisualState state;
    state.visible = true;
    state.demand_point_coordinate_wgs84 = coordinate(9.0, 46.9995);
    state.attachment_coordinate_wgs84 = coordinate(9.0, 47.0);
    state.attachment_coordinate_valid = true;
    attachment.state.demand_point_attachments.append(state);
    harness.build(attachment, view);
    int dash_vertices = 0;
    const quint32 dash = MapBgfxHudCanvas::premultipliedAbgr(qRgba(70, 105, 125, 190));
    for (const MapBgfxHudVertex &vertex : harness.canvas.vertices())
        dash_vertices += vertex.abgr == dash ? 1 : 0;
    req(dash_vertices >= 8 * 4, "attachment line is dashed (many dash quads)");
}

// Attachments run per frame for every demand point: culled, clipped, and
// without handles where the line is too short to show one.
void testAttachmentCost()
{
    Harness harness;
    const MapBgfx2d::Camera view = camera();
    const quint32 dash = MapBgfxHudCanvas::premultipliedAbgr(qRgba(70, 105, 125, 190));
    const quint32 handle = MapBgfxHudCanvas::premultipliedAbgr(qRgba(255, 255, 255, 190));
    const std::function<int(const MapBgfxHudCanvas &, quint32)> count =
        [](const MapBgfxHudCanvas &canvas, quint32 abgr)
    {
        int result = 0;
        for (const MapBgfxHudVertex &vertex : canvas.vertices())
            result += vertex.abgr == abgr ? 1 : 0;
        return result;
    };
    const std::function<MapEditorDemandPointAttachmentVisualState(double, double, double, double)>
        attachment = [](double lon0, double lat0, double lon1, double lat1)
    {
        MapEditorDemandPointAttachmentVisualState state;
        state.visible = true;
        state.demand_point_coordinate_wgs84 = coordinate(lon0, lat0);
        state.attachment_coordinate_wgs84 = coordinate(lon1, lat1);
        state.attachment_coordinate_valid = true;
        return state;
    };

    // Entirely off screen (0.01 deg = ~1900 px away): nothing at all.
    Scene away = scene();
    away.snapshot.nodes.clear();
    away.snapshot.links.clear();
    away.state.demand_point_attachments.append(attachment(9.01, 47.0, 9.0101, 47.0));
    harness.build(away, view);
    req(harness.canvas.vertices().isEmpty(), "attachment: off-screen attachment draws nothing");

    // Crossing the screen from far away: only the visible dashes, a few
    // hundred, not the ~1000 of the whole line.
    Scene across = away;
    across.state.demand_point_attachments.clear();
    across.state.demand_point_attachments.append(attachment(8.99, 47.0, 9.01, 47.0));
    harness.build(across, view);
    const int dashes = count(harness.canvas, dash) / 4;
    req(dashes > 50 && dashes < 160, "attachment: dashes clipped to the viewport");
    req(count(harness.canvas, handle) == 0, "attachment: no handle when it is off screen");

    // Shorter on screen than MinimumAttachmentHandleLinePx: no handle.
    Scene tiny = away;
    tiny.state.demand_point_attachments.clear();
    tiny.state.demand_point_attachments.append(attachment(9.0, 47.0, 9.00003, 47.0));
    harness.build(tiny, view);
    req(count(harness.canvas, handle) == 0, "attachment: no handle on a short line");
    tiny.state.demand_point_attachments[0].selected = true;
    harness.build(tiny, view);
    const quint32 selected_handle =
        MapBgfxHudCanvas::premultipliedAbgr(qRgba(255, 255, 255, 230));
    req(count(harness.canvas, selected_handle) > 0, "attachment: a selected one keeps its handle");
}

void testAntimeridian()
{
    Harness harness;
    Scene s = scene();
    s.snapshot.geometry_revision = 9;
    s.snapshot.nodes[0].coordinate_wgs84 = coordinate(-179.9999, 0.0);
    s.snapshot.links.clear();
    const MapBgfx2d::Camera view = camera(179.9999, 0.0);
    harness.build(s, view);
    const MapBgfx2d::WorldPoint node = MapBgfx2d::worldFromWgs84(-179.9999, 0.0);
    const MapBgfx2d::ScreenPoint screen = MapBgfx2d::screenFromWorld(view, node);
    req(std::fabs(screen.x - 0.5 * ViewportWidth) < 200.0, "node across the antimeridian is near the camera");
    req(hasSpriteCorner(harness.canvas, harness.cache, double(qRound(screen.x)), double(qRound(screen.y))),
        "its icon is drawn at the nearest world copy");
}
}

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);

    testStaticDetails();
    testSelection();
    testPlacement();
    testMove();
    testDiagnostics();
    testFramesAndAttachments();
    testAttachmentCost();
    testAntimeridian();

    if (fails == 0)
        std::printf("bgfx editor overlay tests passed\n");
    return fails == 0 ? 0 : 1;
}
