#include "map/rhi/map_rhi_globe_renderer.h"

#include "map/core/map_model.h"
#include "map/data/map_tile_repository.h"
#include "map/data/map_terrain_repository.h"
#include "map/data/map_terrain_tile.h"
#include "map/rhi/map_rhi_terrain_mesh_scheduler.h"
#include "config/gui_configuration.h"
#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <QDebug>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QSet>
#include <QtMath>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace
{
// Highest imagery zoom the globe will ever request. Matches MapModel::MaxZoom
// (19) exactly, since MapModel::MinViewGlobeDistanceM is itself pinned to
// zoom 19 via viewGlobeDistanceMForZoomLevel() -- the globe's maximum zoom-in
// should reach exactly as much detail as 2D/3D ever do, no more, no less.
constexpr int GlobeImageryMaxZoom = MapModel::MaxZoom;
constexpr int GlobeTerrainReliefMinimumZoom = 8;
// Keep the same terrain LOD policy as the flat RHI 3D renderer: powers of
// two from one cell up to the DEM-native density, with camera-driven rebuilds
// rate-limited so continuous orbit/zoom never resamples the retained apron at
// vsync frequency.
constexpr int GlobeTerrainMinimumLodCellCount = 1;
constexpr int GlobeAsyncTerrainMeshMinimumCellCount = 16;
constexpr qint64 GlobeMinimumTerrainLodRebuildIntervalMs = 120;
// At the globe's whole-planet zoom levels, a Mercator-centred square is not
// sufficient coverage: near either pole all longitudes converge into the
// visible disc, and clipping the request window around one arbitrary
// longitude leaves large wedge-shaped holes. Zoom 4 is the normal fully
// zoomed-out globe LOD (MapModel::GlobeMinZoomLevel); requesting the complete
// XYZ world at this LOD is only 16x16 tiles and guarantees that every part of
// the visible globe has geometry/imagery available, independent of latitude
// and dateline position.
constexpr int GlobeFullCoverageMaxZoom = 4;
// Extra ring of tiles kept beyond the strictly-visible foreground, so an
// ordinary pan/orbit does not immediately fall outside the built window and
// force a rebuild on every frame -- the same role
// MapRhiBasemapRenderer's own retention margin plays for 2D/3D.
constexpr int GlobeWindowRetentionMarginTiles = 2;
// Dead zone (in zoom levels) around the current zoom before
// computeDesiredZoom() will actually switch -- without this, distance
// values that hover near an integer boundary would flicker the window
// between two zoom levels every frame.
constexpr double GlobeZoomHysteresis = 0.6;
// Longitude segments used for each polar cap fan. Independent of the
// imagery tile grid -- a small seam between the imagery tiles' edge at
// +-85.05 degrees and the cap fan's ring is not visually significant at
// whole-globe viewing distance.
constexpr int GlobePolarCapSegments = 48;
// Flat fallback color for the polar caps (area above/below Web Mercator's
// +-85.05 degree limit, which basemap tiles never cover). A light,
// ice/cloud-like color reads reasonably for both poles without pretending
// to be real imagery.
const QColor GlobePolarCapColor(235, 240, 245);
// Missing imagery must never punch transparent/black holes through the planet
// while requests are still arriving. This low-contrast ocean-like fallback is
// only visible until the real tile texture is uploaded.
const QColor GlobeMissingTileColor(18, 58, 72);

constexpr int GlobeCameraUniformBytes = 16 * int(sizeof(float));

// Vertex grid subdivisions per tile edge, by zoom level. Low zoom tiles
// span a huge angular area (a zoom-0 tile is the entire planet, a zoom-1
// tile is a full hemisphere) and need heavy subdivision for the ellipsoid
// curvature to look smooth -- 8 subdivisions across an entire 360-degree
// tile is only 45 degrees per facet, which renders as a visibly faceted
// polyhedron rather than a sphere. By the time tiles are a few degrees
// across or smaller, the curvature within a single tile is negligible and
// a coarse grid is indistinguishable from a fine one while costing far
// less geometry across a whole tile window. In practice MapModel's own
// Min/MaxViewGlobeDistanceM keep the picked zoom at 4 or above, so zoom
// 0-3 should rarely if ever be hit -- these are still handled properly in
// case that ever changes.
int subdivisionsForZoom(int zoom)
{
    if (zoom <= 0)
        return 32;
    if (zoom <= 1)
        return 24;
    if (zoom <= 3)
        return 12;
    if (zoom <= 6)
        return 4;
    return 2;
}

// Picks the imagery zoom level from camera distance, using the exact same
// distance<->zoom relationship the footer zoom control and MapModel's own
// Min/MaxViewGlobeDistanceM are pinned to (see
// MapModel::viewGlobeZoomLevelForDistanceM()), so the imagery resolution
// shown always matches what that same zoom level would show in 2D/3D.
// Hysteresis-gated against the previously chosen zoom to avoid flicker at
// exact boundaries.
int computeDesiredZoom(
    double continuous_zoom_level, int current_zoom)
{
    if (current_zoom >= 0
        && std::abs(continuous_zoom_level - double(current_zoom)) < GlobeZoomHysteresis)
    {
        return current_zoom;
    }
    return qBound(0, int(std::lround(continuous_zoom_level)), GlobeImageryMaxZoom);
}

// The globe's visible footprint is not a fixed square in XYZ tile space.
// Near a visible limb, perspective compression can put many more Mercator
// rows/columns on screen than the centre tile suggests; near a pole the
// Mercator Y density grows strongly; and longitude wraps at the dateline.
// A fixed tile-radius window therefore eventually exposes its own edge and
// makes tiles appear/disappear in chunks while panning.
//
// Sample the actual projected ellipsoid boundary instead. The projected
// ellipsoid is convex and the screen centre always looks at the current globe
// target, so every ray from screen centre to the viewport boundary has one
// contiguous visible interval. If the viewport edge is outside the globe,
// binary-search that radial line to the true limb. Converting only the final
// surface point to geodetic coordinates keeps this cheap enough to do while
// moving, while describing the real on-screen footprint rather than a rough
// horizon-radius approximation.
constexpr int GlobeVisibleBoundarySampleCount = 32;
constexpr int GlobeVisibleLimbSearchIterations = 10;
constexpr int GlobeVisibleBoundarySafetyMarginTiles = 1;

struct GlobeVisibleTileBounds
{
    int x_min = 0;
    int x_max = -1;
    int y_min = 0;
    int y_max = -1;
    bool valid = false;
};

struct GlobeScreenRayContext
{
    GeoWgs84Ellipsoid::OrbitCameraBasis basis;
    double viewport_width = 1.0;
    double viewport_height = 1.0;
    double aspect = 1.0;
    double tan_half_fov = 1.0;
};

bool globeSurfacePointAtScreen(
    const GlobeScreenRayContext &context, double screen_x, double screen_y,
    QVector3D *surface_point)
{
    if (surface_point == nullptr)
        return false;

    const double ndc_x = 2.0 * screen_x / context.viewport_width - 1.0;
    const double ndc_y = 1.0 - 2.0 * screen_y / context.viewport_height;
    QVector3D direction = context.basis.forward
        + context.basis.right * float(ndc_x * context.tan_half_fov * context.aspect)
        + context.basis.up * float(ndc_y * context.tan_half_fov);
    if (direction.lengthSquared() <= 1e-12f)
        return false;

    direction.normalize();
    return GeoWgs84Ellipsoid::rayIntersection(
        context.basis.eye, direction, surface_point);
}

void includeGlobeSurfacePointInTileBounds(
    const QVector3D &surface_point, int zoom, int tile_span,
    double reference_virtual_x, GlobeVisibleTileBounds *bounds)
{
    if (bounds == nullptr)
        return;

    double lon_deg = 0.0;
    double lat_deg = 0.0;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(surface_point, &lon_deg, &lat_deg))
        return;

    const double wrapped_x = GeoWebMercator::lonToTileX(
        GeoWebMercator::normalizeLongitude(lon_deg), zoom);
    const double virtual_x = GeoWebMercator::nearestWrappedTileX(
        wrapped_x, reference_virtual_x, zoom);
    const int tile_x = int(std::floor(virtual_x));
    const int tile_y = qBound(
        0, int(std::floor(GeoWebMercator::latToTileY(lat_deg, zoom))), tile_span - 1);

    if (!bounds->valid)
    {
        bounds->x_min = tile_x;
        bounds->x_max = tile_x;
        bounds->y_min = tile_y;
        bounds->y_max = tile_y;
        bounds->valid = true;
        return;
    }

    bounds->x_min = qMin(bounds->x_min, tile_x);
    bounds->x_max = qMax(bounds->x_max, tile_x);
    bounds->y_min = qMin(bounds->y_min, tile_y);
    bounds->y_max = qMax(bounds->y_max, tile_y);
}

