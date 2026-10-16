#include "map/bgfx/map_bgfx_editor_overlay.h"

#include "network/infrastructure_entity_traits.h"

#include <algorithm>
#include <cmath>

namespace
{
// Glow colours of the selection frames (as the former QPainter editor).
constexpr quint32 rgba(int red, int green, int blue, int alpha)
{
    return (quint32(alpha & 0xff) << 24) | (quint32(red & 0xff) << 16)
        | (quint32(green & 0xff) << 8) | quint32(blue & 0xff);
}

bool isPointEntity(InfrastructureEntity entity)
{
    return InfrastructureEntityTraits::isHydraulicPointEntity(entity);
}

bool isDeviceLink(InfrastructureEntity entity)
{
    return InfrastructureEntityTraits::isHydraulicDeviceLink(entity);
}

double normalizeLongitude(double longitude)
{
    double result = std::fmod(longitude + 180.0, 360.0);
    if (result < 0.0)
        result += 360.0;
    return result - 180.0;
}

// The former QPainter editor's deviceLinkCenterCoordinate().
CoordinateWGS84 deviceLinkCenter(const QList<CoordinateWGS84> &vertices)
{
    if (vertices.size() > 2)
        return vertices.at(1);
    CoordinateWGS84 center;
    if (vertices.size() < 2)
        return center;
    const CoordinateWGS84 &start = vertices.first();
    const CoordinateWGS84 &end = vertices.last();
    center.latitude_deg = (start.latitude_deg + end.latitude_deg) / 2.0;
    const double longitude_delta = normalizeLongitude(end.longitude_deg - start.longitude_deg);
    center.longitude_deg = normalizeLongitude(start.longitude_deg + longitude_delta / 2.0);
    return center;
}

MapBgfx2d::WorldPoint world(const CoordinateWGS84 &coordinate)
{
    return MapBgfx2d::worldFromWgs84(coordinate.longitude_deg, coordinate.latitude_deg);
}

bool finite(const CoordinateWGS84 &coordinate)
{
    return std::isfinite(coordinate.longitude_deg) && std::isfinite(coordinate.latitude_deg);
}

MapBgfxHudPoint midpoint(const MapBgfxHudPoint &a, const MapBgfxHudPoint &b)
{
    return MapBgfxHudPoint{0.5 * (a.x + b.x), 0.5 * (a.y + b.y)};
}

MapBgfxEditorOverlay::Highlight diagnosticHighlight(bool selected, bool stale)
{
    if (selected)
        return stale ? MapBgfxEditorOverlay::Highlight::SelectedStale
                     : MapBgfxEditorOverlay::Highlight::SelectedError;
    return stale ? MapBgfxEditorOverlay::Highlight::Stale
                 : MapBgfxEditorOverlay::Highlight::Error;
}
}

void MapBgfxEditorOverlay::synchronizeGeometry(const NetworkRenderSnapshot &snapshot)
{
    if (this->geometry.valid && this->geometry.geometry_revision == snapshot.geometry_revision
        && this->geometry.node_world.size() == snapshot.nodes.size()
        && this->geometry.link_world.size() == snapshot.links.size())
    {
        this->geometry.snapshot = &snapshot;
        return;
    }
    Geometry built;
    built.valid = true;
    built.geometry_revision = snapshot.geometry_revision;
    built.snapshot = &snapshot;
    built.node_world.reserve(snapshot.nodes.size());
    for (qsizetype index = 0; index < snapshot.nodes.size(); ++index)
    {
        const NetworkRenderNode &node = snapshot.nodes.at(index);
        built.node_world.append(world(node.coordinate_wgs84));
        built.node_index.insert(node.uuid, int(index));
    }
    built.link_world.reserve(snapshot.links.size());
    built.device_center_world.reserve(snapshot.links.size());
    for (qsizetype index = 0; index < snapshot.links.size(); ++index)
    {
        const NetworkRenderLink &link = snapshot.links.at(index);
        QVector<MapBgfx2d::WorldPoint> points;
        points.reserve(link.vertices_wgs84.size());
        for (const CoordinateWGS84 &vertex : link.vertices_wgs84)
            points.append(world(vertex));
        built.link_world.append(points);
        built.device_center_world.append(world(deviceLinkCenter(link.vertices_wgs84)));
        built.link_index.insert(link.uuid, int(index));
    }
    this->geometry = std::move(built);
}

void MapBgfxEditorOverlay::build(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                                 const Inputs &inputs)
{
    canvas->clear();
    if (inputs.snapshot == nullptr || inputs.visual_state == nullptr
        || inputs.viewport_state == nullptr || !(inputs.camera.world_pixels > 0.0))
        return;
    this->canvas = canvas;
    this->cache = cache;
    this->inputs = &inputs;
    synchronizeGeometry(*inputs.snapshot);

    // Membership tests per entity: sets instead of the visual state's lists
    // (a full selection would make every icon an O(n) scan).
    const MapEditorVisualState &state = *inputs.visual_state;
    this->selected_markers = QSet<QUuid>(state.selected_marker_uuids.cbegin(),
                                         state.selected_marker_uuids.cend());
    this->selected_pipes = QSet<QUuid>(state.selected_pipe_uuids.cbegin(),
                                       state.selected_pipe_uuids.cend());
    this->moving_markers.clear();
    this->moving_links.clear();
    if (state.move.active)
    {
        for (const MapEditorDynamicMarkerVisualState &marker : state.move.markers)
            this->moving_markers.insert(marker.uuid);
        for (const MapEditorDynamicLinkVisualState &link : state.move.links)
            this->moving_links.insert(link.uuid);
    }

    cache->beginFrame();
    buildOnce();
    if (cache->evictedSinceFrameStart())
    {
        // The atlas was cleared mid-build: rebuild against the refilled atlas.
        cache->beginFrame();
        canvas->clear();
        buildOnce();
    }
    this->canvas = nullptr;
    this->cache = nullptr;
    this->inputs = nullptr;
}

void MapBgfxEditorOverlay::buildOnce()
{
    this->canvas->setWhiteTexel(this->cache->whiteU(), this->cache->whiteV());
    this->offset_x = 0.0;
    this->offset_y = 0.0;
    const MapEditorVisualState &state = *this->inputs->visual_state;

    // The former QPainter editor's paintRenderSurfaceOverlay(), same order.
    tileSelection();
    rectangleSelection();

    if (state.move.active && this->inputs->full_network_move)
    {
        // The GPU network is already shifted; draw the icons and vertices
        // once at the same translation, selected ones highlighted.
        this->offset_x = this->inputs->full_network_move_translation.x();
        this->offset_y = this->inputs->full_network_move_translation.y();
        staticDetails(true);
        this->offset_x = 0.0;
        this->offset_y = 0.0;
        return;
    }

    staticDetails(false);
    if (state.move.active)
    {
        movingNetwork();
        return;
    }
    selectedPipes();
    pipePlacement();
    deviceLinkPlacement();
    selectedMarkersAndDeviceLinks();
    simulationErrors();
    floatingPlacement();
}

// --- Projection and pens ----------------------------------------------------------

MapBgfxHudPoint MapBgfxEditorOverlay::screen(const MapBgfx2d::WorldPoint &point) const
{
    const MapBgfx2d::ScreenPoint projected = MapBgfx2d::screenFromWorld(this->inputs->camera, point);
    return MapBgfxHudPoint{projected.x + this->offset_x, projected.y + this->offset_y};
}

MapBgfxHudPoint MapBgfxEditorOverlay::screen(const CoordinateWGS84 &coordinate) const
{
    return screen(world(coordinate));
}

QVector<MapBgfxHudPoint> MapBgfxEditorOverlay::screenPolyline(
    const QVector<MapBgfx2d::WorldPoint> &points) const
{
    QVector<MapBgfxHudPoint> result;
    if (points.isEmpty())
        return result;
    const MapBgfx2d::Camera &camera = this->inputs->camera;
    // Continuous along the link, then the copy whose middle is nearest to
    // the camera.
    QVector<double> xs;
    xs.reserve(points.size());
    double previous = points.first().x;
    for (const MapBgfx2d::WorldPoint &point : points)
    {
        const double x = MapBgfx2d::wrapNearest(point.x, previous);
        xs.append(x);
        previous = x;
    }
    const double middle = 0.5 * (xs.first() + xs.last());
    const double shift = std::round(camera.center.x - middle);
    result.reserve(points.size());
    for (qsizetype index = 0; index < points.size(); ++index)
    {
        result.append(MapBgfxHudPoint{
            camera.viewport_width * 0.5 + (xs.at(index) + shift - camera.center.x)
                * camera.world_pixels + this->offset_x,
            camera.viewport_height * 0.5 + (points.at(index).y - camera.center.y)
                * camera.world_pixels + this->offset_y});
    }
    return result;
}

QVector<MapBgfxHudPoint> MapBgfxEditorOverlay::screenPolyline(
    const QList<CoordinateWGS84> &coordinates) const
{
    QVector<MapBgfx2d::WorldPoint> points;
    points.reserve(coordinates.size());
    for (const CoordinateWGS84 &coordinate : coordinates)
        points.append(world(coordinate));
    return screenPolyline(points);
}

void MapBgfxEditorOverlay::polyline(const QVector<MapBgfxHudPoint> &points, double width,
                                    quint32 argb)
{
    if (points.size() < 2)
        return;
    for (qsizetype index = 1; index < points.size(); ++index)
    {
        const MapBgfxHudPoint &a = points.at(index - 1);
        const MapBgfxHudPoint &b = points.at(index);
        this->canvas->line(a.x, a.y, b.x, b.y, width, argb);
    }
    // Round caps and joins (Qt::RoundCap / Qt::RoundJoin).
    for (const MapBgfxHudPoint &point : points)
        this->canvas->fillCircle(point.x, point.y, 0.5 * width, argb);
}