GlobeVisibleTileBounds globeVisibleTileBounds(
    const MapModel &map_model, const QSize &viewport_size, int zoom,
    double reference_virtual_x)
{
    GlobeVisibleTileBounds bounds;
    if (!viewport_size.isValid())
        return bounds;

    const int tile_span = 1 << zoom;
    const double viewport_width = double(qMax(1, viewport_size.width()));
    const double viewport_height = double(qMax(1, viewport_size.height()));
    const double center_x = viewport_width * 0.5;
    const double center_y = viewport_height * 0.5;

    GlobeScreenRayContext context;
    context.basis = GeoWgs84Ellipsoid::orbitCameraBasis(
        map_model.centerLon(), map_model.centerLat(),
        map_model.viewGlobeYawDeg(),
        qBound(MapModel::MinViewGlobePitchDeg, map_model.viewGlobePitchDeg(),
               MapModel::MaxViewGlobePitchDeg),
        qMax(MapModel::MinViewGlobeDistanceM, map_model.viewGlobeDistanceM()));
    context.viewport_width = viewport_width;
    context.viewport_height = viewport_height;
    context.aspect = viewport_width / viewport_height;
    context.tan_half_fov = std::tan(
        qDegreesToRadians(MapModel::GlobeFieldOfViewDeg * 0.5));

    QVector3D center_surface_point;
    if (!globeSurfacePointAtScreen(context, center_x, center_y, &center_surface_point))
        return bounds;

    includeGlobeSurfacePointInTileBounds(
        center_surface_point, zoom, tile_span, reference_virtual_x, &bounds);

    const double half_width = viewport_width * 0.5;
    const double half_height = viewport_height * 0.5;
    for (int sample = 0; sample < GlobeVisibleBoundarySampleCount; ++sample)
    {
        const double angle = 2.0 * M_PI * double(sample)
            / double(GlobeVisibleBoundarySampleCount);
        const double direction_x = std::cos(angle);
        const double direction_y = std::sin(angle);

        double maximum_distance = std::numeric_limits<double>::infinity();
        if (std::abs(direction_x) > 1e-12)
            maximum_distance = qMin(maximum_distance, half_width / std::abs(direction_x));
        if (std::abs(direction_y) > 1e-12)
            maximum_distance = qMin(maximum_distance, half_height / std::abs(direction_y));
        if (!std::isfinite(maximum_distance) || maximum_distance <= 0.0)
            continue;

        QVector3D boundary_surface_point;
        const double edge_x = center_x + direction_x * maximum_distance;
        const double edge_y = center_y + direction_y * maximum_distance;
        if (!globeSurfacePointAtScreen(
                context, edge_x, edge_y, &boundary_surface_point))
        {
            double hit_distance = 0.0;
            double miss_distance = maximum_distance;
            QVector3D last_hit = center_surface_point;
            for (int iteration = 0;
                 iteration < GlobeVisibleLimbSearchIterations; ++iteration)
            {
                const double test_distance = (hit_distance + miss_distance) * 0.5;
                QVector3D test_surface_point;
                const bool hit = globeSurfacePointAtScreen(
                    context,
                    center_x + direction_x * test_distance,
                    center_y + direction_y * test_distance,
                    &test_surface_point);
                if (hit)
                {
                    hit_distance = test_distance;
                    last_hit = test_surface_point;
                }
                else
                {
                    miss_distance = test_distance;
                }
            }
            boundary_surface_point = last_hit;
        }

        includeGlobeSurfacePointInTileBounds(
            boundary_surface_point, zoom, tile_span, reference_virtual_x, &bounds);
    }

    if (!bounds.valid)
        return bounds;

    bounds.x_min -= GlobeVisibleBoundarySafetyMarginTiles;
    bounds.x_max += GlobeVisibleBoundarySafetyMarginTiles;
    bounds.y_min = qMax(0, bounds.y_min - GlobeVisibleBoundarySafetyMarginTiles);
    bounds.y_max = qMin(
        tile_span - 1, bounds.y_max + GlobeVisibleBoundarySafetyMarginTiles);

    // Once the sampled visible footprint spans a complete XYZ world in X,
    // canonicalize it. rebuildWindow() already wraps X, but keeping an
    // arbitrary >world-width virtual interval would make the coverage test
    // needlessly sensitive to longitude representation changes at the
    // dateline/poles.
    if (bounds.x_max - bounds.x_min + 1 >= tile_span)
    {
        bounds.x_min = 0;
        bounds.x_max = tile_span - 1;
    }

    return bounds;
}

// Request priority measured on the globe, not in raw XYZ x/y space. This is
// important close to the poles where many different Mercator X tiles are at
// essentially the same physical distance from the crosshair. Lower values are
// dispatched first by MapTileRepository, matching the centre-out behaviour of
// the 2D/3D renderer.
int globeTileRequestPriority(
    int tile_x, int tile_y, int zoom, double center_lon_deg, double center_lat_deg)
{
    const double tile_lon_deg = GeoWebMercator::tileXToLon(double(tile_x) + 0.5, zoom);
    const double tile_lat_deg = GeoWebMercator::tileYToLat(double(tile_y) + 0.5, zoom);

    const double center_lat_rad = qDegreesToRadians(center_lat_deg);
    const double tile_lat_rad = qDegreesToRadians(tile_lat_deg);
    const double lon_delta_rad = qDegreesToRadians(
        GeoWebMercator::normalizeLongitude(tile_lon_deg - center_lon_deg));
    const double cosine_angle = qBound(
        -1.0,
        std::sin(center_lat_rad) * std::sin(tile_lat_rad)
            + std::cos(center_lat_rad) * std::cos(tile_lat_rad) * std::cos(lon_delta_rad),
        1.0);

    // 1-cos(theta) is monotonic over [0, pi], avoids acos(), and gives enough
    // integer resolution that the repository does not fall back to insertion
    // order except for genuinely equidistant tiles.
    return int(std::lround((1.0 - cosine_angle) * 1000000.0));
}


int globeTerrainZoomForImageryZoom(int imagery_zoom)
{
    const int configured_max_detail_zoom = qMax(
        GlobeTerrainReliefMinimumZoom,
        guiConfiguration().map_performance.terrain_max_detail_zoom);
    return qBound(
        GlobeTerrainReliefMinimumZoom, imagery_zoom, configured_max_detail_zoom);
}

QString globeTerrainDatasetId()
{
    return QStringLiteral("copernicus-glo30");
}

quint64 globeTilePositionKey(int tile_x, int tile_y)
{
    return (quint64(quint32(tile_x)) << 32) | quint64(quint32(tile_y));
}

bool globeTerrainDatumUsable(MapTerrainVerticalDatum datum)
{
    return datum == MapTerrainVerticalDatum::Wgs84Ellipsoid
        || datum == MapTerrainVerticalDatum::Egm96
        || datum == MapTerrainVerticalDatum::Egm2008;
}

bool globeTerrainDatumIsOrthometric(MapTerrainVerticalDatum datum)
{
    return datum == MapTerrainVerticalDatum::Egm96
        || datum == MapTerrainVerticalDatum::Egm2008;
}

QShader loadGlobeShader(const QString &resource_path)
{
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly))
        return QShader();
    return QShader::fromSerialized(file.readAll());
}
}

MapRhiGlobeRenderer::MapRhiGlobeRenderer(MapModel *map_model, MapTileRepository *tile_repository)
    : map_model(map_model),
      tile_repository(tile_repository),
      terrain_mesh_scheduler(std::make_unique<MapRhiTerrainMeshScheduler>())
{
}

MapRhiGlobeRenderer::~MapRhiGlobeRenderer() = default;

void MapRhiGlobeRenderer::setTileRepository(MapTileRepository *new_tile_repository)
{
    if (this->tile_repository == new_tile_repository)
        return;

    this->tile_repository = new_tile_repository;
    invalidateImagery();
}

void MapRhiGlobeRenderer::setTerrainRepository(MapTerrainRepository *new_terrain_repository)
{
    if (this->terrain_repository == new_terrain_repository)
        return;

    this->terrain_repository = new_terrain_repository;
    this->reported_orthometric_datum_warning = false;
    this->reported_unusable_datum_warning = false;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.invalidate();

    // Terrain availability changes the fallback mesh density as well as the
    // data source, so rebuild the window from the ellipsoid. Old background
    // results are harmless: their request ids no longer match new tiles.
    this->window_dirty = true;
}

void MapRhiGlobeRenderer::notifyTerrainTileAvailable(const QString &key)
{
    if (key.isEmpty())
        return;

    for (GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_key != key)
            continue;
        tile.terrain_mesh_request_id = 0;
        tile.terrain_mesh_applied = false;
    }
}

void MapRhiGlobeRenderer::invalidateTerrain()
{
    // Do not flatten the currently displayed relief while a replacement is
    // built (for example after vertical exaggeration changed). Mark it stale
    // and let the async worker replace it in-place when ready.
    for (GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;
        tile.terrain_mesh_request_id = 0;
        tile.terrain_mesh_applied = false;
    }
}

void MapRhiGlobeRenderer::setWireframeVisible(bool visible)
{
    if (this->wireframe_visible == visible)
        return;

    this->wireframe_visible = visible;
    if (visible)
        rebuildWireframeVertices();
}

void MapRhiGlobeRenderer::setMapVisible(bool visible)
{
    if (this->map_visible == visible)
        return;

    this->map_visible = visible;
    if (visible)
        this->window_tiles_requested = false;
}

bool MapRhiGlobeRenderer::hasPendingTerrainMeshes() const
{
    if (this->terrain_lod_rebuild_pending)
        return true;

    for (const GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_mesh_request_id != 0)
            return true;
    }
    return false;
}

MapRhiGlobeRenderer::TileVertex MapRhiGlobeRenderer::makeTileVertex(
    double lon_deg, double lat_deg, float u, float v)
{
    const QVector3D position = GeoWgs84Ellipsoid::geodeticToEcef(lon_deg, lat_deg, 0.0);
    TileVertex vertex;
    vertex.x = position.x();
    vertex.y = position.y();
    vertex.z = position.z();
    vertex.u = u;
    vertex.v = v;
    return vertex;
}

void MapRhiGlobeRenderer::buildPolarCap(bool north)
{
    const double ring_lat = north
        ? GeoWebMercator::MaximumLatitude
        : -GeoWebMercator::MaximumLatitude;
    const double pole_lat = north ? 90.0 : -90.0;

    GlobeTile cap;
    cap.is_cap = true;
    cap.first_vertex = this->cap_vertices.size();

    const TileVertex pole_vertex = makeTileVertex(0.0, pole_lat, 0.5f, 0.5f);
    for (int segment = 0; segment < GlobePolarCapSegments; ++segment)
    {
        const double lon0 = -180.0 + 360.0 * double(segment) / double(GlobePolarCapSegments);
        const double lon1 = -180.0 + 360.0 * double(segment + 1) / double(GlobePolarCapSegments);
        const TileVertex ring0 = makeTileVertex(lon0, ring_lat, 0.5f, 0.5f);
        const TileVertex ring1 = makeTileVertex(lon1, ring_lat, 0.5f, 0.5f);

        this->cap_vertices.append(pole_vertex);
        if (north)
        {
            this->cap_vertices.append(ring0);
            this->cap_vertices.append(ring1);
        }
        else
        {
            this->cap_vertices.append(ring1);
            this->cap_vertices.append(ring0);
        }
    }

    cap.vertex_count = this->cap_vertices.size() - cap.first_vertex;
    this->cap_tiles.append(cap);
}

void MapRhiGlobeRenderer::buildCaps()
{
    if (this->caps_built)
        return;

    this->cap_vertices.clear();
    this->cap_tiles.clear();
    buildPolarCap(true);
    buildPolarCap(false);
    this->caps_built = true;
    this->cap_vertex_upload_pending = true;
    rebuildWireframeVertices();
}

int MapRhiGlobeRenderer::terrainCellCountForTile(
    const GlobeTile &tile, const QSize &viewport_size) const
{
    if (this->map_model == nullptr
        || tile.terrain_zoom < GlobeTerrainReliefMinimumZoom
        || tile.zoom < tile.terrain_zoom
        || !viewport_size.isValid())
    {
        return 1;
    }

    const int zoom_delta = tile.zoom - tile.terrain_zoom;
    const int cell_divisor = 1 << qMin(zoom_delta, 6);
    const int native_cell_count = qMax(
        1, MapTerrainTileCellCount / cell_divisor);
    const int maximum_cell_count = native_cell_count;
    const int minimum_cell_count = qMin(
        maximum_cell_count, GlobeTerrainMinimumLodCellCount);
    if (maximum_cell_count <= minimum_cell_count)
        return maximum_cell_count;

    const double tile_center_lon_deg = GeoWebMercator::tileXToLon(
        double(tile.virtual_x) + 0.5, tile.zoom);
    const double tile_center_lat_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y) + 0.5, tile.zoom);
    const QVector3D tile_center = GeoWgs84Ellipsoid::geodeticToEcef(
        tile_center_lon_deg, tile_center_lat_deg, 0.0);

    const double tile_lat_top_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y), tile.zoom);
    const double tile_lat_bottom_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y) + 1.0, tile.zoom);
    const double tile_width_m =
        (2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM
         * qMax(0.0, std::cos(qDegreesToRadians(tile_center_lat_deg))))
        / double(1 << tile.zoom);
    const double tile_height_m = GeoWgs84Ellipsoid::EquatorialRadiusM
        * std::abs(qDegreesToRadians(tile_lat_top_deg - tile_lat_bottom_deg));
    const double tile_reference_size_m = qMax(tile_width_m, tile_height_m);

    const GeoWgs84Ellipsoid::OrbitCameraBasis camera_basis =
        GeoWgs84Ellipsoid::orbitCameraBasis(
            this->map_model->centerLon(), this->map_model->centerLat(),
            this->map_model->viewGlobeYawDeg(),
            qBound(
                MapModel::MinViewGlobePitchDeg,
                this->map_model->viewGlobePitchDeg(),
                MapModel::MaxViewGlobePitchDeg),
            qMax(MapModel::MinViewGlobeDistanceM,
                 this->map_model->viewGlobeDistanceM()));

    const double ground_distance_from_focus_m =
        double((tile_center - camera_basis.target).length());

    // Exactly the same "full detail down to zoom" rule as flat RHI 3D:
    // only the focus tile is forced to the DEM-native density. The rest of
    // the retained globe still follows screen-space falloff.
    if (tile.zoom >= guiConfiguration().map_performance.terrain_full_detail_zoom
        && ground_distance_from_focus_m < tile_reference_size_m * 0.75)
    {
        return maximum_cell_count;
    }

    const double native_camera_distance_m = MapModel::viewGlobeDistanceMForZoomLevel(
        double(tile.zoom), this->map_model->centerLat(),
        qMax(1, viewport_size.height()));
    const double camera_to_tile_distance_m =
        double((tile_center - camera_basis.eye).length());
    const double camera_to_focus_distance_m =
        double((camera_basis.target - camera_basis.eye).length());
    const double focus_falloff_distance_m = std::hypot(
        camera_to_focus_distance_m, ground_distance_from_focus_m);

    // Matching the RHI 3D policy, a tile beneath an oblique camera must not
    // become more detailed than the crosshair/focus merely because the eye is
    // physically closer to it. The focus remains the highest-detail location.
    const double lod_distance_m = qMax(
        camera_to_tile_distance_m, focus_falloff_distance_m);
    const double projected_tile_scale = qBound(
        0.0,
        native_camera_distance_m / qMax(1e-9, lod_distance_m),
        4.0);
    const double target_cell_size_px = qMax(
        1.0, guiConfiguration().map_performance.terrain_lod_target_cell_size_px);
    const double desired_cell_count =
        (double(MapModel::TileSize) / target_cell_size_px)
        * projected_tile_scale;

    int cell_count = minimum_cell_count;
    while (cell_count < maximum_cell_count)
    {
        const int next_cell_count = qMin(
            maximum_cell_count, cell_count * 2);
        const double threshold = std::sqrt(
            double(cell_count) * double(next_cell_count));
        if (desired_cell_count < threshold)
            break;
        cell_count = next_cell_count;
    }

    return cell_count;
}