void MapBgfxEditorOverlay::dashedPolyline(const QVector<MapBgfxHudPoint> &points, double width,
                                          double dash, double gap, double offset, quint32 argb)
{
    if (points.size() < 2 || !(dash > 0.0) || !(gap >= 0.0))
        return;
    const double period = dash + gap;
    // Position inside the pattern carried over segment boundaries.
    double phase = std::fmod(std::max(0.0, offset), period);
    for (qsizetype index = 1; index < points.size(); ++index)
    {
        const MapBgfxHudPoint &a = points.at(index - 1);
        const MapBgfxHudPoint &b = points.at(index);
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        if (!(length > 0.0))
            continue;
        const double ux = (b.x - a.x) / length;
        const double uy = (b.y - a.y) / length;
        double position = 0.0;
        while (position < length)
        {
            const bool on = phase < dash;
            const double step = std::min(length - position, on ? dash - phase : period - phase);
            if (on)
            {
                this->canvas->line(a.x + ux * position, a.y + uy * position,
                                   a.x + ux * (position + step), a.y + uy * (position + step),
                                   width, argb);
            }
            position += step;
            phase = std::fmod(phase + step, period);
        }
    }
}

void MapBgfxEditorOverlay::dashedCircle(double x, double y, double radius, double width,
                                        double dash, double gap, quint32 argb)
{
    if (!(radius > 0.0))
        return;
    const int segments = std::clamp(int(std::ceil(radius * 3.0)), 24, 96);
    QVector<MapBgfxHudPoint> points;
    points.reserve(segments + 1);
    for (int index = 0; index <= segments; ++index)
    {
        const double angle = 2.0 * MapBgfxHudCanvas::Pi * double(index) / double(segments);
        points.append(MapBgfxHudPoint{x + radius * std::cos(angle), y + radius * std::sin(angle)});
    }
    dashedPolyline(points, width, dash, gap, 0.0, argb);
}

void MapBgfxEditorOverlay::strokeRectCentered(const MapBgfxHudRect &rect, double radius,
                                              double width, quint32 argb)
{
    if (!(width > 0.0))
        return;
    const double half = 0.5 * width;
    this->canvas->strokeRoundedRect(MapBgfxHudRect{rect.x - half, rect.y - half,
                                                   rect.width + width, rect.height + width},
                                    radius > 0.0 ? radius + half : 0.0, width, argb);
}

bool MapBgfxEditorOverlay::onScreen(double x, double y, double width, double height) const
{
    return x + width >= 0.0 && y + height >= 0.0 && x <= this->inputs->viewport.width()
        && y <= this->inputs->viewport.height();
}

// --- Icons -------------------------------------------------------------------------

void MapBgfxEditorOverlay::drawIcon(double x, double y, const Icon &icon)
{
    if (!icon.sprite.valid)
        return;
    this->canvas->sprite(MapBgfxHudRect{x - icon.padding, y - icon.padding,
                                        icon.width + 2.0 * icon.padding,
                                        icon.height + 2.0 * icon.padding},
                         icon.sprite, 0xffffffffu);
}

void MapBgfxEditorOverlay::bottomAnchoredIcon(const MapBgfxHudPoint &anchor,
                                              InfrastructureEntity entity, const QString &path,
                                              int width, Highlight highlight, bool round_anchor)
{
    if (!this->icon_provider || width <= 0)
        return;
    // Cull before asking for the sprite: off-screen icons cost nothing.
    const double reach = 2.0 * double(width) + 24.0;
    if (!onScreen(anchor.x - reach, anchor.y - reach, 2.0 * reach, 2.0 * reach))
        return;
    const Icon icon = this->icon_provider(this->cache, entity, path, width, highlight);
    // MapEntityPixmapRenderer::bottomAnchoredRect() at the rounded anchor.
    const double x = round_anchor ? double(qRound(anchor.x)) : anchor.x;
    const double y = round_anchor ? double(qRound(anchor.y)) : anchor.y;
    drawIcon(x, y - icon.height, icon);
}

void MapBgfxEditorOverlay::centeredIcon(const MapBgfxHudPoint &center, InfrastructureEntity entity,
                                        const QString &path, int width, Highlight highlight)
{
    if (!this->icon_provider || width <= 0)
        return;
    const double reach = 2.0 * double(width) + 24.0;
    if (!onScreen(center.x - reach, center.y - reach, 2.0 * reach, 2.0 * reach))
        return;
    const Icon icon = this->icon_provider(this->cache, entity, path, width, highlight);
    drawIcon(center.x - icon.width / 2.0, center.y - icon.height / 2.0, icon);
}

const NetworkRenderNode *MapBgfxEditorOverlay::nodeByUuid(const QUuid &uuid) const
{
    if (uuid.isNull())
        return nullptr;
    const QHash<QUuid, int>::const_iterator found = this->geometry.node_index.constFind(uuid);
    if (found == this->geometry.node_index.cend())
        return nullptr;
    return &this->inputs->snapshot->nodes.at(found.value());
}

bool MapBgfxEditorOverlay::deviceLinkScreen(int link_index, MapBgfxHudPoint *start,
                                            MapBgfxHudPoint *center, MapBgfxHudPoint *end) const
{
    const QVector<MapBgfx2d::WorldPoint> &points = this->geometry.link_world.at(link_index);
    if (points.size() < 2)
        return false;
    // Start, centre, end as one continuous polyline.
    QVector<MapBgfx2d::WorldPoint> three;
    three.append(points.first());
    three.append(this->geometry.device_center_world.at(link_index));
    three.append(points.last());
    const QVector<MapBgfxHudPoint> projected = screenPolyline(three);
    *start = projected.at(0);
    *center = projected.at(1);
    *end = projected.at(2);
    return true;
}

// --- Selection frames --------------------------------------------------------------

void MapBgfxEditorOverlay::tileSelection()
{
    // The former QPainter editor's paintTileSelection(): the tile-cache range at its
    // integer zoom, placed by the continuous camera.
    const MapEditorViewportRenderState &viewport = *this->inputs->viewport_state;
    if (!viewport.tile_selection_visible || viewport.tile_x_max < viewport.tile_x_min
        || viewport.tile_y_max < viewport.tile_y_min || viewport.tile_selection_zoom < 0)
        return;
    const MapBgfx2d::Camera &camera = this->inputs->camera;
    const double tiles = std::exp2(double(viewport.tile_selection_zoom));
    double west = double(viewport.tile_x_min) / tiles;
    double east = double(viewport.tile_x_max + 1) / tiles;
    const double north = double(viewport.tile_y_min) / tiles;
    const double south = double(viewport.tile_y_max + 1) / tiles;
    // Nearest world copy to the camera (whole range shifted together).
    const double shift = std::round(camera.center.x - 0.5 * (west + east));
    west += shift;
    east += shift;
    const std::function<double(double)> screen_x = [&camera](double world_x)
    {
        return camera.viewport_width * 0.5 + (world_x - camera.center.x) * camera.world_pixels;
    };
    const std::function<double(double)> screen_y = [&camera](double world_y)
    {
        return camera.viewport_height * 0.5 + (world_y - camera.center.y) * camera.world_pixels;
    };
    const MapBgfxHudRect rect{screen_x(west), screen_y(north), screen_x(east) - screen_x(west),
                              screen_y(south) - screen_y(north)};
    if (!(rect.width > 0.0) || !(rect.height > 0.0)
        || !onScreen(rect.x, rect.y, rect.width, rect.height))
        return;

    // Diagonal fill gradient (top-left -> bottom-right), corner colours from
    // the gradient's projection.
    QVector<double> positions = {0.0, 0.5, 1.0};
    QVector<quint32> stops = {rgba(92, 255, 82, 54), rgba(32, 224, 58, 66), rgba(8, 132, 38, 76)};
    const double length_squared = rect.width * rect.width + rect.height * rect.height;
    const std::function<quint32(double, double)> gradient_at = [&](double dx, double dy)
    {
        const double t = std::clamp((dx * rect.width + dy * rect.height) / length_squared, 0.0, 1.0);
        return t <= 0.5 ? MapBgfxHudCanvas::interpolateArgb(stops.at(0), stops.at(1), t / 0.5)
                        : MapBgfxHudCanvas::interpolateArgb(stops.at(1), stops.at(2), (t - 0.5) / 0.5);
    };
    QVector<MapBgfxHudPoint> corners = {
        {rect.x, rect.y}, {rect.right(), rect.y}, {rect.right(), rect.bottom()}, {rect.x, rect.bottom()}};
    QVector<quint32> colors = {gradient_at(0.0, 0.0), gradient_at(rect.width, 0.0),
                               gradient_at(rect.width, rect.height), gradient_at(0.0, rect.height)};
    this->canvas->fillConvexPolygonArgb(corners, colors);

    strokeRectCentered(rect, 0.0, 12.0, rgba(60, 255, 78, 54));
    strokeRectCentered(rect, 0.0, 5.0, rgba(92, 255, 96, 120));
    strokeRectCentered(rect, 0.0, 1.5, rgba(155, 255, 145, 230));

    // Tile grid inside the range, only where visible.
    const quint32 grid = rgba(104, 255, 104, 105);
    const double viewport_west = camera.center.x - camera.viewport_width * 0.5 / camera.world_pixels;
    const double viewport_east = camera.center.x + camera.viewport_width * 0.5 / camera.world_pixels;
    const int first_x = std::max(viewport.tile_x_min + 1,
                                 int(std::ceil((viewport_west - shift) * tiles)));
    const int last_x = std::min(viewport.tile_x_max, int(std::floor((viewport_east - shift) * tiles)));
    for (int tile_x = first_x; tile_x <= last_x; ++tile_x)
    {
        const double x = screen_x(double(tile_x) / tiles + shift);
        this->canvas->line(x, rect.y, x, rect.bottom(), 1.0, grid);
    }
    const double viewport_north = camera.center.y - camera.viewport_height * 0.5 / camera.world_pixels;
    const double viewport_south = camera.center.y + camera.viewport_height * 0.5 / camera.world_pixels;
    const int first_y = std::max(viewport.tile_y_min + 1, int(std::ceil(viewport_north * tiles)));
    const int last_y = std::min(viewport.tile_y_max, int(std::floor(viewport_south * tiles)));
    for (int tile_y = first_y; tile_y <= last_y; ++tile_y)
    {
        const double y = screen_y(double(tile_y) / tiles);
        this->canvas->line(rect.x, y, rect.right(), y, 1.0, grid);
    }
}