void MapRhiGlobeRenderer::updateTerrainStitchCellCounts(
    QVector<GlobeTile> *tiles) const
{
    if (tiles == nullptr)
        return;

    QHash<quint64, qsizetype> tiles_by_position;
    tiles_by_position.reserve(tiles->size());
    for (qsizetype index = 0; index < tiles->size(); ++index)
    {
        GlobeTile &tile = (*tiles)[index];
        tile.terrain_stitch_top_cell_count = 0;
        tile.terrain_stitch_right_cell_count = 0;
        tile.terrain_stitch_bottom_cell_count = 0;
        tile.terrain_stitch_left_cell_count = 0;

        if (tile.terrain_key.isEmpty() || tile.terrain_cell_count <= 0)
            continue;
        tiles_by_position.insert(
            globeTilePositionKey(tile.tile_x, tile.tile_y), index);
    }

    for (qsizetype index = 0; index < tiles->size(); ++index)
    {
        GlobeTile &tile = (*tiles)[index];
        if (tile.terrain_key.isEmpty() || tile.terrain_cell_count <= 1)
            continue;

        const int left_x = GeoWebMercator::wrapTileX(tile.tile_x - 1, tile.zoom);
        const int right_x = GeoWebMercator::wrapTileX(tile.tile_x + 1, tile.zoom);

        const QHash<quint64, qsizetype>::const_iterator top_iterator =
            tiles_by_position.constFind(
                globeTilePositionKey(tile.tile_x, tile.tile_y - 1));
        if (top_iterator != tiles_by_position.cend())
        {
            const GlobeTile &neighbor = tiles->at(top_iterator.value());
            if (neighbor.terrain_cell_count > 0
                && neighbor.terrain_cell_count < tile.terrain_cell_count
                && tile.terrain_cell_count % neighbor.terrain_cell_count == 0)
            {
                tile.terrain_stitch_top_cell_count =
                    neighbor.terrain_cell_count;
            }
        }

        const QHash<quint64, qsizetype>::const_iterator right_iterator =
            tiles_by_position.constFind(
                globeTilePositionKey(right_x, tile.tile_y));
        if (right_iterator != tiles_by_position.cend())
        {
            const GlobeTile &neighbor = tiles->at(right_iterator.value());
            if (neighbor.terrain_cell_count > 0
                && neighbor.terrain_cell_count < tile.terrain_cell_count
                && tile.terrain_cell_count % neighbor.terrain_cell_count == 0)
            {
                tile.terrain_stitch_right_cell_count =
                    neighbor.terrain_cell_count;
            }
        }

        const QHash<quint64, qsizetype>::const_iterator bottom_iterator =
            tiles_by_position.constFind(
                globeTilePositionKey(tile.tile_x, tile.tile_y + 1));
        if (bottom_iterator != tiles_by_position.cend())
        {
            const GlobeTile &neighbor = tiles->at(bottom_iterator.value());
            if (neighbor.terrain_cell_count > 0
                && neighbor.terrain_cell_count < tile.terrain_cell_count
                && tile.terrain_cell_count % neighbor.terrain_cell_count == 0)
            {
                tile.terrain_stitch_bottom_cell_count =
                    neighbor.terrain_cell_count;
            }
        }

        const QHash<quint64, qsizetype>::const_iterator left_iterator =
            tiles_by_position.constFind(
                globeTilePositionKey(left_x, tile.tile_y));
        if (left_iterator != tiles_by_position.cend())
        {
            const GlobeTile &neighbor = tiles->at(left_iterator.value());
            if (neighbor.terrain_cell_count > 0
                && neighbor.terrain_cell_count < tile.terrain_cell_count
                && tile.terrain_cell_count % neighbor.terrain_cell_count == 0)
            {
                tile.terrain_stitch_left_cell_count =
                    neighbor.terrain_cell_count;
            }
        }

    }
}

bool MapRhiGlobeRenderer::currentTerrainLodMatches(
    const QSize &viewport_size) const
{
    for (const GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;
        if (tile.terrain_cell_count
            != terrainCellCountForTile(tile, viewport_size))
        {
            return false;
        }
    }
    return true;
}

void MapRhiGlobeRenderer::rebuildWindow(
    int zoom, int x_min, int x_max, int y_min, int y_max, int tile_span,
    const QSize &viewport_size)
{
    const int clamped_y_min = qBound(0, y_min, tile_span - 1);
    const int clamped_y_max = qBound(0, y_max, tile_span - 1);
    const bool terrain_enabled =
        this->terrain_repository != nullptr && zoom >= GlobeTerrainReliefMinimumZoom;
    const int terrain_zoom = terrain_enabled
        ? globeTerrainZoomForImageryZoom(zoom)
        : -1;

    const bool geometry_reuse_allowed = !this->window_dirty;
    QVector<TileVertex> previous_vertices = std::move(this->window_vertices);
    QVector<GlobeTile> previous_tiles = std::move(this->window_tiles);
    QHash<quint64, qsizetype> previous_tiles_by_position;
    if (geometry_reuse_allowed)
    {
        previous_tiles_by_position.reserve(previous_tiles.size());
        for (qsizetype index = 0; index < previous_tiles.size(); ++index)
        {
            const GlobeTile &tile = previous_tiles.at(index);
            previous_tiles_by_position.insert(
                globeTilePositionKey(tile.tile_x, tile.tile_y), index);
        }
    }

    QVector<GlobeTile> next_tiles;
    QSet<quint64> seen_positions;
    for (int x = x_min; x <= x_max; ++x)
    {
        const int wrapped_x = GeoWebMercator::wrapTileX(x, zoom);
        for (int y = clamped_y_min; y <= clamped_y_max; ++y)
        {
            const quint64 position_key = globeTilePositionKey(wrapped_x, y);
            if (seen_positions.contains(position_key))
                continue;
            seen_positions.insert(position_key);

            GlobeTile tile;
            tile.virtual_x = x;
            tile.tile_x = wrapped_x;
            tile.tile_y = y;
            tile.zoom = zoom;
            tile.imagery_key = this->map_model->tileCacheKeyAtZoom(wrapped_x, y, zoom);

            if (terrain_enabled)
            {
                const int zoom_delta = zoom - terrain_zoom;
                MapTerrainTileAddress terrain_address;
                terrain_address.zoom = terrain_zoom;
                terrain_address.x = quint32(wrapped_x) >> zoom_delta;
                terrain_address.y = quint32(y) >> zoom_delta;
                tile.terrain_zoom = terrain_zoom;
                tile.terrain_key =
                    mapTerrainTileKey(globeTerrainDatasetId(), terrain_address);
                tile.terrain_cell_count =
                    terrainCellCountForTile(tile, viewport_size);
            }

            next_tiles.append(tile);
        }
    }

    if (terrain_enabled)
        updateTerrainStitchCellCounts(&next_tiles);

    qsizetype estimated_vertex_count = 0;
    for (const GlobeTile &tile : next_tiles)
    {
        const int subdivisions = terrain_enabled
            ? qMax(1, tile.terrain_cell_count)
            : subdivisionsForZoom(zoom);
        estimated_vertex_count +=
            qsizetype(subdivisions) * qsizetype(subdivisions) * 6;
    }

    this->window_vertices.clear();
    this->window_vertices.reserve(estimated_vertex_count);
    this->window_tiles.clear();
    this->window_tiles.reserve(next_tiles.size());

    for (GlobeTile &tile : next_tiles)
    {
        tile.first_vertex = this->window_vertices.size();
        const int subdivisions = terrain_enabled
            ? qMax(1, tile.terrain_cell_count)
            : subdivisionsForZoom(zoom);
        const int expected_vertex_count = subdivisions * subdivisions * 6;

        bool reused = false;
        const QHash<quint64, qsizetype>::const_iterator previous_iterator =
            previous_tiles_by_position.constFind(
                globeTilePositionKey(tile.tile_x, tile.tile_y));
        if (geometry_reuse_allowed
            && previous_iterator != previous_tiles_by_position.cend())
        {
            const GlobeTile &previous_tile =
                previous_tiles.at(previous_iterator.value());
            const bool same_geometry =
                previous_tile.zoom == tile.zoom
                && previous_tile.imagery_key == tile.imagery_key
                && previous_tile.terrain_key == tile.terrain_key
                && previous_tile.terrain_cell_count == tile.terrain_cell_count
                && previous_tile.terrain_stitch_top_cell_count
                    == tile.terrain_stitch_top_cell_count
                && previous_tile.terrain_stitch_right_cell_count
                    == tile.terrain_stitch_right_cell_count
                && previous_tile.terrain_stitch_bottom_cell_count
                    == tile.terrain_stitch_bottom_cell_count
                && previous_tile.terrain_stitch_left_cell_count
                    == tile.terrain_stitch_left_cell_count
                && previous_tile.vertex_count == expected_vertex_count
                && previous_tile.first_vertex >= 0
                && previous_tile.first_vertex + previous_tile.vertex_count
                    <= previous_vertices.size();
            if (same_geometry)
            {
                const TileVertex *source = previous_vertices.constData()
                    + previous_tile.first_vertex;
                for (int index = 0; index < previous_tile.vertex_count; ++index)
                    this->window_vertices.append(source[index]);
                tile.vertex_count = previous_tile.vertex_count;
                tile.terrain_mesh_request_id =
                    previous_tile.terrain_mesh_request_id;
                tile.terrain_mesh_applied =
                    previous_tile.terrain_mesh_applied;
                reused = true;
            }
        }

        if (!reused)
        {
            bool terrain_built = false;
            if (terrain_enabled
                && this->terrain_repository != nullptr
                && !tile.terrain_key.isEmpty()
                && tile.terrain_cell_count < GlobeAsyncTerrainMeshMinimumCellCount)
            {
                const MapTerrainTile *terrain_tile =
                    this->terrain_repository->tile(tile.terrain_key);
                if (terrain_tile != nullptr
                    && terrain_tile->elevations_m.size() == MapTerrainTileSampleCount
                    && globeTerrainDatumUsable(terrain_tile->vertical_datum))
                {
                    if (globeTerrainDatumIsOrthometric(terrain_tile->vertical_datum)
                        && !this->reported_orthometric_datum_warning)
                    {
                        qWarning().noquote()
                            << QStringLiteral(
                                   "Globe terrain tiles use an orthometric EGM vertical datum; "
                                   "using it directly as local ellipsoid-normal displacement until "
                                   "the terrain service exposes WGS84-ellipsoid tile heights.");
                        this->reported_orthometric_datum_warning = true;
                    }

                    MapRhiTerrainMeshRequest request;
                    request.terrain_key = tile.terrain_key;
                    request.terrain_tile = *terrain_tile;
                    request.terrain_available = true;
                    request.virtual_x = tile.virtual_x;
                    request.tile_x = tile.tile_x;
                    request.y = tile.tile_y;
                    request.imagery_zoom = tile.zoom;
                    request.terrain_zoom = tile.terrain_zoom;
                    request.requested_cell_count = tile.terrain_cell_count;
                    request.stitch_top_cell_count =
                        tile.terrain_stitch_top_cell_count;
                    request.stitch_right_cell_count =
                        tile.terrain_stitch_right_cell_count;
                    request.stitch_bottom_cell_count =
                        tile.terrain_stitch_bottom_cell_count;
                    request.stitch_left_cell_count =
                        tile.terrain_stitch_left_cell_count;
                    request.geometry = MapRhiTerrainMeshGeometry::GlobeEcef;
                    request.globe_vertical_exaggeration =
                        this->map_model->view3dVerticalExaggeration();

                    const MapRhiTerrainMeshResult result =
                        buildTerrainMeshResult(request);
                    if (result.vertices.size() == expected_vertex_count)
                    {
                        for (const MapRhiTerrainMeshVertex &vertex : result.vertices)
                        {
                            this->window_vertices.append(TileVertex{
                                vertex.x, vertex.y, vertex.z, vertex.u, vertex.v});
                        }
                        tile.vertex_count = expected_vertex_count;
                        tile.terrain_mesh_applied = true;
                        terrain_built = true;
                    }
                }
            }

            if (!terrain_built)
            {
                for (int row = 0; row < subdivisions; ++row)
                {
                    const double v0 = double(row) / double(subdivisions);
                    const double v1 = double(row + 1) / double(subdivisions);
                    const double lat0 = GeoWebMercator::tileYToLat(
                        double(tile.tile_y) + v0, zoom);
                    const double lat1 = GeoWebMercator::tileYToLat(
                        double(tile.tile_y) + v1, zoom);

                    for (int column = 0; column < subdivisions; ++column)
                    {
                        const double u0 = double(column) / double(subdivisions);
                        const double u1 = double(column + 1) / double(subdivisions);
                        const double lon0 = GeoWebMercator::tileXToLon(
                            double(tile.virtual_x) + u0, zoom);
                        const double lon1 = GeoWebMercator::tileXToLon(
                            double(tile.virtual_x) + u1, zoom);

                        const TileVertex p00 = makeTileVertex(
                            lon0, lat0, float(u0), float(v0));
                        const TileVertex p10 = makeTileVertex(
                            lon1, lat0, float(u1), float(v0));
                        const TileVertex p01 = makeTileVertex(
                            lon0, lat1, float(u0), float(v1));
                        const TileVertex p11 = makeTileVertex(
                            lon1, lat1, float(u1), float(v1));

                        this->window_vertices.append(p00);
                        this->window_vertices.append(p01);
                        this->window_vertices.append(p10);
                        this->window_vertices.append(p10);
                        this->window_vertices.append(p01);
                        this->window_vertices.append(p11);
                    }
                }
                tile.vertex_count = expected_vertex_count;
            }
        }

        this->window_tiles.append(tile);
    }

    this->window_zoom = zoom;
    this->window_tile_x_min = x_min;
    this->window_tile_x_max = x_max;
    this->window_tile_y_min = clamped_y_min;
    this->window_tile_y_max = clamped_y_max;
    this->window_dirty = false;
    this->window_tiles_requested = false;
    this->window_vertex_upload_pending = true;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.restart();

    rebuildWireframeVertices();
    pruneUnusedTileResources();
}