void MapBgfxEditorOverlay::rectangleSelection()
{
    // The former QPainter editor's paintRectangleSelection().
    const MapEditorViewportRenderState &viewport = *this->inputs->viewport_state;
    if (!viewport.rectangle_selection_visible || viewport.rectangle_selection.isEmpty())
        return;
    const QRect &selection = viewport.rectangle_selection;
    const MapBgfxHudRect outer{selection.x() + 2.5, selection.y() + 2.5,
                               selection.width() - 5.0, selection.height() - 5.0};
    if (!(outer.width > 0.0) || !(outer.height > 0.0))
        return;
    const double radius = std::min(2.0, std::min(outer.width, outer.height) / 8.0);

    // Fill: vertical gradient.
    this->canvas->fillLinearGradient(
        outer, radius, true, outer.y, outer.bottom(), QVector<double>{0.0, 0.45, 1.0},
        QVector<quint32>{rgba(35, 151, 211, 24), rgba(0, 145, 215, 32), rgba(0, 65, 110, 38)});

    // Glows and the dark shadow under the steel frame.
    strokeRectCentered(outer, radius, 18.0, rgba(0, 149, 230, 52));
    strokeRectCentered(outer, radius, 9.0, rgba(23, 190, 255, 112));
    strokeRectCentered(outer, radius, 5.0, rgba(10, 15, 18, 205));

    // Steel frame: 3 px pen with a vertical gradient from the frame's top to
    // its bottom; drawn as four bands (top/bottom/left/right).
    const QVector<double> steel_positions = {0.0, 0.24, 0.52, 0.78, 1.0};
    const QVector<quint32> steel = {rgba(245, 250, 252, 245), rgba(129, 147, 153, 240),
                                    rgba(48, 61, 66, 245), rgba(177, 190, 194, 240),
                                    rgba(31, 42, 46, 245)};
    const double half = 1.5;
    const MapBgfxHudRect bands[4] = {
        {outer.x - half, outer.y - half, outer.width + 2.0 * half, 2.0 * half},
        {outer.x - half, outer.bottom() - half, outer.width + 2.0 * half, 2.0 * half},
        {outer.x - half, outer.y + half, 2.0 * half, outer.height - 2.0 * half},
        {outer.right() - half, outer.y + half, 2.0 * half, outer.height - 2.0 * half}};
    for (const MapBgfxHudRect &band : bands)
    {
        if (band.width > 0.0 && band.height > 0.0)
            this->canvas->fillLinearGradient(band, 0.0, true, outer.y, outer.bottom(),
                                             steel_positions, steel);
    }

    const MapBgfxHudRect edge{outer.x + 1.5, outer.y + 1.5, outer.width - 3.0, outer.height - 3.0};
    if (edge.width > 0.0 && edge.height > 0.0)
        strokeRectCentered(edge, std::max(0.0, radius - 1.0), 1.0, rgba(86, 215, 255, 180));

    // Dashed blue inner frame (Qt::DashLine: 4 on, 2 off in pen widths,
    // dash offset 1.5 pen widths).
    const MapBgfxHudRect frame{outer.x + 3.0, outer.y + 3.0, outer.width - 6.0, outer.height - 6.0};
    if (frame.width > 0.0 && frame.height > 0.0)
    {
        const QVector<MapBgfxHudPoint> loop = {
            {frame.x, frame.y}, {frame.right(), frame.y}, {frame.right(), frame.bottom()},
            {frame.x, frame.bottom()}, {frame.x, frame.y}};
        dashedPolyline(loop, 5.0, 20.0, 10.0, 7.5, rgba(36, 196, 255, 96));
        dashedPolyline(loop, 1.5, 6.0, 3.0, 2.25, rgba(102, 224, 255, 245));
    }
}

// --- Network details ------------------------------------------------------------------

void MapBgfxEditorOverlay::staticDetails(bool include_moving)
{
    // The former QPainter editor's paintRenderSurfaceStaticDetails(): the editor's
    // full-colour icons and the pipe vertex dots on top of the GPU network.
    const MapEditorVisualState &state = *this->inputs->visual_state;
    const NetworkRenderSnapshot &snapshot = *this->inputs->snapshot;
    const int width = state.entity_width;
    for (qsizetype index = 0; index < snapshot.nodes.size(); ++index)
    {
        const NetworkRenderNode &node = snapshot.nodes.at(index);
        if (!include_moving && this->moving_markers.contains(node.uuid))
            continue;
        const Highlight highlight = include_moving && this->selected_markers.contains(node.uuid)
            ? Highlight::Selected : Highlight::None;
        bottomAnchoredIcon(screen(this->geometry.node_world.at(index)), node.entity_type,
                           QString(), width, highlight, true);
    }
    for (qsizetype index = 0; index < snapshot.links.size(); ++index)
    {
        const NetworkRenderLink &link = snapshot.links.at(index);
        if (!include_moving && this->moving_links.contains(link.uuid))
            continue;
        if (link.entity_type == InfrastructureEntity::Pipe)
        {
            const QVector<MapBgfx2d::WorldPoint> &points = this->geometry.link_world.at(index);
            if (points.size() < 3)
                continue;
            const quint32 color = include_moving && this->selected_pipes.contains(link.uuid)
                ? SelectedColor : BlackColor;
            const QVector<MapBgfxHudPoint> projected = screenPolyline(points);
            for (qsizetype vertex = 1; vertex + 1 < projected.size(); ++vertex)
            {
                const MapBgfxHudPoint &point = projected.at(vertex);
                if (onScreen(point.x - PipeVertexRadiusPx, point.y - PipeVertexRadiusPx,
                             2.0 * PipeVertexRadiusPx, 2.0 * PipeVertexRadiusPx))
                    this->canvas->fillCircle(point.x, point.y, PipeVertexRadiusPx, color);
            }
            continue;
        }
        if (!isDeviceLink(link.entity_type) || link.vertices_wgs84.size() < 2)
            continue;
        MapBgfxHudPoint start;
        MapBgfxHudPoint center;
        MapBgfxHudPoint end;
        if (!deviceLinkScreen(int(index), &start, &center, &end))
            continue;
        const Highlight highlight = include_moving && this->selected_markers.contains(link.uuid)
            ? Highlight::Selected : Highlight::None;
        centeredIcon(center, link.entity_type, QString(), width, highlight);
    }
    demandPointAttachments();
}

void MapBgfxEditorOverlay::movingNetwork()
{
    // The former QPainter editor's paintMovingNetwork(): the dragged entities, which
    // are hidden in the GPU network meanwhile.
    const MapEditorVisualState &state = *this->inputs->visual_state;
    for (const MapEditorDynamicLinkVisualState &link : state.move.links)
    {
        if (link.vertices_wgs84.size() < 2)
            continue;
        if (link.entity == InfrastructureEntity::Pipe)
        {
            const quint32 color = this->selected_pipes.contains(link.uuid) ? SelectedColor : BlackColor;
            const QVector<MapBgfxHudPoint> points = screenPolyline(link.vertices_wgs84);
            polyline(points, LineWidthPx, color);
            for (qsizetype vertex = 1; vertex + 1 < points.size(); ++vertex)
                this->canvas->fillCircle(points.at(vertex).x, points.at(vertex).y,
                                         PipeVertexRadiusPx, color);
            continue;
        }
        if (!isDeviceLink(link.entity) || link.vertices_wgs84.size() < 3)
            continue;
        const bool selected = this->selected_markers.contains(link.uuid);
        QList<CoordinateWGS84> three;
        three.append(link.vertices_wgs84.first());
        three.append(link.vertices_wgs84.at(1));
        three.append(link.vertices_wgs84.last());
        const QVector<MapBgfxHudPoint> points = screenPolyline(three);
        polyline(points, LineWidthPx, selected ? SelectedColor : DeviceLinkColor);
        centeredIcon(points.at(1), link.entity, QString(), state.entity_width,
                     selected ? Highlight::Selected : Highlight::None);
    }
    for (const MapEditorDynamicMarkerVisualState &marker : state.move.markers)
    {
        if (!isPointEntity(marker.entity))
            continue;
        const MapBgfxHudPoint point = screen(marker.coordinate_wgs84);
        this->canvas->fillCircle(point.x, point.y, MarkerDotRadiusPx, BlackColor);
        bottomAnchoredIcon(point, marker.entity, marker.pixmap_path, state.entity_width,
                           this->selected_markers.contains(marker.uuid) ? Highlight::Selected
                                                                        : Highlight::None,
                           true);
    }
}

void MapBgfxEditorOverlay::selectedPipes()
{
    // The former QPainter editor's paintSelectedPipes(): blue over the black GPU pipe.
    for (const QUuid &uuid : this->inputs->visual_state->selected_pipe_uuids)
    {
        const QHash<QUuid, int>::const_iterator found = this->geometry.link_index.constFind(uuid);
        if (found == this->geometry.link_index.cend())
            continue;
        const NetworkRenderLink &link = this->inputs->snapshot->links.at(found.value());
        const QVector<MapBgfx2d::WorldPoint> &points = this->geometry.link_world.at(found.value());
        if (link.entity_type != InfrastructureEntity::Pipe || points.size() < 2)
            continue;
        const QVector<MapBgfxHudPoint> projected = screenPolyline(points);
        polyline(projected, LineWidthPx, SelectedColor);
        for (qsizetype vertex = 1; vertex + 1 < projected.size(); ++vertex)
            this->canvas->fillCircle(projected.at(vertex).x, projected.at(vertex).y,
                                     PipeVertexRadiusPx, SelectedColor);
    }
}