void MapRhiGlobeRenderer::appendWireframeEdges(const QVector<TileVertex> &vertices)
{
    for (qsizetype vertex_index = 0; vertex_index + 2 < vertices.size(); vertex_index += 3)
    {
        const TileVertex &a = vertices.at(vertex_index);
        const TileVertex &b = vertices.at(vertex_index + 1);
        const TileVertex &c = vertices.at(vertex_index + 2);
        const WireframeVertex wa = {a.x, a.y, a.z};
        const WireframeVertex wb = {b.x, b.y, b.z};
        const WireframeVertex wc = {c.x, c.y, c.z};

        this->wireframe_vertices.append(wa);
        this->wireframe_vertices.append(wb);
        this->wireframe_vertices.append(wb);
        this->wireframe_vertices.append(wc);
        this->wireframe_vertices.append(wc);
        this->wireframe_vertices.append(wa);
    }
}

void MapRhiGlobeRenderer::rebuildWireframeVertices()
{
    this->wireframe_vertices.clear();
    this->wireframe_vertices.reserve(
        (this->window_vertices.size() + this->cap_vertices.size()) * 2);
    appendWireframeEdges(this->window_vertices);
    appendWireframeEdges(this->cap_vertices);
    this->wireframe_vertex_upload_pending = true;
}

bool MapRhiGlobeRenderer::uploadWireframeVertices(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (!this->wireframe_visible || !this->wireframe_vertex_upload_pending)
        return true;

    if (this->wireframe_vertices.isEmpty())
    {
        this->wireframe_vertex_buffer.reset();
        this->wireframe_vertex_buffer_size = 0;
        this->wireframe_vertex_upload_pending = false;
        return true;
    }

    const int required_bytes = int(
        this->wireframe_vertices.size() * qsizetype(sizeof(WireframeVertex)));
    if (!this->wireframe_vertex_buffer
        || this->wireframe_vertex_buffer_size != required_bytes)
    {
        this->wireframe_vertex_buffer.reset(this->rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_bytes));
        if (!this->wireframe_vertex_buffer || !this->wireframe_vertex_buffer->create())
            return false;
        this->wireframe_vertex_buffer_size = required_bytes;
    }

    resource_updates->updateDynamicBuffer(
        this->wireframe_vertex_buffer.get(), 0, required_bytes,
        this->wireframe_vertices.constData());
    this->wireframe_vertex_upload_pending = false;
    return true;
}

void MapRhiGlobeRenderer::pruneUnusedTileResources()
{
    QSet<QString> keys_in_use;
    keys_in_use.reserve(this->window_tiles.size());
    for (const GlobeTile &tile : this->window_tiles)
    {
        if (!tile.imagery_key.isEmpty())
            keys_in_use.insert(tile.imagery_key);
    }

    for (auto it = this->tile_resources.begin(); it != this->tile_resources.end();)
    {
        if (keys_in_use.contains(it->first))
            ++it;
        else
            it = this->tile_resources.erase(it);
    }
}

bool MapRhiGlobeRenderer::rebuildTileBindings(TileResource *resource)
{
    resource->bindings.reset(this->rhi->newShaderResourceBindings());
    if (!resource->bindings)
        return false;

    resource->bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage
                | QRhiShaderResourceBinding::FragmentStage,
            this->camera_uniform_buffer.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage,
            resource->texture.get(), this->sampler.get())
    });
    return resource->bindings->create();
}

bool MapRhiGlobeRenderer::ensureTileResource(
    GlobeTile &tile, QRhiResourceUpdateBatch *resource_updates)
{
    if (tile.is_cap)
    {
        if (!this->cap_resource.texture)
        {
            QImage image(1, 1, QImage::Format_RGBA8888);
            image.fill(GlobePolarCapColor);
            this->cap_resource.texture.reset(
                this->rhi->newTexture(QRhiTexture::RGBA8, image.size()));
            if (!this->cap_resource.texture || !this->cap_resource.texture->create())
                return false;
            resource_updates->uploadTexture(this->cap_resource.texture.get(), image);
        }
        if (!this->cap_resource.bindings && !rebuildTileBindings(&this->cap_resource))
            return false;

        tile.resource = &this->cap_resource;
        return true;
    }

    if (this->tile_repository == nullptr || tile.imagery_key.isEmpty())
        return true;

    const QPixmap *pixmap = this->tile_repository->tile(tile.imagery_key);
    if (pixmap == nullptr)
        return true;

    std::unique_ptr<TileResource> &slot = this->tile_resources[tile.imagery_key];
    if (!slot)
        slot = std::make_unique<TileResource>();

    TileResource *resource = slot.get();
    const qint64 cache_key = pixmap->cacheKey();
    if (!resource->texture || resource->pixmap_cache_key != cache_key)
    {
        QImage image = pixmap->toImage().convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull())
            return true;

        resource->bindings.reset();
        resource->texture.reset(this->rhi->newTexture(QRhiTexture::RGBA8, image.size()));
        if (!resource->texture || !resource->texture->create())
            return false;
        resource_updates->uploadTexture(resource->texture.get(), image);
        resource->pixmap_cache_key = cache_key;
    }

    if (!resource->bindings && !rebuildTileBindings(resource))
        return false;

    tile.resource = resource;
    return true;
}

void MapRhiGlobeRenderer::requestMissingTiles(QRhiResourceUpdateBatch *resource_updates)
{
    if (this->dummy_texture_upload_pending && this->dummy_texture)
    {
        QImage image(1, 1, QImage::Format_RGBA8888);
        image.fill(GlobeMissingTileColor);
        resource_updates->uploadTexture(this->dummy_texture.get(), image);
        this->dummy_texture_upload_pending = false;
    }

    if (this->tile_repository != nullptr && !this->window_tiles_requested)
    {
        const quint64 batch = this->tile_repository->beginTileRequestBatch(
            this, QStringLiteral("globe"));
        for (const GlobeTile &tile : this->window_tiles)
        {
            if (this->tile_repository->tile(tile.imagery_key) != nullptr)
                continue;

            const int priority = globeTileRequestPriority(
                tile.tile_x, tile.tile_y, tile.zoom,
                this->map_model->centerLon(), this->map_model->centerLat());
            this->tile_repository->requestTile(
                this->map_model->tileEndpointAtZoom(tile.tile_x, tile.tile_y, tile.zoom),
                tile.imagery_key, tile.tile_x, tile.tile_y, priority, batch, true);
        }
        this->window_tiles_requested = true;
    }

    for (GlobeTile &tile : this->window_tiles)
        ensureTileResource(tile, resource_updates);
    for (GlobeTile &tile : this->cap_tiles)
        ensureTileResource(tile, resource_updates);
}


void MapRhiGlobeRenderer::requestMissingTerrainTiles()
{
    if (this->terrain_repository == nullptr || this->window_tiles.isEmpty())
        return;

    QVector<const GlobeTile *> candidates;
    candidates.reserve(this->window_tiles.size());
    for (const GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_zoom < GlobeTerrainReliefMinimumZoom
            || tile.terrain_key.isEmpty())
        {
            continue;
        }
        candidates.append(&tile);
    }

    std::sort(
        candidates.begin(), candidates.end(),
        [this](const GlobeTile *first, const GlobeTile *second)
    {
        const int first_priority = globeTileRequestPriority(
            first->tile_x, first->tile_y, first->zoom,
            this->map_model->centerLon(), this->map_model->centerLat());
        const int second_priority = globeTileRequestPriority(
            second->tile_x, second->tile_y, second->zoom,
            this->map_model->centerLon(), this->map_model->centerLat());
        if (first_priority != second_priority)
            return first_priority < second_priority;
        if (first->terrain_zoom != second->terrain_zoom)
            return first->terrain_zoom > second->terrain_zoom;
        if (first->tile_y != second->tile_y)
            return first->tile_y < second->tile_y;
        return first->tile_x < second->tile_x;
    });

    QSet<QString> requested_keys;
    requested_keys.reserve(candidates.size());
    for (const GlobeTile *tile : candidates)
    {
        if (tile == nullptr
            || requested_keys.contains(tile->terrain_key)
            || this->terrain_repository->tile(tile->terrain_key) != nullptr)
        {
            continue;
        }
        requested_keys.insert(tile->terrain_key);

        const int zoom_delta = tile->zoom - tile->terrain_zoom;
        const quint32 terrain_x = quint32(tile->tile_x) >> zoom_delta;
        const quint32 terrain_y = quint32(tile->tile_y) >> zoom_delta;
        this->terrain_repository->requestTile(
            globeTerrainDatasetId(), tile->terrain_zoom, terrain_x, terrain_y);
    }
}

void MapRhiGlobeRenderer::scheduleReadyTerrainMeshes()
{
    if (this->terrain_repository == nullptr
        || this->terrain_mesh_scheduler == nullptr
        || this->map_model == nullptr)
    {
        return;
    }

    QVector<GlobeTile *> candidates;
    candidates.reserve(this->window_tiles.size());
    for (GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty()
            || tile.terrain_mesh_applied
            || tile.terrain_mesh_request_id != 0)
        {
            continue;
        }

        if (this->terrain_repository->tile(tile.terrain_key) == nullptr)
            continue;
        candidates.append(&tile);
    }

    std::sort(
        candidates.begin(), candidates.end(),
        [this](const GlobeTile *first, const GlobeTile *second)
    {
        const int first_priority = globeTileRequestPriority(
            first->tile_x, first->tile_y, first->zoom,
            this->map_model->centerLon(), this->map_model->centerLat());
        const int second_priority = globeTileRequestPriority(
            second->tile_x, second->tile_y, second->zoom,
            this->map_model->centerLon(), this->map_model->centerLat());
        return first_priority < second_priority;
    });

    for (GlobeTile *tile : candidates)
    {
        if (tile == nullptr)
            continue;

        const MapTerrainTile *terrain_tile =
            this->terrain_repository->tile(tile->terrain_key);
        if (terrain_tile == nullptr)
            continue;

        if (!globeTerrainDatumUsable(terrain_tile->vertical_datum))
        {
            if (!this->reported_unusable_datum_warning)
            {
                qWarning().noquote()
                    << QStringLiteral(
                           "Globe terrain is ignoring terrain tiles with an unknown/local "
                           "vertical datum because they cannot be interpreted as global height.");
                this->reported_unusable_datum_warning = true;
            }
            tile->terrain_mesh_applied = true;
            continue;
        }

        if (globeTerrainDatumIsOrthometric(terrain_tile->vertical_datum)
            && !this->reported_orthometric_datum_warning)
        {
            // The normalized terrain tile API currently has no per-tile
            // WGS84-ellipsoid conversion selector. Preserve the real relief
            // shape by using EGM orthometric height as the local displacement
            // for now, but make the datum approximation explicit rather than
            // silently pretending it is ellipsoidal height. A later datum
            // conversion boundary can replace this without changing the ECEF
            // mesh architecture introduced here.
            qWarning().noquote()
                << QStringLiteral(
                       "Globe terrain tiles use an orthometric EGM vertical datum; "
                       "using it directly as local ellipsoid-normal displacement until "
                       "the terrain service exposes WGS84-ellipsoid tile heights.");
            this->reported_orthometric_datum_warning = true;
        }

        MapRhiTerrainMeshRequest request;
        request.request_id = this->next_terrain_mesh_request_id++;
        request.terrain_key = tile->terrain_key;
        request.terrain_tile = *terrain_tile;
        request.terrain_available =
            terrain_tile->elevations_m.size() == MapTerrainTileSampleCount;
        request.virtual_x = tile->virtual_x;
        request.tile_x = tile->tile_x;
        request.y = tile->tile_y;
        request.imagery_zoom = tile->zoom;
        request.terrain_zoom = tile->terrain_zoom;
        request.requested_cell_count = tile->terrain_cell_count;
        request.stitch_top_cell_count =
            tile->terrain_stitch_top_cell_count;
        request.stitch_right_cell_count =
            tile->terrain_stitch_right_cell_count;
        request.stitch_bottom_cell_count =
            tile->terrain_stitch_bottom_cell_count;
        request.stitch_left_cell_count =
            tile->terrain_stitch_left_cell_count;
        request.geometry = MapRhiTerrainMeshGeometry::GlobeEcef;
        request.globe_vertical_exaggeration =
            this->map_model->view3dVerticalExaggeration();

        tile->terrain_mesh_request_id = request.request_id;
        this->terrain_mesh_scheduler->submit(request);
    }
}