void MapBgfxEditorOverlay::pipePlacement()
{
    // The former QPainter editor's paintPipePlacement(): start node, clicked
    // intermediate vertices, then the pointer or the snapped target node.
    const MapEditorPlacementVisualState &placement = this->inputs->visual_state->placement;
    if (!placement.creating || placement.entity != InfrastructureEntity::Pipe
        || placement.pipe_start_node_uuid.isNull())
        return;
    const NetworkRenderNode *start = nodeByUuid(placement.pipe_start_node_uuid);
    if (start == nullptr)
        return;
    QList<CoordinateWGS84> coordinates;
    coordinates.append(start->coordinate_wgs84);
    coordinates.append(placement.pipe_intermediate_vertices);
    QVector<MapBgfxHudPoint> points = screenPolyline(coordinates);
    const NetworkRenderNode *end = nodeByUuid(placement.connection_target_uuid);
    points.append(end != nullptr ? screen(end->coordinate_wgs84)
                                 : MapBgfxHudPoint{placement.mouse_position.x(),
                                                   placement.mouse_position.y()});
    polyline(points, LineWidthPx, PreviewColor);
}

void MapBgfxEditorOverlay::deviceLinkPlacement()
{
    // The former QPainter editor's paintDeviceLinkPlacement(): start -> middle -> end.
    const MapEditorPlacementVisualState &placement = this->inputs->visual_state->placement;
    if (!placement.creating || !isDeviceLink(placement.entity)
        || placement.device_link_start_node_uuid.isNull())
        return;
    const NetworkRenderNode *start_node = nodeByUuid(placement.device_link_start_node_uuid);
    if (start_node == nullptr)
        return;
    const MapBgfxHudPoint start = screen(start_node->coordinate_wgs84);
    const NetworkRenderNode *end_node = nodeByUuid(placement.connection_target_uuid);
    const MapBgfxHudPoint end = end_node != nullptr
        ? screen(end_node->coordinate_wgs84)
        : MapBgfxHudPoint{placement.mouse_position.x(), placement.mouse_position.y()};
    polyline(QVector<MapBgfxHudPoint>{start, midpoint(start, end), end}, LineWidthPx, PreviewColor);
}

void MapBgfxEditorOverlay::selectedMarkersAndDeviceLinks()
{
    // The former QPainter editor's paintSelectedMarkersAndDeviceLinks().
    const MapEditorVisualState &state = *this->inputs->visual_state;
    for (const QUuid &uuid : state.selected_marker_uuids)
    {
        const QHash<QUuid, int>::const_iterator found = this->geometry.link_index.constFind(uuid);
        if (found == this->geometry.link_index.cend())
            continue;
        const NetworkRenderLink &link = this->inputs->snapshot->links.at(found.value());
        if (!isDeviceLink(link.entity_type))
            continue;
        MapBgfxHudPoint start;
        MapBgfxHudPoint center;
        MapBgfxHudPoint end;
        if (!deviceLinkScreen(found.value(), &start, &center, &end))
            continue;
        polyline(QVector<MapBgfxHudPoint>{start, center, end}, LineWidthPx, SelectedColor);
        centeredIcon(center, link.entity_type, QString(), state.entity_width, Highlight::Selected);
    }
    for (const QUuid &uuid : state.selected_marker_uuids)
    {
        const QHash<QUuid, int>::const_iterator found = this->geometry.node_index.constFind(uuid);
        if (found == this->geometry.node_index.cend())
            continue;
        const NetworkRenderNode &node = this->inputs->snapshot->nodes.at(found.value());
        bottomAnchoredIcon(screen(this->geometry.node_world.at(found.value())), node.entity_type,
                           QString(), state.entity_width, Highlight::Selected, true);
    }
    const QUuid &target_uuid = state.placement.connection_target_uuid;
    const QHash<QUuid, int>::const_iterator target = this->geometry.node_index.constFind(target_uuid);
    if (!target_uuid.isNull() && target != this->geometry.node_index.cend())
    {
        const NetworkRenderNode &node = this->inputs->snapshot->nodes.at(target.value());
        const MapBgfxHudPoint point = screen(this->geometry.node_world.at(target.value()));
        this->canvas->fillCircle(point.x, point.y, ConnectionTargetRadiusPx, PreviewColor);
        bottomAnchoredIcon(point, node.entity_type, QString(), state.entity_width,
                           this->selected_markers.contains(target_uuid) ? Highlight::Selected
                                                                        : Highlight::None,
                           true);
    }
}