bool MapRhiGlobeRenderer::applyReadyTerrainMeshes(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (this->terrain_mesh_scheduler == nullptr || resource_updates == nullptr)
        return true;

    QVector<MapRhiTerrainMeshResult> results;
    this->terrain_mesh_scheduler->collectReady(&results);
    if (results.isEmpty())
        return true;

    bool wireframe_changed = false;
    for (const MapRhiTerrainMeshResult &result : results)
    {
        for (GlobeTile &tile : this->window_tiles)
        {
            if (tile.terrain_mesh_request_id != result.request_id)
                continue;

            tile.terrain_mesh_request_id = 0;
            if (!result.terrain_available
                || result.cell_count != tile.terrain_cell_count
                || result.stitch_top_cell_count
                    != tile.terrain_stitch_top_cell_count
                || result.stitch_right_cell_count
                    != tile.terrain_stitch_right_cell_count
                || result.stitch_bottom_cell_count
                    != tile.terrain_stitch_bottom_cell_count
                || result.stitch_left_cell_count
                    != tile.terrain_stitch_left_cell_count
                || result.vertices.size() != tile.vertex_count)
            {
                break;
            }

            const qsizetype first_vertex = tile.first_vertex;
            for (qsizetype index = 0; index < result.vertices.size(); ++index)
            {
                const MapRhiTerrainMeshVertex &vertex = result.vertices.at(index);
                this->window_vertices[first_vertex + index] =
                    TileVertex{vertex.x, vertex.y, vertex.z, vertex.u, vertex.v};
            }

            tile.terrain_mesh_applied = true;
            wireframe_changed = true;

            if (this->window_vertex_buffer)
            {
                const int byte_offset =
                    int(first_vertex * qsizetype(sizeof(TileVertex)));
                const int byte_count =
                    int(result.vertices.size() * qsizetype(sizeof(TileVertex)));
                resource_updates->updateDynamicBuffer(
                    this->window_vertex_buffer.get(), byte_offset, byte_count,
                    this->window_vertices.constData() + first_vertex);
            }
            else
            {
                this->window_vertex_upload_pending = true;
            }

            break;
        }
    }

    if (wireframe_changed && this->wireframe_visible)
        rebuildWireframeVertices();
    return true;
}

bool MapRhiGlobeRenderer::ensureSharedResources()
{
    if (this->rhi == nullptr || this->render_pass_descriptor == nullptr)
        return false;

    if (!this->camera_uniform_buffer)
    {
        this->camera_uniform_buffer.reset(this->rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, GlobeCameraUniformBytes));
        if (!this->camera_uniform_buffer || !this->camera_uniform_buffer->create())
            return false;
    }

    if (!this->sampler)
    {
        this->sampler.reset(this->rhi->newSampler(
            QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
            QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!this->sampler || !this->sampler->create())
            return false;
    }

    if (!this->dummy_texture)
    {
        this->dummy_texture.reset(this->rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!this->dummy_texture || !this->dummy_texture->create())
            return false;
        this->dummy_texture_upload_pending = true;
    }

    if (!this->template_bindings)
    {
        this->template_bindings.reset(this->rhi->newShaderResourceBindings());
        if (!this->template_bindings)
            return false;
        this->template_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                this->camera_uniform_buffer.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                this->dummy_texture.get(), this->sampler.get())
        });
        if (!this->template_bindings->create())
            return false;
    }

    if (!this->wireframe_bindings)
    {
        this->wireframe_bindings.reset(this->rhi->newShaderResourceBindings());
        if (!this->wireframe_bindings)
            return false;
        this->wireframe_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage,
                this->camera_uniform_buffer.get())
        });
        if (!this->wireframe_bindings->create())
            return false;
    }

    if (!this->pipeline)
    {
        const QShader vertex_shader = loadGlobeShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe.vert.qsb"));
        const QShader fragment_shader = loadGlobeShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
            return false;

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(TileVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(TileVertex, x))},
            {0, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(TileVertex, u))}
        });

        this->pipeline.reset(this->rhi->newGraphicsPipeline());
        if (!this->pipeline)
            return false;
        this->pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->pipeline->setVertexInputLayout(input_layout);
        this->pipeline->setShaderResourceBindings(this->template_bindings.get());
        this->pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->pipeline->setSampleCount(this->sample_count);
        this->pipeline->setDepthTest(true);
        this->pipeline->setDepthWrite(true);
        this->pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if (!this->pipeline->create())
            return false;
    }

    if (!this->wireframe_pipeline)
    {
        const QShader vertex_shader = loadGlobeShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe_wireframe.vert.qsb"));
        const QShader fragment_shader = loadGlobeShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe_wireframe.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
            return false;

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(WireframeVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(WireframeVertex, x))}
        });

        this->wireframe_pipeline.reset(this->rhi->newGraphicsPipeline());
        if (!this->wireframe_pipeline)
            return false;
        this->wireframe_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->wireframe_pipeline->setVertexInputLayout(input_layout);
        this->wireframe_pipeline->setShaderResourceBindings(
            this->wireframe_bindings.get());
        this->wireframe_pipeline->setRenderPassDescriptor(
            this->render_pass_descriptor);
        this->wireframe_pipeline->setTopology(QRhiGraphicsPipeline::Lines);
        this->wireframe_pipeline->setSampleCount(this->sample_count);
        this->wireframe_pipeline->setDepthTest(true);
        this->wireframe_pipeline->setDepthWrite(false);
        this->wireframe_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if (!this->wireframe_pipeline->create())
            return false;
    }

    return true;
}

bool MapRhiGlobeRenderer::initialize(
    QRhi *rhi_instance, QRhiRenderPassDescriptor *render_pass_descriptor_instance,
    int sample_count_value)
{
    if (rhi_instance == nullptr || render_pass_descriptor_instance == nullptr)
        return false;

    this->rhi = rhi_instance;
    this->render_pass_descriptor = render_pass_descriptor_instance;
    this->sample_count = sample_count_value;

    buildCaps();
    return ensureSharedResources();
}

bool MapRhiGlobeRenderer::prepare(
    QRhiResourceUpdateBatch *resource_updates, const QMatrix4x4 &view_projection,
    const QSize &viewport_size)
{
    if (this->rhi == nullptr || resource_updates == nullptr || this->map_model == nullptr)
        return false;
    if (!ensureSharedResources())
        return false;

    const double distance_m = qMax(1.0, this->map_model->viewGlobeDistanceM());
    const double continuous_zoom_level = MapModel::viewGlobeZoomLevelForDistanceM(
        distance_m, this->map_model->centerLat(),
        viewport_size.isValid()
            ? viewport_size.height() : MapModel::GlobeZoomReferenceViewportHeightPx);
    const int desired_zoom = computeDesiredZoom(continuous_zoom_level, this->window_zoom);
    const int tile_span = 1 << desired_zoom;
    const bool full_globe_coverage = desired_zoom <= GlobeFullCoverageMaxZoom;

    // Keep X in a continuous virtual tile space, exactly like the flat RHI
    // renderer does. A wrapped longitude jumps numerically from tile 0 to
    // tile N-1 at the dateline even though the globe moved continuously;
    // anchoring the new center to the current retained window prevents that
    // representation jump from rebuilding the opposite side of the globe.
    const double wrapped_center_tile_x = GeoWebMercator::lonToTileX(
        GeoWebMercator::normalizeLongitude(this->map_model->centerLon()), desired_zoom);
    double reference_virtual_x = wrapped_center_tile_x;
    if (this->window_zoom == desired_zoom
        && this->window_tile_x_max >= this->window_tile_x_min)
    {
        reference_virtual_x =
            (double(this->window_tile_x_min) + double(this->window_tile_x_max)) * 0.5;
    }
    const double center_virtual_x = GeoWebMercator::nearestWrappedTileX(
        wrapped_center_tile_x, reference_virtual_x, desired_zoom);

    GlobeVisibleTileBounds foreground_bounds;
    if (full_globe_coverage)
    {
        foreground_bounds.x_min = 0;
        foreground_bounds.x_max = tile_span - 1;
        foreground_bounds.y_min = 0;
        foreground_bounds.y_max = tile_span - 1;
        foreground_bounds.valid = true;
    }
    else
    {
        foreground_bounds = globeVisibleTileBounds(
            *this->map_model, viewport_size, desired_zoom, center_virtual_x);
    }

    // The screen centre is guaranteed to hit the target ellipsoid, so this is
    // only a defensive fallback for an invalid viewport/camera state. Keep a
    // small local window rather than dropping the globe geometry entirely.
    if (!foreground_bounds.valid)
    {
        const int center_tile_x = int(std::floor(center_virtual_x));
        const int center_tile_y = qBound(
            0, int(std::floor(GeoWebMercator::latToTileY(
                   this->map_model->centerLat(), desired_zoom))), tile_span - 1);
        foreground_bounds.x_min = center_tile_x - 2;
        foreground_bounds.x_max = center_tile_x + 2;
        foreground_bounds.y_min = qMax(0, center_tile_y - 2);
        foreground_bounds.y_max = qMin(tile_span - 1, center_tile_y + 2);
        foreground_bounds.valid = true;
    }

    // Mirrors MapRhiBasemapRenderer's currentLayoutCoversForeground() short
    // circuit: only rebuild the window when the actual projected globe
    // footprint has moved outside the already-built retention apron. This is
    // what keeps the high-resolution edge off-screen while a pan is in
    // progress instead of exposing/replacing whole tile rows at the limb.
    const int window_x_width = this->window_tile_x_max - this->window_tile_x_min + 1;
    const bool window_covers_all_x = window_x_width >= tile_span;
    const bool foreground_needs_all_x =
        foreground_bounds.x_max - foreground_bounds.x_min + 1 >= tile_span;
    const bool window_covers_foreground_x =
        window_covers_all_x
        || (!foreground_needs_all_x
            && foreground_bounds.x_min >= this->window_tile_x_min
            && foreground_bounds.x_max <= this->window_tile_x_max);
    const bool window_covers_foreground =
        !this->window_dirty
        && this->window_zoom == desired_zoom
        && window_covers_foreground_x
        && foreground_bounds.y_min >= this->window_tile_y_min
        && foreground_bounds.y_max <= this->window_tile_y_max;

    if (!window_covers_foreground)
    {
        if (full_globe_coverage)
        {
            rebuildWindow(
                desired_zoom, 0, tile_span - 1, 0, tile_span - 1, tile_span,
                viewport_size);
        }
        else
        {
            int retained_x_min =
                foreground_bounds.x_min - GlobeWindowRetentionMarginTiles;
            int retained_x_max =
                foreground_bounds.x_max + GlobeWindowRetentionMarginTiles;
            if (retained_x_max - retained_x_min + 1 >= tile_span)
            {
                retained_x_min = 0;
                retained_x_max = tile_span - 1;
            }

            rebuildWindow(
                desired_zoom, retained_x_min, retained_x_max,
                foreground_bounds.y_min - GlobeWindowRetentionMarginTiles,
                foreground_bounds.y_max + GlobeWindowRetentionMarginTiles,
                tile_span, viewport_size);
        }
    }
    else
    {
        const bool terrain_enabled =
            this->terrain_repository != nullptr
            && desired_zoom >= GlobeTerrainReliefMinimumZoom;
        const bool terrain_lod_matches =
            !terrain_enabled || currentTerrainLodMatches(viewport_size);
        if (!terrain_lod_matches)
        {
            if (this->terrain_lod_rebuild_clock.isValid()
                && this->terrain_lod_rebuild_clock.elapsed()
                    < GlobeMinimumTerrainLodRebuildIntervalMs)
            {
                // Keep the current terrain mesh on screen until the same
                // 120 ms LOD debounce used by RHI 3D expires. renderGlobe()
                // keeps requesting frames while this flag is set, so a drag
                // that stops inside the debounce window still settles to the
                // correct LOD without another input event.
                this->terrain_lod_rebuild_pending = true;
            }
            else
            {
                rebuildWindow(
                    this->window_zoom,
                    this->window_tile_x_min, this->window_tile_x_max,
                    this->window_tile_y_min, this->window_tile_y_max,
                    1 << this->window_zoom, viewport_size);
            }
        }
        else
        {
            this->terrain_lod_rebuild_pending = false;
        }
    }

    if (!applyReadyTerrainMeshes(resource_updates))
        return false;
    requestMissingTerrainTiles();
    scheduleReadyTerrainMeshes();

    if (this->window_vertex_upload_pending && !this->window_vertices.isEmpty())
    {
        const int required_bytes =
            int(this->window_vertices.size() * qsizetype(sizeof(TileVertex)));
        if (!this->window_vertex_buffer || this->window_vertex_buffer_size != required_bytes)
        {
            this->window_vertex_buffer.reset(this->rhi->newBuffer(
                QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_bytes));
            if (!this->window_vertex_buffer || !this->window_vertex_buffer->create())
                return false;
            this->window_vertex_buffer_size = required_bytes;
        }
        resource_updates->updateDynamicBuffer(
            this->window_vertex_buffer.get(), 0, required_bytes,
            this->window_vertices.constData());
        this->window_vertex_upload_pending = false;
    }

    if (this->cap_vertex_upload_pending && !this->cap_vertices.isEmpty())
    {
        const int required_bytes =
            int(this->cap_vertices.size() * qsizetype(sizeof(TileVertex)));
        if (!this->cap_vertex_buffer)
        {
            this->cap_vertex_buffer.reset(this->rhi->newBuffer(
                QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_bytes));
            if (!this->cap_vertex_buffer || !this->cap_vertex_buffer->create())
                return false;
        }
        resource_updates->updateDynamicBuffer(
            this->cap_vertex_buffer.get(), 0, required_bytes, this->cap_vertices.constData());
        this->cap_vertex_upload_pending = false;
    }

    if (!uploadWireframeVertices(resource_updates))
        return false;

    if (this->map_visible)
        requestMissingTiles(resource_updates);

    float matrix_data[16];
    std::copy(view_projection.constData(), view_projection.constData() + 16, matrix_data);
    resource_updates->updateDynamicBuffer(
        this->camera_uniform_buffer.get(), 0, GlobeCameraUniformBytes, matrix_data);

    return true;
}

void MapRhiGlobeRenderer::draw(QRhiCommandBuffer *command_buffer)
{
    if (command_buffer == nullptr)
        return;

    if (this->map_visible && this->pipeline)
    {
        command_buffer->setGraphicsPipeline(this->pipeline.get());

        if (this->window_vertex_buffer)
        {
            for (const GlobeTile &tile : this->window_tiles)
            {
                if (tile.vertex_count <= 0)
                    continue;

                QRhiShaderResourceBindings *bindings = this->template_bindings.get();
                if (tile.resource != nullptr && tile.resource->bindings)
                    bindings = tile.resource->bindings.get();
                if (bindings == nullptr)
                    continue;

                command_buffer->setShaderResources(bindings);
                const quint32 byte_offset = quint32(
                    tile.first_vertex * int(sizeof(TileVertex)));
                const QRhiCommandBuffer::VertexInput binding(
                    this->window_vertex_buffer.get(), byte_offset);
                command_buffer->setVertexInput(0, 1, &binding);
                command_buffer->draw(quint32(tile.vertex_count));
            }
        }

        if (this->cap_vertex_buffer)
        {
            for (const GlobeTile &tile : this->cap_tiles)
            {
                if (tile.resource == nullptr || !tile.resource->bindings
                    || tile.vertex_count <= 0)
                {
                    continue;
                }

                command_buffer->setShaderResources(tile.resource->bindings.get());
                const quint32 byte_offset = quint32(
                    tile.first_vertex * int(sizeof(TileVertex)));
                const QRhiCommandBuffer::VertexInput binding(
                    this->cap_vertex_buffer.get(), byte_offset);
                command_buffer->setVertexInput(0, 1, &binding);
                command_buffer->draw(quint32(tile.vertex_count));
            }
        }
    }

    if (this->wireframe_visible
        && this->wireframe_pipeline && this->wireframe_bindings
        && this->wireframe_vertex_buffer && !this->wireframe_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->wireframe_pipeline.get());
        command_buffer->setShaderResources(this->wireframe_bindings.get());
        const QRhiCommandBuffer::VertexInput binding(
            this->wireframe_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(this->wireframe_vertices.size()));
    }
}

void MapRhiGlobeRenderer::invalidateImagery()
{
    this->tile_resources.clear();
    this->cap_resource = TileResource();
    for (GlobeTile &tile : this->window_tiles)
        tile.resource = nullptr;
    for (GlobeTile &tile : this->cap_tiles)
        tile.resource = nullptr;
    this->window_tiles_requested = false;
}

void MapRhiGlobeRenderer::releaseResources()
{
    this->pipeline.reset();
    this->wireframe_pipeline.reset();
    this->template_bindings.reset();
    this->wireframe_bindings.reset();
    this->dummy_texture.reset();
    this->dummy_texture_upload_pending = true;
    this->sampler.reset();
    this->camera_uniform_buffer.reset();
    this->window_vertex_buffer.reset();
    this->window_vertex_buffer_size = 0;
    this->window_vertex_upload_pending = true;
    this->wireframe_vertex_buffer.reset();
    this->wireframe_vertex_buffer_size = 0;
    this->wireframe_vertex_upload_pending = true;
    this->window_dirty = true;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.invalidate();
    this->window_zoom = -1;
    this->window_tile_x_min = 0;
    this->window_tile_x_max = -1;
    this->window_tile_y_min = 0;
    this->window_tile_y_max = -1;
    this->cap_vertex_buffer.reset();
    this->cap_vertex_upload_pending = true;
    invalidateImagery();
}