void MapBgfxEditorOverlay::simulationErrors()
{
    // The former QPainter editor's paintSimulationError(): red (current) or grey
    // (stale) lines and glows, a blue outline under selected links.
    const MapEditorVisualState &state = *this->inputs->visual_state;
    if (state.simulation_error_entities.isEmpty())
        return;
    for (QHash<QUuid, InfrastructureEntity>::const_iterator error =
             state.simulation_error_entities.cbegin();
         error != state.simulation_error_entities.cend(); ++error)
    {
        const QUuid &uuid = error.key();
        const InfrastructureEntity entity_type = error.value();
        const bool stale = state.simulation_stale_diagnostic_entity_uuids.contains(uuid);
        const quint32 color = stale ? StaleColor : ErrorColor;
        const QHash<QUuid, int>::const_iterator link_found = this->geometry.link_index.constFind(uuid);
        if (link_found != this->geometry.link_index.cend())
        {
            const NetworkRenderLink &link = this->inputs->snapshot->links.at(link_found.value());
            const QVector<MapBgfx2d::WorldPoint> &points = this->geometry.link_world.at(link_found.value());
            if (link.entity_type != entity_type || points.size() < 2)
                continue;
            const bool selected = link.entity_type == InfrastructureEntity::Pipe
                ? this->selected_pipes.contains(link.uuid)
                : this->selected_markers.contains(link.uuid);
            QVector<MapBgfxHudPoint> projected;
            MapBgfxHudPoint center;
            if (link.entity_type == InfrastructureEntity::Pipe)
            {
                projected = screenPolyline(points);
            }
            else if (isDeviceLink(link.entity_type))
            {
                MapBgfxHudPoint start;
                MapBgfxHudPoint end;
                if (!deviceLinkScreen(link_found.value(), &start, &center, &end))
                    continue;
                projected = QVector<MapBgfxHudPoint>{start, center, end};
            }
            else
            {
                continue;
            }
            if (selected)
                polyline(projected, SelectedOutlineWidthPx, SelectedColor);
            polyline(projected, LineWidthPx, color);
            if (isDeviceLink(link.entity_type))
                centeredIcon(center, link.entity_type, QString(), state.entity_width,
                             diagnosticHighlight(selected, stale));
            continue;
        }
        const QHash<QUuid, int>::const_iterator node_found = this->geometry.node_index.constFind(uuid);
        if (node_found == this->geometry.node_index.cend())
            continue;
        const NetworkRenderNode &node = this->inputs->snapshot->nodes.at(node_found.value());
        if (node.entity_type != entity_type)
            continue;
        bottomAnchoredIcon(screen(this->geometry.node_world.at(node_found.value())), node.entity_type,
                           QString(), state.entity_width,
                           diagnosticHighlight(this->selected_markers.contains(node.uuid), stale), true);
    }
}

void MapBgfxEditorOverlay::floatingPlacement()
{
    // The former QPainter editor's paintPlacement(): the marker under the pointer, or
    // a device link's icon midway to its pointer end.
    const MapEditorPlacementVisualState &placement = this->inputs->visual_state->placement;
    if (!placement.creating || !placement.floating_marker_visible
        || placement.entity == InfrastructureEntity::Unknown || placement.floating_width <= 0)
        return;
    const MapBgfxHudPoint mouse{placement.mouse_position.x(), placement.mouse_position.y()};
    if (isDeviceLink(placement.entity) && !placement.device_link_start_node_uuid.isNull())
    {
        const NetworkRenderNode *start_node = nodeByUuid(placement.device_link_start_node_uuid);
        if (start_node != nullptr)
        {
            const MapBgfxHudPoint start = screen(start_node->coordinate_wgs84);
            const NetworkRenderNode *end_node = nodeByUuid(placement.connection_target_uuid);
            const MapBgfxHudPoint end = end_node != nullptr ? screen(end_node->coordinate_wgs84) : mouse;
            centeredIcon(midpoint(start, end), placement.entity, QString(), placement.floating_width,
                         Highlight::None);
            return;
        }
    }
    bottomAnchoredIcon(mouse, placement.entity, QString(), placement.floating_width,
                       Highlight::None, false);
}

void MapBgfxEditorOverlay::demandPointAttachments()
{
    // The former QPainter editor's paintDemandPointAttachment(): dashed line from the
    // demand point to its network attachment with a round handle.
    const MapEditorVisualState &state = *this->inputs->visual_state;
    const quint32 emphasized_color = rgba(0, 120, 190, 255);
    for (const MapEditorDemandPointAttachmentVisualState &attachment : state.demand_point_attachments)
    {
        if (!attachment.visible || !finite(attachment.demand_point_coordinate_wgs84))
            continue;
        const MapBgfxHudPoint demand = screen(attachment.demand_point_coordinate_wgs84);
        if (!attachment.attachment_coordinate_valid)
        {
            if (attachment.selecting_target)
                dashedCircle(demand.x, demand.y, 11.0, 1.5, 6.0, 3.0, emphasized_color);
            continue;
        }
        QList<CoordinateWGS84> pair;
        pair.append(attachment.demand_point_coordinate_wgs84);
        pair.append(attachment.attachment_coordinate_wgs84);
        const QVector<MapBgfxHudPoint> line = screenPolyline(pair);
        const bool emphasized = attachment.selected || attachment.selecting_target || attachment.moving;
        const quint32 line_color = emphasized ? emphasized_color : rgba(70, 105, 125, 190);
        const double line_width = emphasized ? 1.5 : 1.0;
        dashedPolyline(line, line_width, 4.0 * line_width, 2.0 * line_width, 0.0, line_color);

        const MapBgfxHudPoint &network = line.last();
        const double radius = attachment.moving ? 7.0 : attachment.selected ? 6.0 : 4.5;
        const double pen = emphasized ? 2.0 : 1.5;
        this->canvas->fillCircle(network.x, network.y, radius, rgba(255, 255, 255, emphasized ? 230 : 190));
        this->canvas->strokeCircle(network.x, network.y, radius + 0.5 * pen, pen, line_color);
        if (attachment.selecting_target)
            dashedCircle(line.first().x, line.first().y, 10.0, 1.0, 1.0, 2.0, emphasized_color);
    }
}
