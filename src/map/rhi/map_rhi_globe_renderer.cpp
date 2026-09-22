#include "map/rhi/map_rhi_globe_renderer.h"

#include "map/render/map_globe_vertical_transform.h"
#include "map/render/map_globe_picking.h"

#include "map/core/map_model.h"
#include "map/data/map_tile_repository.h"
#include "map/data/map_terrain_repository.h"
#include "map/data/map_terrain_tile.h"
#include "map/rhi/map_rhi_terrain_mesh_scheduler.h"
#include "config/gui_configuration.h"
#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <QByteArray>
#include <QDebug>
#include <QHash>
#include <QImage>
#include <QLoggingCategory>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QSet>
#include <QtMath>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <utility>

Q_LOGGING_CATEGORY(
    globeHeatmapPerformanceLog,
    "aowis.map.rhi.globe.heatmap.performance",
    QtInfoMsg)
Q_LOGGING_CATEGORY(
    globeTerrainHeightCachePerformanceLog,
    "aowis.map.rhi.globe.terrain.height_cache.performance",
    QtInfoMsg)

namespace
{
class ScopedHeatmapProfileTimer
{
public:
    ScopedHeatmapProfileTimer(qint64 *target, bool enabled)
        : target(enabled ? target : nullptr)
    {
        if (this->target != nullptr)
            this->timer.start();
    }

    ~ScopedHeatmapProfileTimer()
    {
        if (this->target != nullptr)
            *this->target += this->timer.nsecsElapsed();
    }

private:
    qint64 *target = nullptr;
    QElapsedTimer timer;
};

// Highest imagery zoom the globe will ever request. Matches MapModel::MaxZoom
// (19) exactly, since MapModel::MinViewGlobeDistanceM is itself pinned to
// zoom 19 via viewGlobeDistanceMForZoomLevel() -- the globe's maximum zoom-in
// should reach exactly as much detail as 2D does, no more, no less.
constexpr int GlobeImageryMaxZoom = MapModel::MaxZoom;
constexpr int GlobeTerrainReliefMinimumZoom =
    MapGlobeSurfaceScene::TerrainReliefMinimumZoom;
// Terrain LOD uses powers of two from one cell up to the DEM-native density,
// with camera-driven rebuilds
// rate-limited so continuous orbit/zoom never resamples the retained apron at
// vsync frequency.
constexpr int GlobeAsyncTerrainMeshMinimumCellCount = 16;
constexpr qint64 GlobeMinimumTerrainLodRebuildIntervalMs = 120;
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

bool rayAabbIntersectionDistanceRange(
    const QVector3D &ray_origin, const QVector3D &ray_direction,
    const QVector3D &bounds_min, const QVector3D &bounds_max,
    double *entry_distance_m, double *exit_distance_m)
{
    if (entry_distance_m == nullptr || exit_distance_m == nullptr)
        return false;

    double entry = 0.0;
    double exit = std::numeric_limits<double>::infinity();

    for (int axis = 0; axis < 3; ++axis)
    {
        const double origin = double(ray_origin[axis]);
        const double direction = double(ray_direction[axis]);
        const double minimum = double(bounds_min[axis]);
        const double maximum = double(bounds_max[axis]);

        if (std::abs(direction) <= 1e-12)
        {
            if (origin < minimum || origin > maximum)
                return false;
            continue;
        }

        double near_distance = (minimum - origin) / direction;
        double far_distance = (maximum - origin) / direction;
        if (near_distance > far_distance)
            std::swap(near_distance, far_distance);

        entry = qMax(entry, near_distance);
        exit = qMin(exit, far_distance);
        if (exit < entry)
            return false;
    }

    if (!(exit > 0.0) || !std::isfinite(entry))
        return false;

    *entry_distance_m = entry;
    *exit_distance_m = exit;
    return true;
}

// Matches MapRhiBasemapRenderer's HeatmapTextureSize exactly (one texel per
// Web Mercator pixel at whatever zoom a given tile is fetched at) -- see
// MapRhiGlobeRenderer::renderHeatmapTile().
constexpr int GlobeHeatmapTextureSize = MapGlobeHeatmapScene::TextureSize;
// A marker is retained once at every Web Mercator index level. Candidate
// queries choose a level whose expanded tile footprint is only a few cells
// wide, avoiding both the old zoom-18 empty-cell walks and full-network scans.
constexpr int GlobeHeatmapValidationTolerance = 8;

struct GlobeHeatmapValidationMetrics
{
    quint64 absolute_error_sum = 0;
    quint64 alpha_error_sum = 0;
    int maximum_channel_error = 0;
    int pixels_over_tolerance = 0;
    int premultiplied_violations = 0;
    int active_pixels = 0;
    int cpu_covered_pixels = 0;
    int gpu_covered_pixels = 0;
};

QString globeHeatmapTextureFormatName(QRhiTexture::Format format)
{
    switch (format)
    {
    case QRhiTexture::RGBA8:
        return QStringLiteral("RGBA8");
    case QRhiTexture::BGRA8:
        return QStringLiteral("BGRA8");
    default:
        return QStringLiteral("unsupported_%1").arg(int(format));
    }
}

GlobeHeatmapValidationMetrics compareGlobeHeatmapPixels(
    const QImage &cpu_image, const QByteArray &gpu_data,
    QRhiTexture::Format gpu_format, bool flip_y)
{
    GlobeHeatmapValidationMetrics metrics;
    const int width = cpu_image.width();
    const int height = cpu_image.height();
    const int gpu_red = gpu_format == QRhiTexture::BGRA8 ? 2 : 0;
    const int gpu_blue = gpu_format == QRhiTexture::BGRA8 ? 0 : 2;
    const uchar *gpu_bytes = reinterpret_cast<const uchar *>(
        gpu_data.constData());

    for (int y = 0; y < height; ++y)
    {
        const uchar *cpu_row = cpu_image.constScanLine(y);
        const int gpu_y = flip_y ? height - 1 - y : y;
        const uchar *gpu_row = gpu_bytes
            + qsizetype(gpu_y) * qsizetype(width) * 4;
        for (int x = 0; x < width; ++x)
        {
            const uchar *cpu_pixel = cpu_row + x * 4;
            const uchar *gpu_pixel = gpu_row + x * 4;
            const int gpu_channels[] = {
                gpu_pixel[gpu_red], gpu_pixel[1],
                gpu_pixel[gpu_blue], gpu_pixel[3]
            };
            if (cpu_pixel[3] > 1)
                ++metrics.cpu_covered_pixels;
            if (gpu_channels[3] > 1)
                ++metrics.gpu_covered_pixels;
            if (cpu_pixel[3] > 1 || gpu_channels[3] > 1)
                ++metrics.active_pixels;
            int pixel_maximum_error = 0;
            for (int channel = 0; channel < 4; ++channel)
            {
                const int error = std::abs(
                    int(cpu_pixel[channel]) - gpu_channels[channel]);
                metrics.absolute_error_sum += quint64(error);
                metrics.maximum_channel_error = qMax(
                    metrics.maximum_channel_error, error);
                pixel_maximum_error = qMax(pixel_maximum_error, error);
                if (channel == 3)
                    metrics.alpha_error_sum += quint64(error);
            }
            if (pixel_maximum_error > GlobeHeatmapValidationTolerance)
                ++metrics.pixels_over_tolerance;

            const int alpha = gpu_channels[3];
            if (gpu_channels[0] > alpha + 1
                || gpu_channels[1] > alpha + 1
                || gpu_channels[2] > alpha + 1)
            {
                ++metrics.premultiplied_violations;
            }
        }
    }
    return metrics;
}

void reportGlobeHeatmapGpuValidation(
    const QRhiReadbackResult &result, const QImage &cpu_reference,
    quint64 revision, int zoom, int tile_x, int tile_y, int stamp_count)
{
    const QImage cpu_image = cpu_reference.convertToFormat(
        QImage::Format_RGBA8888_Premultiplied);
    const QString format_name = globeHeatmapTextureFormatName(result.format);
    QString invalid_reason;
    if (cpu_image.isNull())
        invalid_reason = QStringLiteral("missing_cpu_reference");
    else if (result.format != QRhiTexture::RGBA8
             && result.format != QRhiTexture::BGRA8)
        invalid_reason = QStringLiteral("unsupported_format");
    else if (result.pixelSize != cpu_image.size())
        invalid_reason = QStringLiteral("pixel_size_mismatch");
    else
    {
        const qsizetype expected_bytes = qsizetype(cpu_image.width())
            * qsizetype(cpu_image.height()) * 4;
        if (result.data.size() < expected_bytes)
            invalid_reason = QStringLiteral("short_readback");
    }

    if (!invalid_reason.isEmpty())
    {
        qCDebug(globeHeatmapPerformanceLog).noquote().nospace()
            << "diagnostic_gpu_validation revision=" << revision
            << " tile=" << zoom << "/" << tile_x << "/" << tile_y
            << " stamps=" << stamp_count
            << " format=" << format_name
            << " size=" << result.pixelSize.width() << "x"
            << result.pixelSize.height()
            << " bytes=" << result.data.size()
            << " status=invalid reason=" << invalid_reason;
        return;
    }

    const GlobeHeatmapValidationMetrics direct =
        compareGlobeHeatmapPixels(
            cpu_image, result.data, result.format, false);
    const GlobeHeatmapValidationMetrics flipped =
        compareGlobeHeatmapPixels(
            cpu_image, result.data, result.format, true);
    const quint64 direct_channel_count =
        quint64(direct.active_pixels) * 4;
    const quint64 flipped_channel_count =
        quint64(flipped.active_pixels) * 4;
    const double direct_mean_error = direct_channel_count > 0
        ? double(direct.absolute_error_sum) / double(direct_channel_count)
        : 0.0;
    const double flipped_mean_error = flipped_channel_count > 0
        ? double(flipped.absolute_error_sum) / double(flipped_channel_count)
        : 0.0;
    const bool use_flipped =
        flipped.absolute_error_sum < direct.absolute_error_sum;
    const GlobeHeatmapValidationMetrics &selected = use_flipped
        ? flipped : direct;
    const quint64 selected_channel_count =
        quint64(selected.active_pixels) * 4;
    const double selected_mean_error = selected_channel_count > 0
        ? double(selected.absolute_error_sum) / double(selected_channel_count)
        : 0.0;
    const double selected_alpha_error = selected.active_pixels > 0
        ? double(selected.alpha_error_sum) / double(selected.active_pixels)
        : 0.0;
    const double pixels_over_tolerance_percent = selected.active_pixels > 0
        ? 100.0 * double(selected.pixels_over_tolerance)
            / double(selected.active_pixels)
        : 0.0;
    const double coverage_delta_percent =
        selected.cpu_covered_pixels > 0
        ? 100.0 * double(std::abs(
              selected.cpu_covered_pixels - selected.gpu_covered_pixels))
            / double(selected.cpu_covered_pixels)
        : (selected.gpu_covered_pixels > 0 ? 100.0 : 0.0);
    const bool compatible = selected.active_pixels > 0
        && selected_mean_error <= 3.0
        && selected_alpha_error <= 3.0
        && pixels_over_tolerance_percent <= 5.0
        && coverage_delta_percent <= 5.0
        && selected.premultiplied_violations == 0;

    qCDebug(globeHeatmapPerformanceLog).noquote().nospace()
        << "diagnostic_gpu_validation revision=" << revision
        << " tile=" << zoom << "/" << tile_x << "/" << tile_y
        << " stamps=" << stamp_count
        << " format=" << format_name
        << " selected_orientation="
        << (use_flipped ? "flip_y" : "direct")
        << " direct_mean_abs_error="
        << QString::number(direct_mean_error, 'f', 3)
        << " flip_y_mean_abs_error="
        << QString::number(flipped_mean_error, 'f', 3)
        << " mean_abs_error="
        << QString::number(selected_mean_error, 'f', 3)
        << " mean_alpha_error="
        << QString::number(selected_alpha_error, 'f', 3)
        << " max_abs_error=" << selected.maximum_channel_error
        << " active_pixels=" << selected.active_pixels
        << " cpu_covered_pixels=" << selected.cpu_covered_pixels
        << " gpu_covered_pixels=" << selected.gpu_covered_pixels
        << " coverage_delta_pct="
        << QString::number(coverage_delta_percent, 'f', 3)
        << " pixels_over_" << GlobeHeatmapValidationTolerance << "_pct="
        << QString::number(pixels_over_tolerance_percent, 'f', 3)
        << " premul_violations=" << selected.premultiplied_violations
        << " status=" << (compatible ? "compatible" : "mismatch");
}

// Each lazily-created Globe imagery or heatmap page has 256 RGBA8 layers.
// Layer 0 is a permanent sentinel, leaving 255 batchable tiles per page. At
// 256x256, each allocated page is 64 MiB when fully resident. Heatmap pages
// pack only tiles that contain actual overlay pixels, independently from the
// imagery-page layout.
constexpr int GlobeTileArrayLayerCount = 256;
constexpr int GlobeTileArrayUsableLayerCount = GlobeTileArrayLayerCount - 1;
// A single R32F page is roughly 4.13 MiB (256 * 65 * 65 * 4 bytes). Eight
// lazy pages cap this preparatory cache at roughly 33 MiB while retaining
// 2048 unique DEM tiles -- normally far more than one quadtree window needs.
// When full, only least-recently-used, currently invisible entries can be
// recycled. The CPU terrain renderer remains authoritative in this patch.
constexpr int GlobeTerrainHeightArrayLayerCount = 256;
constexpr int GlobeTerrainHeightArrayMaximumPageCount = 8;
constexpr int GlobeTerrainHeightUploadBudgetPerFrame = 32;
static_assert(sizeof(float) == 4, "R32F terrain uploads require 32-bit float");
constexpr quint64 GlobeTerrainHeightTileBytes =
    quint64(MapTerrainTileSampleCount) * quint64(sizeof(float));
// Visible fallback heatmaps are baked into a single horizontal strip. Keep
// the strip to 16 MiB at RGBA8 on backends supporting a 16384-wide texture;
// lower texture-size limits automatically reduce the number of slots.
constexpr int GlobeHeatmapGpuBakeAtlasMaximumSlots = 64;

// Vertex grid subdivisions per tile edge, by zoom level. Low zoom tiles
// span a huge angular area (a zoom-0 tile is the entire planet, a zoom-1
// tile is a full hemisphere) and need heavy subdivision for the ellipsoid
// curvature to look smooth -- 8 subdivisions across an entire 360-degree
// tile is only 45 degrees per facet, which renders as a visibly faceted
// polyhedron rather than a sphere. By the time tiles are a few degrees
// across or smaller, the curvature within a single tile is negligible and
// a coarse grid is indistinguishable from a fine one while costing far
// less geometry across a whole tile window. Unlike the old single-zoom
// window, the neutral surface-scene quadtree walk can
// keep a zoom 0-3 leaf alive whenever the camera is far enough out that a
// large fraction of the planet projects to under the subdivide threshold at
// once (the same "zoomed all the way out" case the old code's
// full-coverage special case handled), so this remains an occasional rather
// than a hot-path case, but it is reached through the ordinary walk now
// instead of a special-cased branch.
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

// Every regular Globe tile uses the same row-major grid topology. Keep that
// topology in a retained UInt32 index buffer and let imagery/terrain updates
// replace only the compact unique-vertex range. The winding exactly matches
// the former expanded triangles: p00,p01,p10 then p10,p01,p11.
void appendIndexedGridIndices(
    QVector<quint32> *indices, qsizetype first_vertex, int cell_count)
{
    if (indices == nullptr || first_vertex < 0 || cell_count <= 0)
        return;

    const qsizetype grid_width_size = qsizetype(cell_count) + 1;
    const qsizetype last_vertex = first_vertex
        + grid_width_size * grid_width_size - 1;
    if (last_vertex > qsizetype(std::numeric_limits<quint32>::max()))
        return;

    const quint32 first = quint32(first_vertex);
    const quint32 grid_width = quint32(cell_count + 1);
    for (int row = 0; row < cell_count; ++row)
    {
        for (int column = 0; column < cell_count; ++column)
        {
            const quint32 p00 = first
                + quint32(row) * grid_width + quint32(column);
            const quint32 p10 = p00 + 1;
            const quint32 p01 = p00 + grid_width;
            const quint32 p11 = p01 + 1;
            indices->append(p00);
            indices->append(p01);
            indices->append(p10);
            indices->append(p10);
            indices->append(p01);
            indices->append(p11);
        }
    }
}

// Even a pure height animation periodically refreshes visibility/resource
// state. This bounds any horizon change caused by a large climb and also
// provides a safety net for a missed external dirty notification, while
// still removing roughly five out of six full preparations at 60 Hz.
constexpr int GlobeCameraOnlyMaximumReuseMs = 100;
constexpr int GlobeTileArrayMaximumPageCount =
    (MapGlobeSurfaceScene::MaximumLeafCount + GlobeTileArrayUsableLayerCount - 1)
    / GlobeTileArrayUsableLayerCount;

// Request priority measured on the globe, not in raw XYZ x/y space. This is
// important close to the poles where many different Mercator X tiles are at
// essentially the same physical distance from the crosshair. Lower values are
// dispatched first by MapTileRepository, matching the centre-out behaviour of
// the flat 2D renderer.
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

}

MapRhiGlobeRenderer::MapRhiGlobeRenderer(MapModel *map_model, MapTileRepository *tile_repository)
    : map_model(map_model),
      tile_repository(tile_repository),
      heatmap_scene(MapModel::MaxZoom),
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

    resetTerrainHeightCache();
    this->terrain_repository = new_terrain_repository;
    this->reported_orthometric_datum_warning = false;
    this->reported_unusable_datum_warning = false;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.invalidate();

    // Terrain availability changes the fallback mesh density as well as the
    // data source, so rebuild the window from the ellipsoid. Old background
    // results are harmless: their request ids no longer match new tiles.
    this->preparation_dirty = true;
    this->window_dirty = true;
}

void MapRhiGlobeRenderer::notifyTileRepositoryChanged()
{
    // The changed key can also be an ancestor/child used by a provisional
    // texture, so conservatively let the normal resource scan decide which
    // visible tiles actually need work.
    this->preparation_dirty = true;
}

void MapRhiGlobeRenderer::notifyTerrainRepositoryChanged()
{
    this->preparation_dirty = true;
}

void MapRhiGlobeRenderer::requestTerrainForCurrentView(const QSize &viewport_size)
{
    if (this->map_model == nullptr || this->terrain_repository == nullptr
        || !viewport_size.isValid())
    {
        return;
    }

    // Do the same visible-quadtree selection prepare() would do, but do it
    // synchronously at the moment the network fit lands. That gives every
    // visible leaf its terrain key now and dispatches the DEM requests now;
    // no camera nudge or LOD transition is needed to wake terrain loading.
    const QVector<MapGlobeQuadtreeLeaf> desired_leaves =
        this->surface_scene.selectVisibleLeaves(
            *this->map_model, viewport_size, this->terrain_repository);
    rebuildWindow(desired_leaves, viewport_size);
    requestMissingTerrainTiles();
    this->preparation_dirty = true;
}

void MapRhiGlobeRenderer::invalidateTerrainView()
{
    // Force the next Globe frame to rebuild its visible quadtree window from
    // the current camera before requesting DEM tiles. This is intentionally
    // stronger than invalidateTerrain(), which only rebuilds already-known
    // meshes and therefore cannot repair a stale pre-fit/pre-network window.
    this->preparation_dirty = true;
    this->window_dirty = true;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.invalidate();
    this->surface_scene.clearVisibilityHistory();
}

bool MapRhiGlobeRenderer::setRenderOriginEcef(
    const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef)
{
    if (this->render_origin_ecef.x == origin_ecef.x
        && this->render_origin_ecef.y == origin_ecef.y
        && this->render_origin_ecef.z == origin_ecef.z)
    {
        return false;
    }

    this->render_origin_ecef = origin_ecef;
    this->preparation_dirty = true;

    // Globe terrain, polar caps, wireframe, and network must all use this
    // same coordinate frame. The camera origin is sticky, so this rebuild
    // happens only after a long translation rather than during ordinary
    // pan/orbit frames. Clearing request ids makes results produced for the
    // old origin harmless when the asynchronous worker later returns them.
    for (GlobeTile &tile : this->window_tiles)
        tile.terrain_mesh_request_id = 0;
    this->window_dirty = true;
    this->caps_built = false;
    this->cap_vertices.clear();
    this->cap_indices.clear();
    this->cap_tiles.clear();
    this->surface_backend.invalidateCaps();
    return true;
}

void MapRhiGlobeRenderer::notifyTerrainTileAvailable(const QString &key)
{
    if (key.isEmpty())
        return;

    this->preparation_dirty = true;

    // A retry or provider refresh may replace the CPU tile behind an
    // existing key. Preserve its stable layer assignment but require the
    // new bytes to reach that layer before advertising it as ready again.
    QHash<QString, TerrainHeightCacheEntry>::iterator cache_iterator =
        this->terrain_height_cache.find(key);
    if (cache_iterator != this->terrain_height_cache.end())
        cache_iterator.value().uploaded = false;

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
    this->preparation_dirty = true;
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
    this->prepared_surface_render_frame.wireframe_visible = visible;
    this->preparation_dirty = true;
    if (visible)
        rebuildWireframeVertices();
}

void MapRhiGlobeRenderer::setMapVisible(bool visible)
{
    if (this->map_visible == visible)
        return;

    this->map_visible = visible;
    this->prepared_surface_render_frame.map_visible = visible;
    this->preparation_dirty = true;
    if (visible)
        this->window_tiles_requested = false;
}

void MapRhiGlobeRenderer::setHeatmapOverlay(
    const QVector<HeatmapMarker> &markers, double radius_m, double solid_fraction)
{
    if (!this->heatmap_scene.setOverlay(markers, radius_m, solid_fraction))
        return;

    this->preparation_dirty = true;
    this->heatmap_gpu_bake_jobs.clear();
    // Before the once-per-QRhi diagnostic has been scheduled there is
    // nothing useful to retain. Once scheduled, its fixed stamps and CPU
    // reference remain self-contained and must not be cancelled merely
    // because the simulation advances to the next color revision.
    if (!this->diagnostic_heatmap_gpu_validation_attempted)
    {
        this->diagnostic_heatmap_bake_instances.clear();
        this->diagnostic_heatmap_bake_cpu_reference = QImage();
        this->diagnostic_heatmap_bake_pending = false;
        this->diagnostic_heatmap_bake_revision = 0;
    }
    this->heatmap_array_draw_indices_dirty = true;
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

void MapRhiGlobeRenderer::terrainMeshProgress(
    int *completed, int *total, bool *active) const
{
    int completed_count = 0;
    int total_count = 0;
    bool build_active = this->terrain_lod_rebuild_pending;

    for (const GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;

        if (tile.terrain_mesh_applied)
        {
            ++completed_count;
            ++total_count;
        }
        else if (tile.terrain_mesh_request_id != 0)
        {
            ++total_count;
            build_active = true;
        }
    }

    if (completed != nullptr)
        *completed = completed_count;
    if (total != nullptr)
        *total = total_count;
    if (active != nullptr)
        *active = build_active;
}

void MapRhiGlobeRenderer::updateTerrainRayBounds(GlobeTile *tile)
{
    if (tile == nullptr)
        return;

    tile->terrain_ray_bounds_valid = false;
    tile->terrain_ray_row_bounds.clear();
    if (!tile->terrain_mesh_has_relief
        || tile->first_vertex < 0 || tile->vertex_count <= 0)
    {
        return;
    }

    const qsizetype vertex_begin = qsizetype(tile->first_vertex);
    const qsizetype vertex_end = vertex_begin + qsizetype(tile->vertex_count);
    if (vertex_begin < 0 || vertex_end > this->window_vertices.size())
        return;

    const TileVertex &first_vertex = this->window_vertices.at(vertex_begin);
    QVector3D bounds_min(first_vertex.x, first_vertex.y, first_vertex.z);
    QVector3D bounds_max = bounds_min;

    for (qsizetype index = vertex_begin + 1; index < vertex_end; ++index)
    {
        const TileVertex &vertex = this->window_vertices.at(index);
        bounds_min.setX(qMin(bounds_min.x(), vertex.x));
        bounds_min.setY(qMin(bounds_min.y(), vertex.y));
        bounds_min.setZ(qMin(bounds_min.z(), vertex.z));
        bounds_max.setX(qMax(bounds_max.x(), vertex.x));
        bounds_max.setY(qMax(bounds_max.y(), vertex.y));
        bounds_max.setZ(qMax(bounds_max.z(), vertex.z));
    }

    tile->terrain_ray_bounds_min = bounds_min;
    tile->terrain_ray_bounds_max = bounds_max;
    tile->terrain_ray_bounds_valid = true;

    const int cell_count = qMax(1, tile->terrain_cell_count);
    const qsizetype grid_width = qsizetype(cell_count) + 1;
    const qsizetype expected_vertex_count = grid_width * grid_width;
    if (qsizetype(tile->vertex_count) != expected_vertex_count)
        return;

    tile->terrain_ray_row_bounds.reserve(cell_count);
    for (int row = 0; row < cell_count; ++row)
    {
        const qsizetype row_begin = vertex_begin
            + qsizetype(row) * grid_width;
        const qsizetype row_end = row_begin + grid_width * 2;
        if (row_begin < vertex_begin || row_end > vertex_end)
        {
            tile->terrain_ray_row_bounds.clear();
            return;
        }

        const TileVertex &row_first = this->window_vertices.at(row_begin);
        QVector3D row_min(row_first.x, row_first.y, row_first.z);
        QVector3D row_max = row_min;
        for (qsizetype index = row_begin + 1; index < row_end; ++index)
        {
            const TileVertex &vertex = this->window_vertices.at(index);
            row_min.setX(qMin(row_min.x(), vertex.x));
            row_min.setY(qMin(row_min.y(), vertex.y));
            row_min.setZ(qMin(row_min.z(), vertex.z));
            row_max.setX(qMax(row_max.x(), vertex.x));
            row_max.setY(qMax(row_max.y(), vertex.y));
            row_max.setZ(qMax(row_max.z(), vertex.z));
        }

        GlobeTile::TerrainRayRowBounds row_bounds;
        row_bounds.minimum = row_min;
        row_bounds.maximum = row_max;
        tile->terrain_ray_row_bounds.append(row_bounds);
    }
}

bool MapRhiGlobeRenderer::visibleTerrainRayIntersection(
    const GeoWgs84Ellipsoid::EcefPositionD &ray_origin_ecef,
    const QVector3D &ray_direction_ecef,
    GeoWgs84Ellipsoid::EcefPositionD *intersection_ecef,
    double *distance_m) const
{
    if (intersection_ecef == nullptr || distance_m == nullptr)
        return false;

    QVector3D direction = ray_direction_ecef;
    if (direction.lengthSquared() <= 1e-12f)
        return false;
    direction.normalize();

    // window_vertices are the exact origin-relative float32 positions the
    // GPU draws. Keep the ray in that same coordinate frame so this routine
    // is a reference intersection against the visible terrain itself rather
    // than a second, subtly different reconstruction of the DEM surface.
    const QVector3D relative_origin(
        float(ray_origin_ecef.x - this->render_origin_ecef.x),
        float(ray_origin_ecef.y - this->render_origin_ecef.y),
        float(ray_origin_ecef.z - this->render_origin_ecef.z));

    bool found = false;
    double nearest_distance_m = std::numeric_limits<double>::infinity();

    for (const GlobeTile &tile : this->window_tiles)
    {
        // Only actual DEM relief participates. Initial placeholders and
        // ordinary zero-height imagery geometry intentionally fall through
        // to the caller's ellipsoid fallback. Stale-but-still-visible relief
        // remains eligible while a replacement mesh is being built.
        if (!tile.terrain_mesh_has_relief
            || !tile.terrain_ray_bounds_valid
            || tile.first_index < 0 || tile.index_count < 3)
        {
            continue;
        }

        double bounds_entry_distance_m = 0.0;
        double bounds_exit_distance_m = 0.0;
        if (!rayAabbIntersectionDistanceRange(
                relative_origin, direction,
                tile.terrain_ray_bounds_min, tile.terrain_ray_bounds_max,
                &bounds_entry_distance_m, &bounds_exit_distance_m)
            || bounds_entry_distance_m >= nearest_distance_m)
        {
            continue;
        }

        const qsizetype index_begin = qsizetype(tile.first_index);
        const qsizetype index_end = index_begin + qsizetype(tile.index_count);
        if (index_begin < 0 || index_end > this->window_indices.size())
            continue;

        const std::function<void(qsizetype, qsizetype)> test_index_range =
            [this, &relative_origin, &direction, &nearest_distance_m, &found]
            (qsizetype range_begin, qsizetype range_end)
        {
            for (qsizetype index = range_begin;
                 index + 2 < range_end; index += 3)
            {
                const quint32 a_index = this->window_indices.at(index);
                const quint32 b_index = this->window_indices.at(index + 1);
                const quint32 c_index = this->window_indices.at(index + 2);
                if (qsizetype(a_index) >= this->window_vertices.size()
                    || qsizetype(b_index) >= this->window_vertices.size()
                    || qsizetype(c_index) >= this->window_vertices.size())
                {
                    continue;
                }

                const TileVertex &a_vertex = this->window_vertices.at(a_index);
                const TileVertex &b_vertex = this->window_vertices.at(b_index);
                const TileVertex &c_vertex = this->window_vertices.at(c_index);
                const QVector3D a(a_vertex.x, a_vertex.y, a_vertex.z);
                const QVector3D b(b_vertex.x, b_vertex.y, b_vertex.z);
                const QVector3D c(c_vertex.x, c_vertex.y, c_vertex.z);

                double candidate_distance_m = 0.0;
                if (!mapGlobeRayTriangleIntersectionDistance(
                        relative_origin, direction, a, b, c,
                        &candidate_distance_m)
                    || !(candidate_distance_m > 0.0)
                    || !mapGlobeUpdateNearestHitDistance(
                        candidate_distance_m, &nearest_distance_m))
                {
                    continue;
                }

                found = true;
            }
        };

        const int cell_count = qMax(1, tile.terrain_cell_count);
        const qsizetype row_index_count = qsizetype(cell_count) * 6;
        const bool have_row_bounds =
            tile.terrain_ray_row_bounds.size() == cell_count
            && qsizetype(tile.index_count)
                == qsizetype(cell_count) * qsizetype(cell_count) * 6;
        if (!have_row_bounds)
        {
            test_index_range(index_begin, index_end);
            continue;
        }

        for (int row = 0; row < cell_count; ++row)
        {
            const GlobeTile::TerrainRayRowBounds &row_bounds =
                tile.terrain_ray_row_bounds.at(row);
            double row_entry_distance_m = 0.0;
            double row_exit_distance_m = 0.0;
            if (!rayAabbIntersectionDistanceRange(
                    relative_origin, direction,
                    row_bounds.minimum, row_bounds.maximum,
                    &row_entry_distance_m, &row_exit_distance_m)
                || row_entry_distance_m >= nearest_distance_m)
            {
                continue;
            }

            const qsizetype row_begin = index_begin
                + qsizetype(row) * row_index_count;
            const qsizetype row_end = qMin(
                row_begin + row_index_count, index_end);
            test_index_range(row_begin, row_end);
        }
    }

    if (!found)
        return false;

    intersection_ecef->x = ray_origin_ecef.x
        + double(direction.x()) * nearest_distance_m;
    intersection_ecef->y = ray_origin_ecef.y
        + double(direction.y()) * nearest_distance_m;
    intersection_ecef->z = ray_origin_ecef.z
        + double(direction.z()) * nearest_distance_m;
    *distance_m = nearest_distance_m;
    return true;
}

bool MapRhiGlobeRenderer::visibleTerrainSamplingAtCoordinate(
    const CoordinateWGS84 &coordinate,
    int *terrain_zoom,
    double *cell_size_m) const
{
    if (terrain_zoom == nullptr || cell_size_m == nullptr
        || !std::isfinite(coordinate.longitude_deg)
        || !std::isfinite(coordinate.latitude_deg))
    {
        return false;
    }

    *terrain_zoom = -1;
    *cell_size_m = 0.0;

    const GlobeTile *resolved_tile = nullptr;
    for (int zoom = GlobeImageryMaxZoom; zoom >= 0; --zoom)
    {
        const double coordinate_tile_x = GeoWebMercator::lonToTileX(
            coordinate.longitude_deg, zoom);
        const double coordinate_tile_y = GeoWebMercator::latToTileY(
            coordinate.latitude_deg, zoom);
        const int tile_count = 1 << zoom;
        const int coordinate_x = GeoWebMercator::wrapTileX(
            int(std::floor(coordinate_tile_x)), zoom);
        const int coordinate_y = qBound(
            0, int(std::floor(coordinate_tile_y)), tile_count - 1);
        const quint64 position_key = MapGlobeSurfaceScene::positionKey(
            zoom, coordinate_x, coordinate_y);
        const QHash<quint64, qsizetype>::const_iterator tile_iterator =
            this->window_tile_indices_by_position.constFind(position_key);
        if (tile_iterator == this->window_tile_indices_by_position.constEnd())
            continue;

        const qsizetype tile_index = tile_iterator.value();
        if (tile_index < 0 || tile_index >= this->window_tiles.size())
            return false;
        resolved_tile = &this->window_tiles.at(tile_index);
        break;
    }

    if (resolved_tile == nullptr || resolved_tile->is_cap
        || resolved_tile->terrain_key.isEmpty()
        || resolved_tile->terrain_zoom < GlobeTerrainReliefMinimumZoom
        || resolved_tile->terrain_cell_count <= 0
        || !resolved_tile->terrain_mesh_has_relief)
    {
        return false;
    }

    const int tile_count = 1 << resolved_tile->zoom;
    const double latitude_top_deg = GeoWebMercator::tileYToLat(
        double(resolved_tile->tile_y), resolved_tile->zoom);
    const double latitude_bottom_deg = GeoWebMercator::tileYToLat(
        double(resolved_tile->tile_y) + 1.0, resolved_tile->zoom);
    const double tile_width_m =
        (2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM
         * qMax(0.0, std::cos(qDegreesToRadians(coordinate.latitude_deg))))
        / double(tile_count);
    const double tile_height_m = GeoWgs84Ellipsoid::EquatorialRadiusM
        * std::abs(qDegreesToRadians(latitude_top_deg - latitude_bottom_deg));
    const double reference_size_m = qMax(tile_width_m, tile_height_m);
    const double resolved_cell_size_m =
        reference_size_m / double(resolved_tile->terrain_cell_count);
    if (!std::isfinite(resolved_cell_size_m) || resolved_cell_size_m <= 0.0)
        return false;

    *terrain_zoom = resolved_tile->terrain_zoom;
    *cell_size_m = resolved_cell_size_m;
    return true;
}


MapRhiGlobeRenderer::TileVertex MapRhiGlobeRenderer::makeTileVertex(
    double lon_deg, double lat_deg, float u, float v) const
{
    const GeoWgs84Ellipsoid::EcefPositionD position =
        GeoWgs84Ellipsoid::geodeticToEcefD(lon_deg, lat_deg, 0.0);
    TileVertex vertex;
    vertex.x = float(position.x - this->render_origin_ecef.x);
    vertex.y = float(position.y - this->render_origin_ecef.y);
    vertex.z = float(position.z - this->render_origin_ecef.z);
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
    cap.first_index = this->cap_indices.size();

    const TileVertex pole_vertex = makeTileVertex(0.0, pole_lat, 0.5f, 0.5f);
    this->cap_vertices.append(pole_vertex);
    for (int segment = 0; segment <= GlobePolarCapSegments; ++segment)
    {
        const double longitude_deg = -180.0
            + 360.0 * double(segment) / double(GlobePolarCapSegments);
        this->cap_vertices.append(
            makeTileVertex(longitude_deg, ring_lat, 0.5f, 0.5f));
    }

    const quint32 pole_index = quint32(cap.first_vertex);
    for (int segment = 0; segment < GlobePolarCapSegments; ++segment)
    {
        const quint32 ring0 = pole_index + 1 + quint32(segment);
        const quint32 ring1 = ring0 + 1;
        this->cap_indices.append(pole_index);
        if (north)
        {
            this->cap_indices.append(ring0);
            this->cap_indices.append(ring1);
        }
        else
        {
            this->cap_indices.append(ring1);
            this->cap_indices.append(ring0);
        }
    }

    cap.vertex_count = this->cap_vertices.size() - cap.first_vertex;
    cap.index_count = this->cap_indices.size() - cap.first_index;
    this->cap_tiles.append(cap);
}

void MapRhiGlobeRenderer::buildCaps()
{
    if (this->caps_built)
        return;

    this->cap_vertices.clear();
    this->cap_indices.clear();
    this->cap_tiles.clear();
    buildPolarCap(true);
    buildPolarCap(false);
    this->caps_built = true;
    this->surface_backend.invalidateCaps();
    if (this->wireframe_visible)
        rebuildWireframeVertices();
}

int MapRhiGlobeRenderer::terrainCellCountForTile(
    const GlobeTile &tile, const QSize &viewport_size,
    const GeoWgs84Ellipsoid::OrbitCameraBasis *camera_basis_override) const
{
    if (this->map_model == nullptr)
        return 1;

    MapGlobeTerrainLodTile surface_tile;
    surface_tile.virtual_x = tile.virtual_x;
    surface_tile.tile_x = tile.tile_x;
    surface_tile.tile_y = tile.tile_y;
    surface_tile.zoom = tile.zoom;
    surface_tile.terrain_zoom = tile.terrain_zoom;
    surface_tile.terrain_available = !tile.terrain_key.isEmpty();
    surface_tile.terrain_cell_count = tile.terrain_cell_count;
    return this->surface_scene.terrainCellCountForTile(
        *this->map_model, surface_tile, viewport_size, camera_basis_override);
}

void MapRhiGlobeRenderer::updateTerrainStitchCellCounts(
    QVector<GlobeTile> *tiles) const
{
    if (tiles == nullptr)
        return;

    QVector<MapGlobeTerrainLodTile> surface_tiles;
    surface_tiles.reserve(tiles->size());
    for (const GlobeTile &tile : *tiles)
    {
        MapGlobeTerrainLodTile surface_tile;
        surface_tile.virtual_x = tile.virtual_x;
        surface_tile.tile_x = tile.tile_x;
        surface_tile.tile_y = tile.tile_y;
        surface_tile.zoom = tile.zoom;
        surface_tile.terrain_zoom = tile.terrain_zoom;
        surface_tile.terrain_available = !tile.terrain_key.isEmpty();
        surface_tile.terrain_cell_count = tile.terrain_cell_count;
        surface_tiles.append(surface_tile);
    }

    MapGlobeSurfaceScene::updateTerrainStitchCellCounts(&surface_tiles);
    for (qsizetype index = 0; index < tiles->size(); ++index)
    {
        GlobeTile &tile = (*tiles)[index];
        const MapGlobeTerrainLodTile &surface_tile = surface_tiles.at(index);
        tile.terrain_stitch_top_cell_count =
            surface_tile.terrain_stitch_top_cell_count;
        tile.terrain_stitch_right_cell_count =
            surface_tile.terrain_stitch_right_cell_count;
        tile.terrain_stitch_bottom_cell_count =
            surface_tile.terrain_stitch_bottom_cell_count;
        tile.terrain_stitch_left_cell_count =
            surface_tile.terrain_stitch_left_cell_count;
    }
}

bool MapRhiGlobeRenderer::currentTerrainLodMatches(
    const QSize &viewport_size) const
{
    if (this->map_model == nullptr)
        return true;

    QVector<MapGlobeTerrainLodTile> surface_tiles;
    surface_tiles.reserve(this->window_tiles.size());
    for (const GlobeTile &tile : this->window_tiles)
    {
        MapGlobeTerrainLodTile surface_tile;
        surface_tile.virtual_x = tile.virtual_x;
        surface_tile.tile_x = tile.tile_x;
        surface_tile.tile_y = tile.tile_y;
        surface_tile.zoom = tile.zoom;
        surface_tile.terrain_zoom = tile.terrain_zoom;
        surface_tile.terrain_available = !tile.terrain_key.isEmpty();
        surface_tile.terrain_cell_count = tile.terrain_cell_count;
        surface_tiles.append(surface_tile);
    }

    return this->surface_scene.terrainLodMatches(
        *this->map_model, surface_tiles, viewport_size);
}

void MapRhiGlobeRenderer::resetTerrainHeightCache()
{
    this->surface_backend.clearTerrainHeightArrayPages();
    this->terrain_height_cache.clear();
    this->terrain_height_cache_frame = 0;
    this->terrain_height_cache_disabled = false;
    this->terrain_height_cache_page_growth_disabled = false;
    this->terrain_height_cache_profile =
        TerrainHeightCacheProfileCounters();
    for (GlobeTile &tile : this->window_tiles)
    {
        tile.terrain_height_array_page = -1;
        tile.terrain_height_array_layer = -1;
        tile.terrain_height_array_ready = false;
    }
}

bool MapRhiGlobeRenderer::createTerrainHeightArrayPage()
{
    return this->surface_backend.createTerrainHeightArrayPage(
        QSize(MapTerrainTileGridSize, MapTerrainTileGridSize),
        GlobeTerrainHeightArrayLayerCount,
        GlobeTerrainHeightArrayMaximumPageCount);
}

void MapRhiGlobeRenderer::releaseTerrainHeightCacheEntry(
    const QString &terrain_key)
{
    const QHash<QString, TerrainHeightCacheEntry>::iterator iterator =
        this->terrain_height_cache.find(terrain_key);
    if (iterator == this->terrain_height_cache.end())
        return;

    const TerrainHeightCacheEntry entry = iterator.value();
    if (entry.array_page >= 0
        && entry.array_page < int(this->surface_backend.terrainHeightArrayPages().size())
        && entry.array_layer >= 0
        && entry.array_layer < GlobeTerrainHeightArrayLayerCount)
    {
        TerrainHeightArrayPage &page =
            this->surface_backend.terrainHeightArrayPages()[entry.array_page];
        if (!page.free_layers.contains(entry.array_layer))
            page.free_layers.append(entry.array_layer);
    }
    this->terrain_height_cache.erase(iterator);
}

MapRhiGlobeRenderer::TerrainHeightCacheEntry *
MapRhiGlobeRenderer::ensureTerrainHeightCacheEntry(
    const QString &terrain_key,
    const QSet<QString> &protected_terrain_keys)
{
    QHash<QString, TerrainHeightCacheEntry>::iterator existing =
        this->terrain_height_cache.find(terrain_key);
    if (existing != this->terrain_height_cache.end())
    {
        existing.value().last_used_frame = this->terrain_height_cache_frame;
        return &existing.value();
    }

    const std::function<int()> free_page_index = [this]()
    {
        for (int page_index = 0;
             page_index < int(this->surface_backend.terrainHeightArrayPages().size());
             ++page_index)
        {
            if (!this->surface_backend.terrainHeightArrayPages()[page_index]
                     .free_layers.isEmpty())
            {
                return page_index;
            }
        }
        return -1;
    };

    int page_index = free_page_index();
    if (page_index < 0
        && !this->terrain_height_cache_page_growth_disabled
        && int(this->surface_backend.terrainHeightArrayPages().size())
            < GlobeTerrainHeightArrayMaximumPageCount)
    {
        if (createTerrainHeightArrayPage())
        {
            page_index = int(this->surface_backend.terrainHeightArrayPages().size()) - 1;
        }
        else
        {
            // A backend allocation failure should not be retried once per
            // remaining visible key. Existing pages remain useful and can
            // continue recycling invisible entries.
            this->terrain_height_cache_page_growth_disabled = true;
        }
    }

    if (page_index < 0)
    {
        QHash<QString, TerrainHeightCacheEntry>::const_iterator oldest =
            this->terrain_height_cache.cend();
        for (QHash<QString, TerrainHeightCacheEntry>::const_iterator iterator =
                 this->terrain_height_cache.cbegin();
             iterator != this->terrain_height_cache.cend(); ++iterator)
        {
            if (protected_terrain_keys.contains(iterator.key()))
                continue;
            if (oldest == this->terrain_height_cache.cend()
                || iterator.value().last_used_frame
                    < oldest.value().last_used_frame)
            {
                oldest = iterator;
            }
        }

        if (oldest != this->terrain_height_cache.cend())
        {
            const QString oldest_key = oldest.key();
            releaseTerrainHeightCacheEntry(oldest_key);
            ++this->terrain_height_cache_profile.evictions;
            page_index = free_page_index();
        }
    }

    if (page_index < 0)
    {
        ++this->terrain_height_cache_profile.capacity_misses;
        return nullptr;
    }

    TerrainHeightArrayPage &page =
        this->surface_backend.terrainHeightArrayPages()[page_index];
    if (!this->surface_backend.terrainHeightArrayPageReady(page_index)
        || page.free_layers.isEmpty())
    {
        ++this->terrain_height_cache_profile.capacity_misses;
        return nullptr;
    }

    TerrainHeightCacheEntry entry;
    entry.array_page = page_index;
    entry.array_layer = page.free_layers.takeLast();
    entry.last_used_frame = this->terrain_height_cache_frame;
    const QHash<QString, TerrainHeightCacheEntry>::iterator inserted =
        this->terrain_height_cache.insert(terrain_key, entry);
    return &inserted.value();
}

void MapRhiGlobeRenderer::prepareTerrainHeightCache(
    QRhiResourceUpdateBatch *resource_updates)
{
    for (GlobeTile &tile : this->window_tiles)
    {
        tile.terrain_height_array_page = -1;
        tile.terrain_height_array_layer = -1;
        tile.terrain_height_array_ready = false;
    }

    if (this->terrain_repository == nullptr || !this->surface_backend.hasContext()
        || resource_updates == nullptr || !this->map_visible
        || this->window_tiles.isEmpty()
        || this->terrain_height_cache_disabled)
    {
        return;
    }

    this->terrain_height_cache_profile =
        TerrainHeightCacheProfileCounters();
    this->terrain_height_cache_profile.enabled =
        globeTerrainHeightCachePerformanceLog().isDebugEnabled();
    QElapsedTimer profile_timer;
    if (this->terrain_height_cache_profile.enabled)
        profile_timer.start();

    QVector<QString> visible_terrain_keys;
    visible_terrain_keys.reserve(this->window_tiles.size());
    QSet<QString> protected_terrain_keys;
    protected_terrain_keys.reserve(this->window_tiles.size());
    for (const GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;
        ++this->terrain_height_cache_profile.visible_terrain_tiles;
        if (protected_terrain_keys.contains(tile.terrain_key))
            continue;
        protected_terrain_keys.insert(tile.terrain_key);
        visible_terrain_keys.append(tile.terrain_key);
    }
    this->terrain_height_cache_profile.unique_visible_dem_tiles =
        visible_terrain_keys.size();
    if (visible_terrain_keys.isEmpty())
        return;

    if (!this->surface_backend.supportsTextureArrays()
        || !this->surface_backend.supportsR32fTextures())
    {
        this->terrain_height_cache_disabled = true;
        if (this->terrain_height_cache_profile.enabled)
        {
            qCDebug(globeTerrainHeightCachePerformanceLog).nospace()
                << "status=unsupported texture_arrays="
                << (this->surface_backend.supportsTextureArrays()
                    ? 1 : 0)
                << " r32f="
                << (this->surface_backend.supportsR32fTextures()
                    ? 1 : 0);
        }
        return;
    }

    ++this->terrain_height_cache_frame;
    if (this->terrain_height_cache_frame == 0)
        this->terrain_height_cache_frame = 1;

    for (const QString &terrain_key : visible_terrain_keys)
    {
        QHash<QString, TerrainHeightCacheEntry>::iterator cache_iterator =
            this->terrain_height_cache.find(terrain_key);
        const bool valid_assignment = cache_iterator
                != this->terrain_height_cache.end()
            && cache_iterator.value().array_page >= 0
            && cache_iterator.value().array_page
                < int(this->surface_backend.terrainHeightArrayPages().size())
            && cache_iterator.value().array_layer >= 0
            && cache_iterator.value().array_layer
                < GlobeTerrainHeightArrayLayerCount
            && this->surface_backend.terrainHeightArrayPageReady(
                cache_iterator.value().array_page);
        if (cache_iterator != this->terrain_height_cache.end()
            && !valid_assignment)
        {
            releaseTerrainHeightCacheEntry(terrain_key);
            cache_iterator = this->terrain_height_cache.end();
        }

        if (cache_iterator != this->terrain_height_cache.end())
        {
            cache_iterator.value().last_used_frame =
                this->terrain_height_cache_frame;
            if (cache_iterator.value().uploaded)
            {
                ++this->terrain_height_cache_profile.available_dem_tiles;
                ++this->terrain_height_cache_profile.cache_hits;
                continue;
            }
        }

        const MapTerrainTile *terrain_tile =
            this->terrain_repository->tile(terrain_key);
        if (terrain_tile == nullptr
            || terrain_tile->elevations_m.size()
                != MapTerrainTileSampleCount
            || !globeTerrainDatumUsable(terrain_tile->vertical_datum))
        {
            continue;
        }
        ++this->terrain_height_cache_profile.available_dem_tiles;

        if (this->terrain_height_cache_profile.uploads
            >= GlobeTerrainHeightUploadBudgetPerFrame)
        {
            if (cache_iterator == this->terrain_height_cache.end())
                ++this->terrain_height_cache_profile.cache_misses;
            ++this->terrain_height_cache_profile.pending_uploads;
            continue;
        }

        TerrainHeightCacheEntry *entry = nullptr;
        if (cache_iterator == this->terrain_height_cache.end())
        {
            ++this->terrain_height_cache_profile.cache_misses;
            entry = ensureTerrainHeightCacheEntry(
                terrain_key, protected_terrain_keys);
        }
        else
        {
            entry = &cache_iterator.value();
        }

        if (entry == nullptr)
        {
            if (this->surface_backend.terrainHeightArrayPages().empty())
            {
                this->terrain_height_cache_disabled = true;
                break;
            }
            continue;
        }

        const qsizetype byte_count = terrain_tile->elevations_m.size()
            * qsizetype(sizeof(float));
        const QByteArray raw_heights(
            reinterpret_cast<const char *>(
                terrain_tile->elevations_m.constData()),
            int(byte_count));
        if (!this->surface_backend.uploadTerrainHeightArrayPageLayerRaw(
                resource_updates, entry->array_page, entry->array_layer,
                raw_heights))
        {
            continue;
        }
        entry->uploaded = true;
        ++this->terrain_height_cache_profile.uploads;
        this->terrain_height_cache_profile.upload_bytes +=
            quint64(byte_count);
    }

    for (GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;
        const QHash<QString, TerrainHeightCacheEntry>::const_iterator iterator =
            this->terrain_height_cache.constFind(tile.terrain_key);
        if (iterator == this->terrain_height_cache.cend()
            || !iterator.value().uploaded)
        {
            continue;
        }
        tile.terrain_height_array_page = iterator.value().array_page;
        tile.terrain_height_array_layer = iterator.value().array_layer;
        tile.terrain_height_array_ready = true;
        ++this->terrain_height_cache_profile.ready_terrain_tiles;
    }

    if (this->terrain_height_cache_profile.enabled)
    {
        this->terrain_height_cache_profile.cpu_ns =
            profile_timer.nsecsElapsed();
        reportTerrainHeightCacheProfile();
    }
}

void MapRhiGlobeRenderer::reportTerrainHeightCacheProfile() const
{
    if (!this->terrain_height_cache_profile.enabled)
        return;
    if (this->terrain_height_cache_profile.cache_misses <= 0
        && this->terrain_height_cache_profile.uploads <= 0
        && this->terrain_height_cache_profile.pending_uploads <= 0
        && this->terrain_height_cache_profile.evictions <= 0
        && this->terrain_height_cache_profile.capacity_misses <= 0)
    {
        return;
    }

    constexpr double NsecsPerMillisecond = 1000000.0;
    constexpr double BytesPerMebibyte = 1024.0 * 1024.0;
    const quint64 allocated_bytes =
        quint64(this->surface_backend.terrainHeightArrayPages().size())
        * quint64(GlobeTerrainHeightArrayLayerCount)
        * GlobeTerrainHeightTileBytes;
    const char *status = this->terrain_height_cache_disabled
        ? "disabled"
        : (this->terrain_height_cache_profile.capacity_misses > 0
            ? "capacity_limited"
            : (this->terrain_height_cache_profile.pending_uploads > 0
                ? "warming" : "ready"));
    qCDebug(globeTerrainHeightCachePerformanceLog).noquote().nospace()
        << "frame=" << this->terrain_height_cache_frame
        << " visible_terrain_tiles="
        << this->terrain_height_cache_profile.visible_terrain_tiles
        << " unique_visible_dem_tiles="
        << this->terrain_height_cache_profile.unique_visible_dem_tiles
        << " available_dem_tiles="
        << this->terrain_height_cache_profile.available_dem_tiles
        << " ready_terrain_tiles="
        << this->terrain_height_cache_profile.ready_terrain_tiles
        << " cache_hits="
        << this->terrain_height_cache_profile.cache_hits
        << " cache_misses="
        << this->terrain_height_cache_profile.cache_misses
        << " uploads=" << this->terrain_height_cache_profile.uploads
        << " pending_uploads="
        << this->terrain_height_cache_profile.pending_uploads
        << " evictions=" << this->terrain_height_cache_profile.evictions
        << " capacity_misses="
        << this->terrain_height_cache_profile.capacity_misses
        << " resident_layers=" << this->terrain_height_cache.size()
        << " pages=" << this->surface_backend.terrainHeightArrayPages().size()
        << " allocated_mib="
        << QString::number(
               double(allocated_bytes) / BytesPerMebibyte, 'f', 3)
        << " upload_mib="
        << QString::number(
               double(this->terrain_height_cache_profile.upload_bytes)
                   / BytesPerMebibyte,
               'f', 3)
        << " cpu_ms="
        << QString::number(
               double(this->terrain_height_cache_profile.cpu_ns)
                   / NsecsPerMillisecond,
               'f', 3)
        << " max_pages=" << GlobeTerrainHeightArrayMaximumPageCount
        << " status=" << status;
}

void MapRhiGlobeRenderer::rebuildWindow(
    const QVector<MapGlobeQuadtreeLeaf> &leaves, const QSize &viewport_size)
{
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
                MapGlobeSurfaceScene::positionKey(tile.zoom, tile.tile_x, tile.tile_y), index);
        }
    }

    // Leaves come straight out of a quadtree partition, so distinct leaves
    // can never legitimately share a (zoom, x, y) identity -- the dedup set
    // here is just defensive bookkeeping against a future bug in the walk,
    // not something normal operation should ever hit.
    QVector<GlobeTile> next_tiles;
    next_tiles.reserve(leaves.size());
    QSet<quint64> seen_positions;
    seen_positions.reserve(leaves.size());

    GeoWgs84Ellipsoid::OrbitCameraBasis terrain_camera_basis;
    const GeoWgs84Ellipsoid::OrbitCameraBasis *terrain_camera_basis_ptr = nullptr;
    if (this->map_model != nullptr && this->terrain_repository != nullptr
        && viewport_size.isValid())
    {
        terrain_camera_basis = GeoWgs84Ellipsoid::orbitCameraBasis(
            this->map_model->centerLon(), this->map_model->centerLat(),
            this->map_model->viewGlobeYawDeg(),
            qBound(
                MapModel::MinViewGlobePitchDeg,
                this->map_model->viewGlobePitchDeg(),
                MapModel::MaxViewGlobePitchDeg),
            qMax(MapModel::MinViewGlobeDistanceM,
                 this->map_model->viewGlobeDistanceM()));
        terrain_camera_basis_ptr = &terrain_camera_basis;
    }

    for (const MapGlobeQuadtreeLeaf &leaf : leaves)
    {
        const quint64 position_key =
            MapGlobeSurfaceScene::positionKey(leaf.zoom, leaf.tile_x, leaf.tile_y);
        if (seen_positions.contains(position_key))
            continue;
        seen_positions.insert(position_key);

        const bool terrain_enabled =
            this->terrain_repository != nullptr
            && leaf.zoom >= GlobeTerrainReliefMinimumZoom;
        const int terrain_zoom = terrain_enabled
            ? globeTerrainZoomForImageryZoom(leaf.zoom)
            : -1;

        GlobeTile tile;
        tile.virtual_x = leaf.tile_x;
        tile.tile_x = leaf.tile_x;
        tile.tile_y = leaf.tile_y;
        tile.zoom = leaf.zoom;
        tile.imagery_key =
            this->map_model->tileCacheKeyAtZoom(leaf.tile_x, leaf.tile_y, leaf.zoom);

        if (terrain_enabled)
        {
            const int zoom_delta = leaf.zoom - terrain_zoom;
            MapTerrainTileAddress terrain_address;
            terrain_address.zoom = terrain_zoom;
            terrain_address.x = quint32(leaf.tile_x) >> zoom_delta;
            terrain_address.y = quint32(leaf.tile_y) >> zoom_delta;
            tile.terrain_zoom = terrain_zoom;
            tile.terrain_key =
                mapTerrainTileKey(globeTerrainDatasetId(), terrain_address);
            tile.terrain_cell_count = terrainCellCountForTile(
                tile, viewport_size, terrain_camera_basis_ptr);
        }

        next_tiles.append(tile);
    }

    // Retain the already-computed leaf identity set. prepare() runs every
    // rendered frame during ordinary camera interaction; rebuilding this same
    // hash set from window_tiles there was avoidable allocator/hash traffic.
    this->window_position_keys = std::move(seen_positions);

    // Same-zoom-neighbour terrain mesh density stitching only -- see
    // updateTerrainStitchCellCounts()'s own scope. A leaf whose neighbour is
    // at a different quadtree zoom (an actual LOD boundary) is not stitched
    // by this pass; that seam is a known, purely cosmetic follow-up (see the
    // class comment) and does not affect correctness or performance here.
    updateTerrainStitchCellCounts(&next_tiles);

    qsizetype estimated_vertex_count = 0;
    qsizetype estimated_index_count = 0;
    for (const GlobeTile &tile : next_tiles)
    {
        const int subdivisions = !tile.terrain_key.isEmpty()
            ? qMax(1, tile.terrain_cell_count)
            : subdivisionsForZoom(tile.zoom);
        const qsizetype grid_width = qsizetype(subdivisions) + 1;
        estimated_vertex_count += grid_width * grid_width;
        estimated_index_count +=
            qsizetype(subdivisions) * qsizetype(subdivisions) * 6;
    }

    this->window_vertices.clear();
    this->window_vertices.reserve(estimated_vertex_count);
    this->window_indices.clear();
    this->window_indices.reserve(estimated_index_count);
    this->window_tiles.clear();
    this->window_tiles.reserve(next_tiles.size());
    this->window_tile_indices_by_position.clear();
    this->window_tile_indices_by_position.reserve(next_tiles.size());

    for (GlobeTile &tile : next_tiles)
    {
        const bool terrain_enabled = !tile.terrain_key.isEmpty();
        tile.first_vertex = this->window_vertices.size();
        tile.first_index = this->window_indices.size();
        const int subdivisions = terrain_enabled
            ? qMax(1, tile.terrain_cell_count)
            : subdivisionsForZoom(tile.zoom);
        const int grid_width = subdivisions + 1;
        const int expected_vertex_count = grid_width * grid_width;
        const int expected_index_count = subdivisions * subdivisions * 6;

        bool reused = false;
        const QHash<quint64, qsizetype>::const_iterator previous_iterator =
            previous_tiles_by_position.constFind(
                MapGlobeSurfaceScene::positionKey(tile.zoom, tile.tile_x, tile.tile_y));
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
                && previous_tile.index_count == expected_index_count
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
                tile.terrain_mesh_has_relief =
                    previous_tile.terrain_mesh_has_relief;
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
                    request.globe_vertical_exaggeration =
                        this->map_model->view3dVerticalExaggeration();
                    request.globe_render_origin_x = this->render_origin_ecef.x;
                    request.globe_render_origin_y = this->render_origin_ecef.y;
                    request.globe_render_origin_z = this->render_origin_ecef.z;

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
                        tile.terrain_mesh_has_relief = true;
                        terrain_built = true;
                    }
                }
            }

            if (!terrain_built)
            {
                for (int row = 0; row <= subdivisions; ++row)
                {
                    const double v = double(row) / double(subdivisions);
                    const double latitude_deg = GeoWebMercator::tileYToLat(
                        double(tile.tile_y) + v, tile.zoom);

                    for (int column = 0; column <= subdivisions; ++column)
                    {
                        const double u = double(column) / double(subdivisions);
                        const double longitude_deg = GeoWebMercator::tileXToLon(
                            double(tile.virtual_x) + u, tile.zoom);
                        this->window_vertices.append(makeTileVertex(
                            longitude_deg, latitude_deg, float(u), float(v)));
                    }
                }
                tile.vertex_count = expected_vertex_count;
            }
        }

        appendIndexedGridIndices(
            &this->window_indices, tile.first_vertex, subdivisions);
        tile.index_count = this->window_indices.size() - tile.first_index;
        updateTerrainRayBounds(&tile);

        this->window_tiles.append(tile);
        this->window_tile_indices_by_position.insert(
            MapGlobeSurfaceScene::positionKey(tile.zoom, tile.tile_x, tile.tile_y),
            this->window_tiles.size() - 1);
    }

    this->window_dirty = false;
    this->window_tiles_requested = false;
    this->surface_backend.invalidateWindowGeometry();
    // Keep the second layer stream lazy: most sessions never enable a
    // heatmap, so they should not allocate or clear one float per terrain
    // vertex merely because the Globe window changed.
    this->window_heatmap_array_layers.clear();
    this->heatmap_array_layer_upload_pending = true;
    this->tile_array_draw_indices_dirty = true;
    this->heatmap_array_draw_indices_dirty = true;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.restart();

    if (this->wireframe_visible)
        rebuildWireframeVertices();
    pruneUnusedTileResources();
}

QVector<MapGlobeQuadtreeLeaf> MapRhiGlobeRenderer::currentWindowLeaves() const
{
    QVector<MapGlobeQuadtreeLeaf> leaves;
    leaves.reserve(this->window_tiles.size());
    for (const GlobeTile &tile : this->window_tiles)
        leaves.append(MapGlobeQuadtreeLeaf{tile.zoom, tile.tile_x, tile.tile_y});
    return leaves;
}

void MapRhiGlobeRenderer::appendWireframeEdges(
    const QVector<TileVertex> &vertices, const QVector<quint32> &indices)
{
    for (qsizetype index = 0; index + 2 < indices.size(); index += 3)
    {
        const quint32 a_index = indices.at(index);
        const quint32 b_index = indices.at(index + 1);
        const quint32 c_index = indices.at(index + 2);
        if (qsizetype(a_index) >= vertices.size()
            || qsizetype(b_index) >= vertices.size()
            || qsizetype(c_index) >= vertices.size())
        {
            continue;
        }

        const TileVertex &a = vertices.at(a_index);
        const TileVertex &b = vertices.at(b_index);
        const TileVertex &c = vertices.at(c_index);
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
        (this->window_indices.size() + this->cap_indices.size()) * 2);
    appendWireframeEdges(this->window_vertices, this->window_indices);
    appendWireframeEdges(this->cap_vertices, this->cap_indices);
    this->surface_backend.invalidateWireframe();
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

    std::map<QString, std::unique_ptr<TileResource>>::iterator iterator =
        this->tile_resources.begin();
    while (iterator != this->tile_resources.end())
    {
        if (keys_in_use.contains(iterator->first))
        {
            ++iterator;
        }
        else
        {
            releaseHeatmapArrayLayer(iterator->second.get());
            releaseTileArrayLayer(iterator->second.get());
            this->surface_backend.releaseTileGpuResource(
                iterator->second->gpu_resource_id);
            iterator = this->tile_resources.erase(iterator);
        }
    }

    trimUnusedTileArrayPages();
    trimUnusedHeatmapArrayPages();
}

bool MapRhiGlobeRenderer::arrayBatchingActive() const
{
    // Heatmaps extend this batching with a fused two-array pipeline, so
    // their activation does not disable imagery batching. The setting is
    // shared with flat RHI batching and remains a pure performance switch.
    if (!guiConfiguration().map_performance.array_batching_enabled
        || !this->surface_backend.imageryArrayPipelineReady()
        || this->surface_backend.imageryArrayPages().empty())
    {
        return false;
    }

    return this->surface_backend.imageryArrayPageReady(0);
}

void MapRhiGlobeRenderer::setTileArrayReady(GlobeTile &tile, bool ready)
{
    // A heatmap-array draw is only valid on top of an imagery-array draw.
    // Preserve that invariant even when late validation disables imagery.
    if (!ready)
        setTileHeatmapArrayReady(tile, false);
    if (tile.array_ready == ready)
        return;

    tile.array_ready = ready;
    this->tile_array_draw_indices_dirty = true;
    this->heatmap_array_draw_indices_dirty = true;
}

void MapRhiGlobeRenderer::setTileHeatmapArrayReady(
    GlobeTile &tile, bool ready)
{
    if (tile.heatmap_array_ready == ready)
        return;

    tile.heatmap_array_ready = ready;
    this->heatmap_array_draw_indices_dirty = true;
}

void MapRhiGlobeRenderer::releaseTileArrayLayer(TileResource *resource)
{
    if (resource == nullptr)
        return;

    if (resource->array_page >= 0
        && resource->array_page < int(this->surface_backend.imageryArrayPages().size())
        && resource->array_layer > 0
        && resource->array_layer < GlobeTileArrayLayerCount)
    {
        TileArrayPage &page = this->surface_backend.imageryArrayPages()[resource->array_page];
        if (!page.free_layers.contains(resource->array_layer))
            page.free_layers.append(resource->array_layer);
    }

    if (resource->array_page >= 0 || resource->array_layer >= 0)
    {
        this->tile_array_draw_indices_dirty = true;
        this->heatmap_array_draw_indices_dirty = true;
    }
    resource->array_page = -1;
    resource->array_layer = -1;
    resource->array_content_revision = 0;
}

void MapRhiGlobeRenderer::releaseHeatmapArrayLayer(TileResource *resource)
{
    if (resource == nullptr)
        return;

    if (resource->heatmap_array_page >= 0
        && resource->heatmap_array_page
            < int(this->surface_backend.heatmapArrayPages().size())
        && resource->heatmap_array_layer > 0
        && resource->heatmap_array_layer < GlobeTileArrayLayerCount)
    {
        HeatmapArrayPage &page =
            this->surface_backend.heatmapArrayPages()[resource->heatmap_array_page];
        if (!page.free_layers.contains(resource->heatmap_array_layer))
            page.free_layers.append(resource->heatmap_array_layer);
    }

    if (resource->heatmap_array_page >= 0
        || resource->heatmap_array_layer >= 0)
    {
        this->heatmap_array_draw_indices_dirty = true;
    }
    resource->heatmap_array_page = -1;
    resource->heatmap_array_layer = -1;
    resource->heatmap_array_revision = 0;
}

void MapRhiGlobeRenderer::trimUnusedTileArrayPages()
{
    while (this->surface_backend.imageryArrayPages().size() > 1)
    {
        const TileArrayPage &page = this->surface_backend.imageryArrayPages().back();
        if (page.free_layers.size() != GlobeTileArrayUsableLayerCount)
            break;
        this->heatmap_array_draw_batches.clear();
        this->surface_backend.clearHeatmapArrayBatchBindings();
        this->surface_backend.popImageryArrayPage();
        this->tile_array_draw_indices_dirty = true;
        this->heatmap_array_draw_indices_dirty = true;
    }
}

void MapRhiGlobeRenderer::trimUnusedHeatmapArrayPages()
{
    while (this->surface_backend.heatmapArrayPages().size() > 1)
    {
        const HeatmapArrayPage &page = this->surface_backend.heatmapArrayPages().back();
        if (page.free_layers.size() != GlobeTileArrayUsableLayerCount)
            break;
        this->heatmap_array_draw_batches.clear();
        this->surface_backend.clearHeatmapArrayBatchBindings();
        this->surface_backend.popHeatmapArrayPage();
        this->heatmap_array_draw_indices_dirty = true;
    }
}

void MapRhiGlobeRenderer::resetWindowArrayLayers()
{
    bool vertices_changed = false;
    for (GlobeTile &tile : this->window_tiles)
    {
        setTileArrayReady(tile, false);
        setTileHeatmapArrayReady(tile, false);
    }
    for (TileVertex &vertex : this->window_vertices)
    {
        if (vertex.layer != 0.0f)
        {
            vertex.layer = 0.0f;
            vertices_changed = true;
        }
    }
    if (vertices_changed)
        this->surface_backend.invalidateWindowVertices();

    if (!this->window_heatmap_array_layers.isEmpty())
    {
        // A future heatmap assignment lazily restores the correctly sized
        // zero-filled stream before stamping its first page-local layer.
        this->window_heatmap_array_layers.clear();
        this->heatmap_array_layer_upload_pending = true;
    }
    this->tile_array_draw_indices_dirty = true;
    this->heatmap_array_draw_indices_dirty = true;
}

void MapRhiGlobeRenderer::rebuildTileArrayDrawIndices()
{
    this->tile_array_draw_indices.clear();
    for (TileArrayPage &page : this->surface_backend.imageryArrayPages())
    {
        page.first_draw_index = 0;
        page.draw_index_count = 0;
    }

    if (!arrayBatchingActive())
    {
        this->tile_array_draw_indices_dirty = false;
        this->tile_array_draw_index_upload_pending = false;
        return;
    }

    // Validate transient readiness before grouping. A bad/stale resource
    // reference must fall back to the per-tile renderer rather than being
    // skipped merely because array_ready was left true.
    for (GlobeTile &tile : this->window_tiles)
    {
        if (!tile.array_ready)
            continue;

        const bool valid_page_index = tile.resource != nullptr
            && tile.resource->array_page >= 0
            && tile.resource->array_page < int(this->surface_backend.imageryArrayPages().size());
        bool valid_page = false;
        if (valid_page_index)
        {
            valid_page = this->surface_backend.imageryArrayPageReady(
                tile.resource->array_page);
        }
        const bool valid_resource = valid_page
            && tile.resource->array_layer > 0
            && tile.resource->array_layer < GlobeTileArrayLayerCount;
        const bool valid_geometry = tile.first_index >= 0
            && tile.index_count > 0
            && qsizetype(tile.first_index) + tile.index_count
                <= this->window_indices.size();
        if (!valid_resource || !valid_geometry)
            setTileArrayReady(tile, false);
    }

    this->tile_array_draw_indices.reserve(this->window_indices.size());
    for (int page_index = 0;
         page_index < int(this->surface_backend.imageryArrayPages().size()); ++page_index)
    {
        TileArrayPage &page = this->surface_backend.imageryArrayPages()[page_index];
        page.first_draw_index = this->tile_array_draw_indices.size();
        for (const GlobeTile &tile : this->window_tiles)
        {
            if (!tile.array_ready || tile.resource == nullptr
                || tile.resource->array_page != page_index)
            {
                continue;
            }

            const qsizetype destination_first =
                this->tile_array_draw_indices.size();
            this->tile_array_draw_indices.resize(
                destination_first + tile.index_count);
            std::copy_n(
                this->window_indices.constData() + tile.first_index,
                tile.index_count,
                this->tile_array_draw_indices.data() + destination_first);
        }
        page.draw_index_count =
            this->tile_array_draw_indices.size() - page.first_draw_index;
    }

    this->tile_array_draw_indices_dirty = false;
    this->tile_array_draw_index_upload_pending =
        !this->tile_array_draw_indices.isEmpty();
}

bool MapRhiGlobeRenderer::uploadTileArrayDrawIndices(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource_updates == nullptr)
        return false;
    if (this->tile_array_draw_indices_dirty)
        rebuildTileArrayDrawIndices();
    if (!this->tile_array_draw_index_upload_pending)
        return true;

    if (this->tile_array_draw_indices.isEmpty())
    {
        this->tile_array_draw_index_upload_pending = false;
        return true;
    }

    if (!this->surface_backend.uploadImageryArrayDrawIndices(
            resource_updates, this->tile_array_draw_indices,
            this->window_indices.size()))
    {
        return false;
    }

    this->tile_array_draw_index_upload_pending = false;
    return true;
}

bool MapRhiGlobeRenderer::heatmapArrayBatchingActive() const
{
    if (!arrayBatchingActive()
        || this->heatmap_scene.isEmpty()
        || !this->surface_backend.heatmapArrayPipelineReady()
        || this->surface_backend.heatmapArrayPages().empty())
    {
        return false;
    }

    return this->surface_backend.heatmapArrayPageReady(0);
}

bool MapRhiGlobeRenderer::rebuildHeatmapArrayDrawIndices()
{
    this->heatmap_array_draw_indices.clear();
    this->heatmap_array_draw_batches.clear();

    if (!heatmapArrayBatchingActive())
    {
        this->heatmap_array_draw_indices_dirty = false;
        this->heatmap_array_draw_index_upload_pending = false;
        return true;
    }

    bool has_visible_heatmap = false;
    for (GlobeTile &tile : this->window_tiles)
    {
        if (!tile.array_ready)
            continue;

        const bool valid_imagery_page_index = tile.resource != nullptr
            && tile.resource->array_page >= 0
            && tile.resource->array_page
                < int(this->surface_backend.imageryArrayPages().size());
        bool valid_imagery_page = false;
        if (valid_imagery_page_index)
        {
            valid_imagery_page = this->surface_backend.imageryArrayPageReady(
                tile.resource->array_page);
        }

        const bool has_heatmap = tile.resource != nullptr
            && tile.resource->heatmap_has_content;
        const bool valid_heatmap_page_index = has_heatmap
            && tile.resource->heatmap_array_page >= 0
            && tile.resource->heatmap_array_page
                < int(this->surface_backend.heatmapArrayPages().size());
        bool valid_heatmap_page = !has_heatmap;
        if (valid_heatmap_page_index)
        {
            valid_heatmap_page = this->surface_backend.heatmapArrayPageReady(
                tile.resource->heatmap_array_page);
        }
        const bool valid_heatmap = valid_heatmap_page
            && (!has_heatmap
                || (tile.heatmap_array_ready
                    && tile.resource->heatmap_revision
                        == this->heatmap_scene.revision()
                    && tile.resource->heatmap_array_revision
                        == this->heatmap_scene.revision()
                    && tile.resource->heatmap_array_layer > 0
                    && tile.resource->heatmap_array_layer
                        < GlobeTileArrayLayerCount));
        const bool valid_resource = valid_imagery_page
            && valid_heatmap
            && tile.resource->array_layer > 0
            && tile.resource->array_layer < GlobeTileArrayLayerCount;
        const bool valid_geometry = tile.first_index >= 0
            && tile.index_count > 0
            && qsizetype(tile.first_index) + tile.index_count
                <= this->window_indices.size();
        if (!valid_resource || !valid_geometry)
        {
            setTileArrayReady(tile, false);
            continue;
        }
        if (has_heatmap)
            has_visible_heatmap = true;
    }

    if (!has_visible_heatmap)
    {
        this->heatmap_array_draw_indices_dirty = false;
        this->heatmap_array_draw_index_upload_pending = false;
        return true;
    }

    this->heatmap_array_draw_indices.reserve(this->window_indices.size());
    for (int imagery_page_index = 0;
         imagery_page_index < int(this->surface_backend.imageryArrayPages().size());
         ++imagery_page_index)
    {
        for (int heatmap_page_index = 0;
             heatmap_page_index < int(this->surface_backend.heatmapArrayPages().size());
             ++heatmap_page_index)
        {
            const int first_draw_index =
                this->heatmap_array_draw_indices.size();
            for (const GlobeTile &tile : this->window_tiles)
            {
                if (!tile.array_ready || tile.resource == nullptr
                    || tile.resource->array_page != imagery_page_index)
                {
                    continue;
                }

                const int effective_heatmap_page =
                    tile.resource->heatmap_has_content
                    ? tile.resource->heatmap_array_page : 0;
                if (effective_heatmap_page != heatmap_page_index)
                    continue;

                const qsizetype destination_first =
                    this->heatmap_array_draw_indices.size();
                this->heatmap_array_draw_indices.resize(
                    destination_first + tile.index_count);
                std::copy_n(
                    this->window_indices.constData() + tile.first_index,
                    tile.index_count,
                    this->heatmap_array_draw_indices.data()
                        + destination_first);
            }

            const int draw_index_count =
                this->heatmap_array_draw_indices.size() - first_draw_index;
            if (draw_index_count <= 0)
                continue;

            HeatmapArrayDrawBatch batch;
            batch.imagery_page_index = imagery_page_index;
            batch.heatmap_page_index = heatmap_page_index;
            batch.first_draw_index = first_draw_index;
            batch.draw_index_count = draw_index_count;
            if (!this->surface_backend.ensureHeatmapArrayBatchBinding(
                    imagery_page_index, heatmap_page_index))
            {
                this->heatmap_array_draw_indices.clear();
                this->heatmap_array_draw_batches.clear();
                return false;
            }
            this->heatmap_array_draw_batches.push_back(batch);
        }
    }

    this->heatmap_array_draw_indices_dirty = false;
    this->heatmap_array_draw_index_upload_pending =
        !this->heatmap_array_draw_indices.isEmpty();
    return true;
}

bool MapRhiGlobeRenderer::uploadHeatmapArrayDrawIndices(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource_updates == nullptr)
        return false;
    if (this->heatmap_array_draw_indices_dirty)
    {
        if (!rebuildHeatmapArrayDrawIndices())
            return false;
    }
    if (!this->heatmap_array_draw_index_upload_pending)
        return true;

    if (this->heatmap_array_draw_indices.isEmpty())
    {
        this->heatmap_array_draw_index_upload_pending = false;
        return true;
    }

    if (!this->surface_backend.uploadHeatmapArrayDrawIndices(
            resource_updates,
            this->heatmap_array_draw_indices, this->window_indices.size()))
    {
        return false;
    }

    this->heatmap_array_draw_index_upload_pending = false;
    return true;
}

bool MapRhiGlobeRenderer::uploadHeatmapArrayLayers(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource_updates == nullptr)
        return false;
    if (!this->heatmap_array_layer_upload_pending)
        return true;

    if (this->window_heatmap_array_layers.isEmpty()
        || this->window_heatmap_array_layers.size()
            != this->window_vertices.size())
    {
        return false;
    }

    if (!this->surface_backend.uploadHeatmapArrayLayers(
            resource_updates,
            this->window_heatmap_array_layers))
    {
        return false;
    }

    this->heatmap_array_layer_upload_pending = false;
    return true;
}

QImage MapRhiGlobeRenderer::currentTileArrayImage(
    const GlobeTile &tile, const TileResource &resource) const
{
    if (this->tile_repository == nullptr || tile.imagery_key.isEmpty())
        return QImage();

    if (!resource.is_provisional)
    {
        const QPixmap *pixmap = this->tile_repository->tile(tile.imagery_key);
        if (pixmap == nullptr || pixmap->isNull()
            || pixmap->cacheKey() != resource.pixmap_cache_key)
        {
            return QImage();
        }
        return pixmap->toImage().convertToFormat(QImage::Format_RGBA8888);
    }

    const QString children_key = QStringLiteral("children:%1/%2/%3")
        .arg(tile.zoom).arg(tile.tile_x).arg(tile.tile_y);
    if (resource.provisional_source_key == children_key)
    {
        const int child_zoom = tile.zoom + 1;
        const int child_x0 = tile.tile_x * 2;
        const int child_y0 = tile.tile_y * 2;
        QImage child_images[2][2];
        int child_w = 0;
        int child_h = 0;
        for (int dx = 0; dx < 2; ++dx)
        {
            for (int dy = 0; dy < 2; ++dy)
            {
                const QString child_key = this->map_model->tileCacheKeyAtZoom(
                    child_x0 + dx, child_y0 + dy, child_zoom);
                const QPixmap *child_pixmap = this->tile_repository->tile(child_key);
                if (child_pixmap == nullptr || child_pixmap->isNull())
                    return QImage();

                child_images[dx][dy] = child_pixmap->toImage().convertToFormat(
                    QImage::Format_RGBA8888);
                if (child_images[dx][dy].isNull())
                    return QImage();
                child_w = qMax(child_w, child_images[dx][dy].width());
                child_h = qMax(child_h, child_images[dx][dy].height());
            }
        }

        if (child_w <= 0 || child_h <= 0)
            return QImage();

        QImage composite(child_w * 2, child_h * 2, QImage::Format_RGBA8888);
        QPainter painter(&composite);
        for (int dx = 0; dx < 2; ++dx)
        {
            for (int dy = 0; dy < 2; ++dy)
            {
                painter.drawImage(
                    QRect(dx * child_w, dy * child_h, child_w, child_h),
                    child_images[dx][dy]);
            }
        }
        painter.end();
        return composite;
    }

    for (int levels_up = 1; tile.zoom - levels_up >= 0; ++levels_up)
    {
        const int ancestor_zoom = tile.zoom - levels_up;
        const quint32 ancestor_x = quint32(tile.tile_x) >> levels_up;
        const quint32 ancestor_y = quint32(tile.tile_y) >> levels_up;
        const QString ancestor_key = this->map_model->tileCacheKeyAtZoom(
            int(ancestor_x), int(ancestor_y), ancestor_zoom);
        if (ancestor_key != resource.provisional_source_key)
            continue;

        const QPixmap *ancestor_pixmap = this->tile_repository->tile(ancestor_key);
        if (ancestor_pixmap == nullptr || ancestor_pixmap->isNull())
            return QImage();

        const int span = 1 << levels_up;
        const int local_x = tile.tile_x & (span - 1);
        const int local_y = tile.tile_y & (span - 1);
        const int source_w = ancestor_pixmap->width();
        const int source_h = ancestor_pixmap->height();
        const QRect crop_rect(
            local_x * source_w / span, local_y * source_h / span,
            qMax(1, source_w / span), qMax(1, source_h / span));
        return ancestor_pixmap->copy(crop_rect)
            .toImage().convertToFormat(QImage::Format_RGBA8888)
            .scaled(source_w, source_h, Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
    }

    return QImage();
}

bool MapRhiGlobeRenderer::stampTileArrayLayer(
    GlobeTile &tile, const TileResource &resource,
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource.array_layer < 0 || tile.first_vertex < 0
        || tile.vertex_count <= 0
        || qsizetype(tile.first_vertex) + tile.vertex_count
            > this->window_vertices.size())
    {
        return false;
    }

    const float expected_layer = float(resource.array_layer);
    if (this->window_vertices.at(tile.first_vertex).layer == expected_layer)
        return true;

    for (int index = 0; index < tile.vertex_count; ++index)
        this->window_vertices[tile.first_vertex + index].layer = expected_layer;

    // Geometry rebuilds are followed by a full upload later in prepare().
    // Once the backend owns a current buffer, patch only this tile's
    // contiguous range when its imagery first acquires an array layer.
    if (!this->surface_backend.patchWindowVertices(
            resource_updates, tile.first_vertex, tile.vertex_count,
            this->window_vertices))
    {
        this->surface_backend.invalidateWindowVertices();
    }
    return true;
}

bool MapRhiGlobeRenderer::ensureTileArrayLayer(
    GlobeTile &tile, const QImage &updated_image,
    QRhiResourceUpdateBatch *resource_updates)
{
    if (!arrayBatchingActive() || tile.is_cap || tile.resource == nullptr
        || !tileTextureReady(tile.resource) || resource_updates == nullptr)
    {
        setTileArrayReady(tile, false);
        return true;
    }

    TileResource *resource = tile.resource;
    bool valid_assignment = resource->array_page >= 0
        && resource->array_page < int(this->surface_backend.imageryArrayPages().size())
        && resource->array_layer > 0
        && resource->array_layer < GlobeTileArrayLayerCount;
    if (valid_assignment)
    {
        valid_assignment = this->surface_backend.imageryArrayPageReady(
            resource->array_page);
    }
    if (!valid_assignment
        && (resource->array_page >= 0 || resource->array_layer >= 0))
    {
        releaseTileArrayLayer(resource);
    }

    bool assigned_now = false;
    if (resource->array_page < 0)
    {
        int page_index = -1;
        for (int index = 0;
             index < int(this->surface_backend.imageryArrayPages().size()); ++index)
        {
            if (!this->surface_backend.imageryArrayPages()[index].free_layers.isEmpty())
            {
                page_index = index;
                break;
            }
        }

        if (page_index < 0
            && int(this->surface_backend.imageryArrayPages().size())
                < GlobeTileArrayMaximumPageCount
            && createTileArrayPage())
        {
            page_index = int(this->surface_backend.imageryArrayPages().size()) - 1;
        }
        if (page_index < 0)
        {
            setTileArrayReady(tile, false);
            return true;
        }

        TileArrayPage &page = this->surface_backend.imageryArrayPages()[page_index];
        resource->array_page = page_index;
        resource->array_layer = page.free_layers.takeLast();
        resource->array_content_revision = 0;
        assigned_now = true;
    }

    if (resource->content_revision == 0)
    {
        if (assigned_now)
            releaseTileArrayLayer(resource);
        setTileArrayReady(tile, false);
        return true;
    }

    if (resource->array_content_revision != resource->content_revision)
    {
        QImage image = updated_image;
        if (image.isNull())
            image = currentTileArrayImage(tile, *resource);
        if (image.isNull())
        {
            if (assigned_now)
                releaseTileArrayLayer(resource);
            setTileArrayReady(tile, false);
            return true;
        }

        const QSize layer_size(MapModel::TileSize, MapModel::TileSize);
        if (image.size() != layer_size)
        {
            image = image.scaled(
                layer_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        if (image.isNull())
        {
            if (assigned_now)
                releaseTileArrayLayer(resource);
            setTileArrayReady(tile, false);
            return true;
        }

        if (!this->surface_backend.uploadImageryArrayPageLayer(
                resource_updates, resource->array_page,
                resource->array_layer, image))
        {
            return false;
        }
        resource->array_content_revision = resource->content_revision;
    }

    if (!stampTileArrayLayer(tile, *resource, resource_updates))
    {
        setTileArrayReady(tile, false);
        return true;
    }
    setTileArrayReady(tile, true);
    return true;
}

bool MapRhiGlobeRenderer::stampTileHeatmapArrayLayer(
    GlobeTile &tile, int layer,
    QRhiResourceUpdateBatch *resource_updates)
{
    if (layer < 0 || tile.first_vertex < 0
        || tile.vertex_count <= 0
        || qsizetype(tile.first_vertex) + tile.vertex_count
            > this->window_vertices.size())
    {
        return false;
    }

    if (this->window_heatmap_array_layers.size()
        != this->window_vertices.size())
    {
        this->window_heatmap_array_layers.fill(
            0.0f, this->window_vertices.size());
        this->heatmap_array_layer_upload_pending = true;
    }

    const float expected_layer = float(layer);
    if (this->window_heatmap_array_layers.at(tile.first_vertex)
        == expected_layer)
    {
        return true;
    }

    for (int index = 0; index < tile.vertex_count; ++index)
    {
        this->window_heatmap_array_layers[tile.first_vertex + index] =
            expected_layer;
    }

    if (!this->heatmap_array_layer_upload_pending
        && !this->surface_backend.patchHeatmapArrayLayers(
            resource_updates, tile.first_vertex, tile.vertex_count,
            this->window_heatmap_array_layers))
    {
        this->heatmap_array_layer_upload_pending = true;
    }
    return true;
}

bool MapRhiGlobeRenderer::ensureTileHeatmapArray(
    GlobeTile &tile, const QImage &updated_image,
    const QVector<HeatmapStamp> &updated_stamps,
    QRhiResourceUpdateBatch *resource_updates)
{
    ScopedHeatmapProfileTimer profile_timer(
        &this->heatmap_profile.cpu_ns, this->heatmap_profile.enabled);
    if (tile.resource == nullptr)
    {
        setTileHeatmapArrayReady(tile, false);
        return true;
    }

    TileResource *resource = tile.resource;
    if (!resource->heatmap_has_content)
    {
        if (!this->window_heatmap_array_layers.isEmpty()
            && !stampTileHeatmapArrayLayer(tile, 0, resource_updates))
        {
            return false;
        }
        releaseHeatmapArrayLayer(resource);
        setTileHeatmapArrayReady(tile, false);
        return true;
    }

    if (!tile.array_ready || resource_updates == nullptr
        || !createHeatmapArrayResources(resource_updates))
    {
        // An affected tile must retain the ordinary combined
        // imagery/heatmap draw when the heatmap array path is unavailable.
        setTileHeatmapArrayReady(tile, false);
        if (tile.array_ready)
            setTileArrayReady(tile, false);
        return true;
    }

    bool valid_assignment = resource->heatmap_array_page >= 0
        && resource->heatmap_array_page
            < int(this->surface_backend.heatmapArrayPages().size())
        && resource->heatmap_array_layer > 0
        && resource->heatmap_array_layer < GlobeTileArrayLayerCount;
    if (valid_assignment)
    {
        valid_assignment = this->surface_backend.heatmapArrayPageReady(
            resource->heatmap_array_page);
    }
    if (!valid_assignment
        && (resource->heatmap_array_page >= 0
            || resource->heatmap_array_layer >= 0))
    {
        releaseHeatmapArrayLayer(resource);
    }

    bool assigned_now = false;
    if (resource->heatmap_array_page < 0)
    {
        int page_index = -1;
        for (int index = 0;
             index < int(this->surface_backend.heatmapArrayPages().size()); ++index)
        {
            if (!this->surface_backend.heatmapArrayPages()[index].free_layers.isEmpty())
            {
                page_index = index;
                break;
            }
        }

        if (page_index < 0
            && int(this->surface_backend.heatmapArrayPages().size())
                < GlobeTileArrayMaximumPageCount
            && createHeatmapArrayPage(resource_updates))
        {
            page_index = int(this->surface_backend.heatmapArrayPages().size()) - 1;
        }
        if (page_index < 0)
        {
            setTileHeatmapArrayReady(tile, false);
            setTileArrayReady(tile, false);
            return true;
        }

        HeatmapArrayPage &page = this->surface_backend.heatmapArrayPages()[page_index];
        resource->heatmap_array_page = page_index;
        resource->heatmap_array_layer = page.free_layers.takeLast();
        resource->heatmap_array_revision = 0;
        assigned_now = true;
    }

    if (!stampTileHeatmapArrayLayer(
            tile, resource->heatmap_array_layer, resource_updates))
    {
        setTileHeatmapArrayReady(tile, false);
        setTileArrayReady(tile, false);
        return true;
    }

    if (resource->heatmap_array_revision != this->heatmap_scene.revision())
    {
        if (!this->heatmap_gpu_baking_disabled)
        {
            QVector<HeatmapStamp> regenerated_stamps;
            const QVector<HeatmapStamp> *stamps = &updated_stamps;
            if (stamps->isEmpty())
            {
                regenerated_stamps = heatmapStampsForTileProfiled(
                    tile, resource);
                stamps = &regenerated_stamps;
            }

            if (stamps->isEmpty())
            {
                if (!this->window_heatmap_array_layers.isEmpty()
                    && !stampTileHeatmapArrayLayer(
                        tile, 0, resource_updates))
                {
                    return false;
                }
                resource->heatmap_has_content = false;
                resource->heatmap_revision = this->heatmap_scene.revision();
                releaseHeatmapArrayLayer(resource);
                setTileHeatmapArrayReady(tile, false);
                return true;
            }

            if (ensureDiagnosticHeatmapGpuBakeResources()
                && queueHeatmapGpuBake(
                    resource,
                    MapRhiGlobeHeatmapBakeDestination::HeatmapArrayLayer,
                    *stamps, resource->heatmap_array_page,
                    resource->heatmap_array_layer))
            {
                // Reserve both revisions so the fused draw list built later
                // in prepare() can include this tile. A discarded frame or
                // failed bake rolls them back from the retained job queue;
                // runPendingHeatmapGpuBakes() confirms them after recording
                // the atlas-to-layer copy.
                resource->heatmap_array_revision = this->heatmap_scene.revision();
                resource->heatmap_revision = this->heatmap_scene.revision();
                setTileHeatmapArrayReady(tile, true);
                return true;
            }

            disableHeatmapGpuBaking();
        }

        // Automatic failure path only: preserve the established CPU raster
        // and host upload if offscreen baking or texture copies are not
        // available on this backend.
        QImage image = updated_image;
        if (image.isNull())
            image = renderHeatmapTileProfiled(tile, resource);
        if (!image.isNull())
        {
            image = image.convertToFormat(
                QImage::Format_RGBA8888_Premultiplied);
        }
        const QSize layer_size(
            GlobeHeatmapTextureSize, GlobeHeatmapTextureSize);
        if (!image.isNull() && image.size() != layer_size)
        {
            image = image.scaled(
                layer_size, Qt::IgnoreAspectRatio,
                Qt::SmoothTransformation);
        }
        if (image.isNull())
        {
            if (assigned_now)
                releaseHeatmapArrayLayer(resource);
            setTileHeatmapArrayReady(tile, false);
            setTileArrayReady(tile, false);
            return true;
        }

        if (!this->surface_backend.uploadHeatmapArrayPageLayer(
                resource_updates, resource->heatmap_array_page,
                resource->heatmap_array_layer, image))
        {
            return false;
        }
        if (this->heatmap_profile.enabled)
        {
            ++this->heatmap_profile.array_uploads;
            this->heatmap_profile.upload_bytes += quint64(image.sizeInBytes());
        }
        resource->heatmap_array_revision = this->heatmap_scene.revision();
        resource->heatmap_revision = this->heatmap_scene.revision();
    }

    setTileHeatmapArrayReady(tile, true);
    return true;
}

bool MapRhiGlobeRenderer::ensureTileGpuResource(TileResource *resource)
{
    if (resource == nullptr)
        return false;
    if (resource->gpu_resource_id != 0)
        return true;
    resource->gpu_resource_id = this->surface_backend.createTileGpuResource();
    return resource->gpu_resource_id != 0;
}

bool MapRhiGlobeRenderer::tileTextureReady(const TileResource *resource) const
{
    return resource != nullptr
        && this->surface_backend.hasTileTexture(resource->gpu_resource_id);
}

bool MapRhiGlobeRenderer::tileHeatmapTextureReady(
    const TileResource *resource) const
{
    return resource != nullptr
        && this->surface_backend.hasTileHeatmapTexture(resource->gpu_resource_id);
}

bool MapRhiGlobeRenderer::tileBindingsReady(const TileResource *resource) const
{
    return resource != nullptr
        && this->surface_backend.hasTileBindings(resource->gpu_resource_id);
}

void MapRhiGlobeRenderer::invalidateTileBindings(TileResource *resource)
{
    if (resource != nullptr)
        this->surface_backend.invalidateTileBindings(resource->gpu_resource_id);
}

void MapRhiGlobeRenderer::clearTileHeatmapTexture(TileResource *resource)
{
    if (resource != nullptr)
        this->surface_backend.clearTileHeatmapTexture(resource->gpu_resource_id);
}

bool MapRhiGlobeRenderer::recreateTileTexture(
    TileResource *resource, const QSize &size)
{
    return ensureTileGpuResource(resource)
        && this->surface_backend.recreateTileTexture(
            resource->gpu_resource_id, size);
}

bool MapRhiGlobeRenderer::recreateTileHeatmapTexture(
    TileResource *resource, const QSize &size)
{
    return ensureTileGpuResource(resource)
        && this->surface_backend.recreateTileHeatmapTexture(
            resource->gpu_resource_id, size);
}

bool MapRhiGlobeRenderer::rebuildTileBindings(TileResource *resource)
{
    if (!ensureTileGpuResource(resource) || !tileTextureReady(resource))
        return false;
    return this->surface_backend.rebuildTileBindings(
        resource->gpu_resource_id);
}

bool MapRhiGlobeRenderer::ensureTileResource(
    GlobeTile &tile, QRhiResourceUpdateBatch *resource_updates,
    QImage *updated_image, QImage *updated_heatmap_image,
    QVector<HeatmapStamp> *updated_heatmap_stamps)
{
    if (updated_image != nullptr)
        *updated_image = QImage();
    if (updated_heatmap_image != nullptr)
        *updated_heatmap_image = QImage();
    if (updated_heatmap_stamps != nullptr)
        updated_heatmap_stamps->clear();

    if (tile.is_cap)
    {
        if (!ensureTileGpuResource(&this->cap_resource))
            return false;
        if (!tileTextureReady(&this->cap_resource))
        {
            QImage image(1, 1, QImage::Format_RGBA8888);
            image.fill(GlobePolarCapColor);
            if (!recreateTileTexture(&this->cap_resource, image.size()))
                return false;
            if (!this->surface_backend.uploadTileTextureImage(
                    resource_updates, this->cap_resource.gpu_resource_id, image))
            {
                return false;
            }
        }
        if (!tileBindingsReady(&this->cap_resource)
            && !rebuildTileBindings(&this->cap_resource))
        {
            return false;
        }

        tile.resource = &this->cap_resource;
        return true;
    }

    if (this->tile_repository == nullptr || tile.imagery_key.isEmpty())
        return true;

    const QPixmap *pixmap = this->tile_repository->tile(tile.imagery_key);

    std::unique_ptr<TileResource> &slot = this->tile_resources[tile.imagery_key];
    if (!slot)
        slot = std::make_unique<TileResource>();
    TileResource *resource = slot.get();
    if (!ensureTileGpuResource(resource))
        return false;

    if (pixmap == nullptr)
    {
        if (!ensureProvisionalTileResource(
                tile, resource, resource_updates, updated_image))
            return false;
        // tile.resource is only actually set on some of
        // ensureProvisionalTileResource()'s success paths -- see its own
        // comment on the "nothing to derive a placeholder from yet" case,
        // which deliberately leaves it untouched so draw() falls back to
        // template_bindings instead. No heatmap texture to prepare for a
        // tile that isn't even going to use this resource.
        if (tile.resource == resource
            && !ensureHeatmapTexture(
                tile, resource, resource_updates,
                updated_heatmap_image, updated_heatmap_stamps,
                !arrayBatchingActive()))
        {
            return false;
        }
        return true;
    }

    const qint64 cache_key = pixmap->cacheKey();
    if (!tileTextureReady(resource) || resource->is_provisional || resource->pixmap_cache_key != cache_key)
    {
        QImage image = pixmap->toImage().convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull())
            return true;

        invalidateTileBindings(resource);
        if (!recreateTileTexture(resource, image.size()))
        {
            return false;
        }
        if (!this->surface_backend.uploadTileTextureImage(
                resource_updates, resource->gpu_resource_id, image))
        {
            return false;
        }
        resource->pixmap_cache_key = cache_key;
        resource->is_provisional = false;
        resource->provisional_source_key.clear();
        ++resource->content_revision;
        if (resource->content_revision == 0)
            resource->content_revision = 1;
        if (updated_image != nullptr)
            *updated_image = image;
    }

    if (!tileBindingsReady(resource) && !rebuildTileBindings(resource))
        return false;

    if (!ensureHeatmapTexture(
            tile, resource, resource_updates, updated_heatmap_image,
            updated_heatmap_stamps,
            !arrayBatchingActive()))
        return false;

    tile.resource = resource;
    return true;
}

// Falls back to a placeholder derived from already-loaded neighboring tiles
// while tile's own imagery is still in flight, instead of the flat
// GlobeMissingTileColor fill -- the same "keep showing something real
// instead of a blank/flat placeholder" goal the flat 2D basemap
// renderer's parent/child LOD handoff serves. The resulting image is kept
// in the ordinary per-tile texture and, when a layer is available, uploaded
// into the shared Globe array as well.
//
// Tries two directions, in this order:
//
// 1. Descendants (zooming OUT): the tile being merged into is brand new
//    and was never itself fetched before -- only its children were, at the
//    finer zoom the camera is pulling back from. If all four direct
//    children are already cached, compose them into a 2x2 mosaic. This is
//    likely *better* detail than the real coarse tile will eventually have
//    (assembled from 4x the resolution), and it's almost certainly the
//    exact same imagery that was already on screen a moment ago. Only
//    checks direct children, not grandchildren -- covers ordinary one-step
//    zoom-out; a large jump that skips levels falls through to (2).
// 2. Ancestors (zooming IN): the tile being subdivided into already has a
//    loaded parent (or grandparent, ...) covering the same area at coarser
//    detail -- crop it to this tile's footprint and upscale.
//
// Cheap even though it can run every frame per still-loading tile: at most
// a handful of hash lookups plus, on the frame a composite/crop is first
// produced, one CPU image composite -- no network or disk I/O, and the
// result is cached on the resource (via is_provisional/provisional_source_key)
// so it isn't redone every frame while still waiting.
bool MapRhiGlobeRenderer::ensureProvisionalTileResource(
    GlobeTile &tile, TileResource *resource,
    QRhiResourceUpdateBatch *resource_updates, QImage *updated_image)
{
    const QString children_key = QStringLiteral("children:%1/%2/%3")
        .arg(tile.zoom).arg(tile.tile_x).arg(tile.tile_y);
    if (resource->is_provisional && tileTextureReady(resource)
        && resource->provisional_source_key == children_key)
    {
        tile.resource = resource;
        return true;
    }

    const int child_zoom = tile.zoom + 1;
    const int child_x0 = tile.tile_x * 2;
    const int child_y0 = tile.tile_y * 2;
    QImage child_images[2][2];
    int child_w = 0;
    int child_h = 0;
    bool all_children_cached = true;
    for (int dx = 0; dx < 2 && all_children_cached; ++dx)
    {
        for (int dy = 0; dy < 2 && all_children_cached; ++dy)
        {
            const QString child_key = this->map_model->tileCacheKeyAtZoom(
                child_x0 + dx, child_y0 + dy, child_zoom);
            const QPixmap *child_pixmap = this->tile_repository->tile(child_key);
            if (child_pixmap == nullptr || child_pixmap->isNull())
            {
                all_children_cached = false;
                break;
            }

            const QImage image = child_pixmap->toImage().convertToFormat(QImage::Format_RGBA8888);
            if (image.isNull())
            {
                all_children_cached = false;
                break;
            }

            child_images[dx][dy] = image;
            child_w = qMax(child_w, image.width());
            child_h = qMax(child_h, image.height());
        }
    }

    if (all_children_cached && child_w > 0 && child_h > 0)
    {
        QImage composite(child_w * 2, child_h * 2, QImage::Format_RGBA8888);
        QPainter painter(&composite);
        for (int dx = 0; dx < 2; ++dx)
        {
            for (int dy = 0; dy < 2; ++dy)
            {
                // Tile addressing here is the same XYZ scheme used
                // throughout (Y increasing southward, matching image row
                // order), so child (dx, dy) maps directly onto quadrant
                // (dx, dy) of the composite with no flip.
                painter.drawImage(
                    QRect(dx * child_w, dy * child_h, child_w, child_h), child_images[dx][dy]);
            }
        }
        painter.end();

        invalidateTileBindings(resource);
        if (!recreateTileTexture(resource, composite.size()))
        {
            return false;
        }
        if (!this->surface_backend.uploadTileTextureImage(
                resource_updates, resource->gpu_resource_id, composite))
        {
            return false;
        }
        resource->pixmap_cache_key = -1;
        resource->is_provisional = true;
        resource->provisional_source_key = children_key;
        ++resource->content_revision;
        if (resource->content_revision == 0)
            resource->content_revision = 1;
        if (updated_image != nullptr)
            *updated_image = composite;

        if (!tileBindingsReady(resource) && !rebuildTileBindings(resource))
            return false;

        tile.resource = resource;
        return true;
    }

    for (int levels_up = 1; tile.zoom - levels_up >= 0; ++levels_up)
    {
        const int ancestor_zoom = tile.zoom - levels_up;
        const quint32 ancestor_x = quint32(tile.tile_x) >> levels_up;
        const quint32 ancestor_y = quint32(tile.tile_y) >> levels_up;
        const QString ancestor_key = this->map_model->tileCacheKeyAtZoom(
            int(ancestor_x), int(ancestor_y), ancestor_zoom);
        const QPixmap *ancestor_pixmap = this->tile_repository->tile(ancestor_key);
        if (ancestor_pixmap == nullptr || ancestor_pixmap->isNull())
            continue;

        // Already showing exactly this ancestor from a previous frame --
        // nothing to re-derive.
        if (resource->is_provisional && tileTextureReady(resource)
            && resource->provisional_source_key == ancestor_key)
        {
            tile.resource = resource;
            return true;
        }

        const int span = 1 << levels_up;
        const int local_x = tile.tile_x & (span - 1);
        const int local_y = tile.tile_y & (span - 1);
        const int source_w = ancestor_pixmap->width();
        const int source_h = ancestor_pixmap->height();
        const QRect crop_rect(
            local_x * source_w / span, local_y * source_h / span,
            qMax(1, source_w / span), qMax(1, source_h / span));
        const QImage fallback_image = ancestor_pixmap->copy(crop_rect)
            .toImage().convertToFormat(QImage::Format_RGBA8888)
            .scaled(source_w, source_h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (fallback_image.isNull())
            continue;

        invalidateTileBindings(resource);
        if (!recreateTileTexture(resource, fallback_image.size()))
        {
            return false;
        }
        if (!this->surface_backend.uploadTileTextureImage(
                resource_updates, resource->gpu_resource_id, fallback_image))
        {
            return false;
        }
        resource->pixmap_cache_key = -1;
        resource->is_provisional = true;
        resource->provisional_source_key = ancestor_key;
        ++resource->content_revision;
        if (resource->content_revision == 0)
            resource->content_revision = 1;
        if (updated_image != nullptr)
            *updated_image = fallback_image;

        if (!tileBindingsReady(resource) && !rebuildTileBindings(resource))
            return false;

        tile.resource = resource;
        return true;
    }

    // Neither direct children nor any ancestor are loaded yet (e.g. the
    // very first tiles requested right after startup, or a fresh area with
    // nothing cached at any nearby zoom) -- nothing to derive a placeholder
    // from. Leaving tile.resource untouched here falls through to
    // template_bindings (the flat GlobeMissingTileColor fill) at draw
    // time, exactly as before this fallback existed.
    return true;
}

QImage MapRhiGlobeRenderer::renderHeatmapTileProfiled(
    const GlobeTile &tile, TileResource *resource)
{
    if (!this->heatmap_profile.enabled)
        return renderHeatmapTile(tile, resource, nullptr);

    QElapsedTimer timer;
    timer.start();
    HeatmapRasterStats stats;
    QVector<HeatmapStamp> diagnostic_stamps;
    QImage diagnostic_cpu_reference;
    QVector<HeatmapStamp> *rendered_stamps = nullptr;
    QImage *premultiplied_image = nullptr;
    if (!this->diagnostic_heatmap_gpu_validation_attempted)
    {
        rendered_stamps = &diagnostic_stamps;
        premultiplied_image = &diagnostic_cpu_reference;
    }
    QImage image = renderHeatmapTile(
        tile, resource, &stats, rendered_stamps, premultiplied_image);
    ++this->heatmap_profile.raster_calls;
    if (!image.isNull())
        ++this->heatmap_profile.raster_tiles_with_content;
    this->heatmap_profile.candidate_markers += stats.candidate_markers;
    this->heatmap_profile.candidate_bucket_cells +=
        stats.candidate_bucket_cells;
    this->heatmap_profile.marker_tile_pairs += stats.marker_tile_pairs;
    if (stats.stamp_layout_cache_hit)
        ++this->heatmap_profile.stamp_layout_cache_hits;
    else
        ++this->heatmap_profile.stamp_layout_cache_misses;
    this->heatmap_profile.raster_ns += timer.nsecsElapsed();
    if (!diagnostic_stamps.isEmpty())
    {
        scheduleDiagnosticHeatmapGpuBake(
            tile, diagnostic_stamps, diagnostic_cpu_reference);
    }
    return image;
}

QImage MapRhiGlobeRenderer::renderHeatmapTile(
    const GlobeTile &tile, TileResource *resource,
    HeatmapRasterStats *stats,
    QVector<HeatmapStamp> *rendered_stamps,
    QImage *premultiplied_image) const
{
    if (premultiplied_image != nullptr)
        *premultiplied_image = QImage();
    const QVector<HeatmapStamp> stamps = heatmapStampsForTile(
        tile, resource, stats);
    if (rendered_stamps != nullptr)
        *rendered_stamps = stamps;
    if (stamps.isEmpty())
        return QImage();

    const QImage image = renderHeatmapStamps(stamps);
    if (premultiplied_image != nullptr)
        *premultiplied_image = image;

    return image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
}

QImage MapRhiGlobeRenderer::renderHeatmapStamps(
    const QVector<HeatmapStamp> &stamps) const
{
    return this->heatmap_scene.renderStamps(stamps);
}

QVector<MapRhiGlobeRenderer::HeatmapStamp>
MapRhiGlobeRenderer::heatmapStampsForTileProfiled(
    const GlobeTile &tile, TileResource *resource)
{
    QElapsedTimer timer;
    timer.start();
    HeatmapRasterStats stats;
    QVector<HeatmapStamp> stamps = heatmapStampsForTile(
        tile, resource, &stats);
    this->heatmap_profile.candidate_markers += stats.candidate_markers;
    this->heatmap_profile.candidate_bucket_cells +=
        stats.candidate_bucket_cells;
    this->heatmap_profile.marker_tile_pairs += stats.marker_tile_pairs;
    if (stats.stamp_layout_cache_hit)
        ++this->heatmap_profile.stamp_layout_cache_hits;
    else
        ++this->heatmap_profile.stamp_layout_cache_misses;
    this->heatmap_profile.stamp_ns += timer.nsecsElapsed();
    if (!stamps.isEmpty())
        ++this->heatmap_profile.raster_tiles_with_content;
    return stamps;
}

QVector<MapRhiGlobeRenderer::HeatmapStamp>
MapRhiGlobeRenderer::heatmapStampsForTile(
    const GlobeTile &tile, TileResource *resource,
    HeatmapRasterStats *stats) const
{
    MapGlobeHeatmapTile heatmap_tile;
    heatmap_tile.zoom = tile.zoom;
    heatmap_tile.virtual_x = tile.virtual_x;
    heatmap_tile.tile_y = tile.tile_y;
    heatmap_tile.is_cap = tile.is_cap;
    MapGlobeHeatmapTileLayoutCache *layout_cache = resource != nullptr
        ? &resource->heatmap_layout_cache : nullptr;
    return this->heatmap_scene.stampsForTile(
        heatmap_tile, layout_cache, stats);
}

bool MapRhiGlobeRenderer::queueHeatmapGpuBake(
    TileResource *resource,
    MapRhiGlobeHeatmapBakeDestination destination,
    const QVector<HeatmapStamp> &stamps,
    int heatmap_array_page, int destination_layer)
{
    if (this->heatmap_gpu_baking_disabled || !this->surface_backend.hasContext()
        || resource == nullptr
        || destination_layer < 0
        || destination_layer >= GlobeTileArrayLayerCount
        || stamps.isEmpty())
    {
        return false;
    }

    if (destination == MapRhiGlobeHeatmapBakeDestination::TileHeatmapTexture)
    {
        if (!tileHeatmapTextureReady(resource))
            return false;
    }
    else if (heatmap_array_page < 0
             || !this->surface_backend.heatmapArrayPageReady(
                 heatmap_array_page))
    {
        return false;
    }

    HeatmapGpuBakeJob job;
    job.resource = resource;
    job.destination = destination;
    job.tile_gpu_resource_id = resource->gpu_resource_id;
    job.heatmap_array_page = heatmap_array_page;
    job.destination_layer = destination_layer;
    job.revision = this->heatmap_scene.revision();
    job.instances.reserve(stamps.size());
    // The bake shader writes raw clip-space positions and deliberately does
    // not use QRhi::clipSpaceCorrMatrix(). Therefore the input Y correction
    // must follow the backend's NDC convention, not its framebuffer-origin
    // convention.
    const bool flip_for_ndc = !this->surface_backend.isYUpInNdc();
    for (const HeatmapStamp &stamp : stamps)
    {
        HeatmapBakeInstance instance;
        instance.center_x_pixels = float(stamp.center_x_pixels);
        instance.center_y_pixels = float(
            flip_for_ndc
                ? double(GlobeHeatmapTextureSize) - stamp.center_y_pixels
                : stamp.center_y_pixels);
        instance.radius_pixels = float(stamp.radius_pixels);
        instance.solid_fraction = float(this->heatmap_scene.solidFraction());
        instance.red = stamp.color.redF();
        instance.green = stamp.color.greenF();
        instance.blue = stamp.color.blueF();
        job.instances.append(instance);
    }
    this->heatmap_gpu_bake_jobs.append(std::move(job));
    return true;
}

void MapRhiGlobeRenderer::disableHeatmapGpuBaking()
{
    this->preparation_dirty = true;
    for (const HeatmapGpuBakeJob &job : this->heatmap_gpu_bake_jobs)
    {
        if (job.resource == nullptr)
            continue;
        job.resource->heatmap_revision = 0;
        if (job.destination_layer > 0)
            job.resource->heatmap_array_revision = 0;
        else
            job.resource->heatmap_texture_revision = 0;
    }
    this->heatmap_gpu_bake_jobs.clear();
    if (!this->heatmap_gpu_baking_disabled)
    {
        qCWarning(globeHeatmapPerformanceLog)
            << "Visible Globe heatmap GPU baking failed; falling back to CPU rasterization.";
    }
    this->heatmap_gpu_baking_disabled = true;
}

void MapRhiGlobeRenderer::scheduleDiagnosticHeatmapGpuBake(
    const GlobeTile &tile, const QVector<HeatmapStamp> &stamps,
    const QImage &premultiplied_image)
{
    if (!this->heatmap_profile.enabled || stamps.isEmpty()
        || premultiplied_image.isNull()
        || this->diagnostic_heatmap_gpu_validation_attempted)
    {
        return;
    }

    this->diagnostic_heatmap_bake_instances.clear();
    this->diagnostic_heatmap_bake_instances.reserve(stamps.size());
    const bool flip_for_ndc = this->surface_backend.hasContext()
        && !this->surface_backend.isYUpInNdc();
    for (const HeatmapStamp &stamp : stamps)
    {
        HeatmapBakeInstance instance;
        instance.center_x_pixels = float(stamp.center_x_pixels);
        instance.center_y_pixels = float(
            flip_for_ndc
                ? double(GlobeHeatmapTextureSize) - stamp.center_y_pixels
                : stamp.center_y_pixels);
        instance.radius_pixels = float(stamp.radius_pixels);
        instance.solid_fraction = float(this->heatmap_scene.solidFraction());
        instance.red = stamp.color.redF();
        instance.green = stamp.color.greenF();
        instance.blue = stamp.color.blueF();
        this->diagnostic_heatmap_bake_instances.append(instance);
    }

    this->diagnostic_heatmap_bake_revision = this->heatmap_scene.revision();
    this->diagnostic_heatmap_gpu_validation_attempted = true;
    this->diagnostic_heatmap_bake_zoom = tile.zoom;
    this->diagnostic_heatmap_bake_tile_x = tile.virtual_x;
    this->diagnostic_heatmap_bake_tile_y = tile.tile_y;
    this->diagnostic_heatmap_bake_cpu_reference = premultiplied_image;
    this->diagnostic_heatmap_bake_pending = true;
}

void MapRhiGlobeRenderer::scheduleDiagnosticHeatmapGpuSelfTest()
{
    if (!this->heatmap_profile.enabled
        || this->diagnostic_heatmap_gpu_validation_attempted)
    {
        return;
    }

    // This deliberately asymmetric pattern validates Y orientation, clipped
    // stamp quads, radial falloff, color channels, and ordered source-over
    // overlap without depending on the current camera or visible markers.
    QVector<HeatmapStamp> stamps;
    stamps.reserve(4);

    HeatmapStamp stamp;
    stamp.center_x_pixels = 48.25;
    stamp.center_y_pixels = 57.5;
    stamp.radius_pixels = 40.75;
    stamp.color = QColor(239, 74, 62);
    stamps.append(stamp);

    stamp.center_x_pixels = 122.5;
    stamp.center_y_pixels = 91.25;
    stamp.radius_pixels = 63.5;
    stamp.color = QColor(54, 198, 121);
    stamps.append(stamp);

    stamp.center_x_pixels = 182.25;
    stamp.center_y_pixels = 169.75;
    stamp.radius_pixels = 51.25;
    stamp.color = QColor(68, 112, 242);
    stamps.append(stamp);

    stamp.center_x_pixels = 251.0;
    stamp.center_y_pixels = 224.5;
    stamp.radius_pixels = 49.0;
    stamp.color = QColor(231, 174, 48);
    stamps.append(stamp);

    GlobeTile self_test_tile;
    self_test_tile.zoom = -1;
    scheduleDiagnosticHeatmapGpuBake(
        self_test_tile, stamps, renderHeatmapStamps(stamps));
}

void MapRhiGlobeRenderer::releaseDiagnosticHeatmapGpuBakeResources()
{
    this->surface_backend.releaseDiagnosticHeatmapBakeResources();
}

void MapRhiGlobeRenderer::releaseVisibleHeatmapGpuBakeAtlasResources()
{
    this->surface_backend.releaseVisibleHeatmapBakeAtlasResources();
}

bool MapRhiGlobeRenderer::ensureDiagnosticHeatmapGpuBakeResources()
{
    return this->surface_backend.ensureHeatmapBakeResources(
        GlobeHeatmapTextureSize);
}

bool MapRhiGlobeRenderer::runPendingHeatmapGpuBakes(
    QRhiCommandBuffer *command_buffer)
{
    if (this->heatmap_gpu_bake_jobs.isEmpty())
        return true;

    QVector<MapRhiGlobeHeatmapBakeJob> backend_jobs;
    backend_jobs.reserve(this->heatmap_gpu_bake_jobs.size());
    for (const HeatmapGpuBakeJob &job : this->heatmap_gpu_bake_jobs)
    {
        if (job.resource == nullptr
            || job.destination_layer < 0
            || job.destination_layer >= GlobeTileArrayLayerCount
            || job.revision != this->heatmap_scene.revision()
            || job.instances.isEmpty())
        {
            disableHeatmapGpuBaking();
            return false;
        }

        MapRhiGlobeHeatmapBakeJob backend_job;
        backend_job.destination = job.destination;
        backend_job.tile_gpu_resource_id = job.tile_gpu_resource_id;
        backend_job.heatmap_array_page = job.heatmap_array_page;
        backend_job.destination_layer = job.destination_layer;
        backend_job.instances = &job.instances;
        backend_jobs.append(backend_job);
    }

    MapRhiGlobeHeatmapBakeExecutionStats execution_stats;
    if (!this->surface_backend.recordVisibleHeatmapBakes(
            command_buffer, backend_jobs,
            GlobeHeatmapTextureSize, GlobeTileArrayLayerCount,
            GlobeHeatmapGpuBakeAtlasMaximumSlots, &execution_stats))
    {
        disableHeatmapGpuBaking();
        return false;
    }

    for (const HeatmapGpuBakeJob &job : this->heatmap_gpu_bake_jobs)
    {
        if (job.destination_layer > 0)
            job.resource->heatmap_array_revision = job.revision;
        else
            job.resource->heatmap_texture_revision = job.revision;
        job.resource->heatmap_revision = job.revision;
    }

    this->heatmap_profile.gpu_visible_bake_passes +=
        execution_stats.recorded_passes;
    this->heatmap_profile.gpu_visible_bake_stamps +=
        execution_stats.recorded_stamps;
    this->heatmap_profile.gpu_visible_copies +=
        execution_stats.recorded_tiles;
    this->heatmap_profile.gpu_visible_copy_batches +=
        execution_stats.recorded_copy_batches;
    this->heatmap_profile.gpu_visible_array_copies +=
        execution_stats.recorded_array_copies;

    qCDebug(globeHeatmapPerformanceLog).noquote().nospace()
        << "visible_gpu_bakes revision=" << this->heatmap_scene.revision()
        << " tiles=" << execution_stats.recorded_tiles
        << " passes=" << execution_stats.recorded_passes
        << " stamps=" << execution_stats.recorded_stamps
        << " copies=" << execution_stats.recorded_tiles
        << " array_copies=" << execution_stats.recorded_array_copies
        << " copy_batches=" << execution_stats.recorded_copy_batches
        << " atlas_slots_max=" << execution_stats.maximum_atlas_slots
        << " array_config="
        << (guiConfiguration().map_performance.array_batching_enabled ? 1 : 0)
        << " texture_arrays="
        << (this->surface_backend.hasContext()
                && this->surface_backend.supportsTextureArrays()
            ? 1 : 0)
        << " imagery_array_pipeline="
        << (this->surface_backend.imageryArrayPipelineReady() ? 1 : 0)
        << " imagery_array_pages="
        << this->surface_backend.imageryArrayPages().size()
        << " imagery_array_active=" << (arrayBatchingActive() ? 1 : 0)
        << " status=recorded";
    this->heatmap_gpu_bake_jobs.clear();
    return true;
}

void MapRhiGlobeRenderer::runDiagnosticHeatmapGpuBake(
    QRhiCommandBuffer *command_buffer)
{
    if (!this->diagnostic_heatmap_bake_pending
        && !this->heatmap_profile_report_pending)
    {
        return;
    }

    QString failure;
    if (this->diagnostic_heatmap_bake_pending
        && this->heatmap_profile.enabled)
    {
        const bool bake_recorded =
            this->surface_backend.recordDiagnosticHeatmapBake(
                command_buffer,
                this->diagnostic_heatmap_bake_instances,
                GlobeHeatmapTextureSize, &failure);
        if (bake_recorded)
        {
            if (this->diagnostic_heatmap_bake_cpu_reference.isNull())
            {
                qCDebug(globeHeatmapPerformanceLog)
                    .noquote().nospace()
                    << "diagnostic_gpu_validation revision="
                    << this->diagnostic_heatmap_bake_revision
                    << " tile="
                    << this->diagnostic_heatmap_bake_zoom << "/"
                    << this->diagnostic_heatmap_bake_tile_x << "/"
                    << this->diagnostic_heatmap_bake_tile_y
                    << " status=skipped reason="
                       "missing_cpu_reference";
            }
            else
            {
                QRhiReadbackResult *readback_result =
                    new QRhiReadbackResult{};
                const QImage cpu_reference =
                    this->diagnostic_heatmap_bake_cpu_reference;
                const quint64 revision =
                    this->diagnostic_heatmap_bake_revision;
                const int zoom = this->diagnostic_heatmap_bake_zoom;
                const int tile_x = this->diagnostic_heatmap_bake_tile_x;
                const int tile_y = this->diagnostic_heatmap_bake_tile_y;
                const int stamp_count = int(
                    this->diagnostic_heatmap_bake_instances.size());
                readback_result->completed = [
                    readback_result, cpu_reference, revision, zoom,
                    tile_x, tile_y, stamp_count]()
                {
                    reportGlobeHeatmapGpuValidation(
                        *readback_result, cpu_reference, revision,
                        zoom, tile_x, tile_y, stamp_count);
                    delete readback_result;
                };

                if (this->surface_backend.queueDiagnosticHeatmapReadback(
                        command_buffer, readback_result))
                {
                    ++this->heatmap_profile.gpu_validation_readbacks;
                }
                else
                {
                    delete readback_result;
                    qCDebug(globeHeatmapPerformanceLog)
                        .noquote().nospace()
                        << "diagnostic_gpu_validation revision="
                        << this->diagnostic_heatmap_bake_revision
                        << " tile="
                        << this->diagnostic_heatmap_bake_zoom << "/"
                        << this->diagnostic_heatmap_bake_tile_x << "/"
                        << this->diagnostic_heatmap_bake_tile_y
                        << " status=skipped reason="
                           "update_batch_unavailable";
                }
            }

            ++this->heatmap_profile.gpu_bake_passes;
            this->heatmap_profile.gpu_bake_stamps +=
                int(this->diagnostic_heatmap_bake_instances.size());
            qCDebug(globeHeatmapPerformanceLog).noquote().nospace()
                << "diagnostic_gpu_bake revision="
                << this->diagnostic_heatmap_bake_revision
                << " tile=" << this->diagnostic_heatmap_bake_zoom
                << "/" << this->diagnostic_heatmap_bake_tile_x
                << "/" << this->diagnostic_heatmap_bake_tile_y
                << " stamps="
                << this->diagnostic_heatmap_bake_instances.size()
                << " target=" << GlobeHeatmapTextureSize << "x"
                << GlobeHeatmapTextureSize
                << " status=recorded";
        }

        if (!failure.isEmpty())
        {
            qCDebug(globeHeatmapPerformanceLog).noquote().nospace()
                << "diagnostic_gpu_bake revision="
                << this->diagnostic_heatmap_bake_revision
                << " tile=" << this->diagnostic_heatmap_bake_zoom
                << "/" << this->diagnostic_heatmap_bake_tile_x
                << "/" << this->diagnostic_heatmap_bake_tile_y
                << " stamps="
                << this->diagnostic_heatmap_bake_instances.size()
                << " status=" << failure;
        }
    }

    this->diagnostic_heatmap_bake_instances.clear();
    this->diagnostic_heatmap_bake_cpu_reference = QImage();
    this->diagnostic_heatmap_bake_pending = false;
    this->diagnostic_heatmap_bake_revision = 0;
    if (this->heatmap_profile_report_pending)
    {
        reportHeatmapProfile();
        this->heatmap_profile_report_pending = false;
    }
}

bool MapRhiGlobeRenderer::ensureHeatmapTexture(
    const GlobeTile &tile, TileResource *resource,
    QRhiResourceUpdateBatch *resource_updates, QImage *updated_image,
    QVector<HeatmapStamp> *updated_stamps,
    bool upload_fallback_texture)
{
    ScopedHeatmapProfileTimer profile_timer(
        &this->heatmap_profile.cpu_ns, this->heatmap_profile.enabled);
    if (updated_image != nullptr)
        *updated_image = QImage();
    if (updated_stamps != nullptr)
        updated_stamps->clear();
    if (resource == nullptr || resource_updates == nullptr)
        return false;
    // Cheap common case: this tile's heatmap state already reflects the
    // current revision, so there is no CPU raster to regenerate.
    if (resource->heatmap_revision == this->heatmap_scene.revision())
        return tileBindingsReady(resource) || rebuildTileBindings(resource);

    if (this->heatmap_profile.enabled)
        ++this->heatmap_profile.dirty_tiles;

    // Both visible destinations consume the same small ordered stamp list:
    // the ordinary fallback texture can be queued immediately, while the
    // array destination is assigned by ensureTileHeatmapArray() later in
    // this prepare(). No pixels are rasterized or uploaded by the CPU on
    // either successful GPU path.
    if (!this->heatmap_gpu_baking_disabled)
    {
        QVector<HeatmapStamp> local_stamps;
        QVector<HeatmapStamp> *stamps = updated_stamps != nullptr
            ? updated_stamps : &local_stamps;
        *stamps = heatmapStampsForTileProfiled(tile, resource);
        resource->heatmap_has_content = !stamps->isEmpty();
        bool bindings_changed = false;
        if (stamps->isEmpty())
        {
            if (tileHeatmapTextureReady(resource))
            {
                invalidateTileBindings(resource);
                clearTileHeatmapTexture(resource);
                bindings_changed = true;
            }
            resource->heatmap_texture_revision = this->heatmap_scene.revision();
            resource->heatmap_revision = this->heatmap_scene.revision();
            if (bindings_changed || !tileBindingsReady(resource))
                return rebuildTileBindings(resource);
            return true;
        }

        if (!upload_fallback_texture)
            return tileBindingsReady(resource)
                || rebuildTileBindings(resource);

        if (!ensureDiagnosticHeatmapGpuBakeResources())
        {
            disableHeatmapGpuBaking();
        }
        else
        {
            if (!tileHeatmapTextureReady(resource))
            {
                invalidateTileBindings(resource);
                if (!recreateTileHeatmapTexture(resource, QSize(GlobeHeatmapTextureSize,
                              GlobeHeatmapTextureSize)))
                {
                    disableHeatmapGpuBaking();
                }
                else
                {
                    bindings_changed = true;
                }
            }

            if (!this->heatmap_gpu_baking_disabled
                && queueHeatmapGpuBake(
                    resource,
                    MapRhiGlobeHeatmapBakeDestination::TileHeatmapTexture,
                    *stamps))
            {
                // Treat the sampled texture as current for the fallback
                // check later in this prepare(), but do not commit the
                // tile's logical revision until the copy is recorded.
                resource->heatmap_texture_revision =
                    this->heatmap_scene.revision();
                if (bindings_changed || !tileBindingsReady(resource))
                    return rebuildTileBindings(resource);
                return true;
            }
        }
    }

    QImage image = renderHeatmapTileProfiled(tile, resource);
    resource->heatmap_has_content = !image.isNull();
    bool bindings_changed = false;
    if (!image.isNull() && upload_fallback_texture)
    {
        if (!tileHeatmapTextureReady(resource))
        {
            if (!recreateTileHeatmapTexture(resource, image.size()))
            {
                return false;
            }
            bindings_changed = true;
        }
        if (!this->surface_backend.uploadTileHeatmapTextureImage(
                resource_updates, resource->gpu_resource_id, image))
        {
            return false;
        }
        if (this->heatmap_profile.enabled)
        {
            ++this->heatmap_profile.fallback_uploads;
            this->heatmap_profile.upload_bytes += quint64(image.sizeInBytes());
        }
        resource->heatmap_texture_revision = this->heatmap_scene.revision();
    }
    else if (image.isNull() && tileHeatmapTextureReady(resource))
    {
        // A now-empty tile can bind the shared transparent dummy. Destroying
        // its old private texture avoids a clear upload and releases memory.
        invalidateTileBindings(resource);
        clearTileHeatmapTexture(resource);
        resource->heatmap_texture_revision = this->heatmap_scene.revision();
        bindings_changed = true;
    }

    if (updated_image != nullptr)
        *updated_image = image;

    resource->heatmap_revision = this->heatmap_scene.revision();
    if (bindings_changed || !tileBindingsReady(resource))
        return rebuildTileBindings(resource);
    return true;
}

bool MapRhiGlobeRenderer::ensureHeatmapFallbackTexture(
    const GlobeTile &tile, TileResource *resource,
    const QImage &updated_image,
    const QVector<HeatmapStamp> &updated_stamps,
    QRhiResourceUpdateBatch *resource_updates)
{
    ScopedHeatmapProfileTimer profile_timer(
        &this->heatmap_profile.cpu_ns, this->heatmap_profile.enabled);
    if (resource == nullptr || resource_updates == nullptr
        || !resource->heatmap_has_content)
    {
        return true;
    }
    if (tileHeatmapTextureReady(resource)
        && resource->heatmap_texture_revision == this->heatmap_scene.revision())
    {
        return tileBindingsReady(resource) || rebuildTileBindings(resource);
    }

    if (!this->heatmap_gpu_baking_disabled)
    {
        QVector<HeatmapStamp> regenerated_stamps;
        const QVector<HeatmapStamp> *stamps = &updated_stamps;
        if (stamps->isEmpty())
        {
            regenerated_stamps = heatmapStampsForTileProfiled(
                tile, resource);
            stamps = &regenerated_stamps;
        }

        if (stamps->isEmpty())
        {
            const bool bindings_changed =
                tileHeatmapTextureReady(resource);
            if (bindings_changed)
            {
                invalidateTileBindings(resource);
                clearTileHeatmapTexture(resource);
            }
            resource->heatmap_has_content = false;
            resource->heatmap_texture_revision = this->heatmap_scene.revision();
            resource->heatmap_revision = this->heatmap_scene.revision();
            if (bindings_changed || !tileBindingsReady(resource))
                return rebuildTileBindings(resource);
            return true;
        }

        if (ensureDiagnosticHeatmapGpuBakeResources())
        {
            bool bindings_changed = false;
            if (!tileHeatmapTextureReady(resource))
            {
                invalidateTileBindings(resource);
                if (recreateTileHeatmapTexture(resource, QSize(GlobeHeatmapTextureSize,
                              GlobeHeatmapTextureSize)))
                {
                    bindings_changed = true;
                }
            }

            if (tileHeatmapTextureReady(resource)
                && queueHeatmapGpuBake(
                    resource,
                    MapRhiGlobeHeatmapBakeDestination::TileHeatmapTexture,
                    *stamps))
            {
                resource->heatmap_texture_revision =
                    this->heatmap_scene.revision();
                if (bindings_changed || !tileBindingsReady(resource))
                    return rebuildTileBindings(resource);
                return true;
            }
        }

        disableHeatmapGpuBaking();
    }

    // Automatic backend/resource failure fallback. This is the only
    // remaining visible path that rasterizes heatmap pixels on the CPU.
    QImage image = updated_image;
    if (image.isNull())
        image = renderHeatmapTileProfiled(tile, resource);
    if (image.isNull())
        return false;

    bool bindings_changed = false;
    if (!tileHeatmapTextureReady(resource))
    {
        if (!recreateTileHeatmapTexture(resource, image.size()))
        {
            return false;
        }
        bindings_changed = true;
    }
    if (!this->surface_backend.uploadTileHeatmapTextureImage(
            resource_updates, resource->gpu_resource_id, image))
    {
        return false;
    }
    if (this->heatmap_profile.enabled)
    {
        ++this->heatmap_profile.fallback_uploads;
        this->heatmap_profile.upload_bytes += quint64(image.sizeInBytes());
    }
    resource->heatmap_texture_revision = this->heatmap_scene.revision();
    resource->heatmap_revision = this->heatmap_scene.revision();
    if (bindings_changed || !tileBindingsReady(resource))
        return rebuildTileBindings(resource);
    return true;
}

bool MapRhiGlobeRenderer::requestMissingTiles(QRhiResourceUpdateBatch *resource_updates)
{
    // A normal frame consumes this queue before the next prepare(). If a
    // preceding frame aborted later in preparation, invalidate its queued
    // texture revisions so the same tiles are queued again instead of
    // sampling data that was never recorded.
    for (const HeatmapGpuBakeJob &job : this->heatmap_gpu_bake_jobs)
    {
        if (job.resource == nullptr)
            continue;
        job.resource->heatmap_revision = 0;
        if (job.destination_layer > 0)
            job.resource->heatmap_array_revision = 0;
        else
            job.resource->heatmap_texture_revision = 0;
    }
    this->heatmap_gpu_bake_jobs.clear();

    this->heatmap_profile = HeatmapProfileCounters();
    this->heatmap_profile.enabled =
        globeHeatmapPerformanceLog().isDebugEnabled();
    this->heatmap_profile.visible_tiles = this->window_tiles.size();
    this->heatmap_profile_report_pending = this->heatmap_profile.enabled;
    scheduleDiagnosticHeatmapGpuSelfTest();

    if (!this->surface_backend.uploadPendingMissingTileTexture(
            resource_updates, GlobeMissingTileColor))
    {
        return false;
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
    {
        QImage updated_image;
        QImage updated_heatmap_image;
        QVector<HeatmapStamp> updated_heatmap_stamps;
        if (!ensureTileResource(
                tile, resource_updates, &updated_image,
                &updated_heatmap_image, &updated_heatmap_stamps))
            return false;
        if (!ensureTileArrayLayer(tile, updated_image, resource_updates))
            return false;
        if (!ensureTileHeatmapArray(
                tile, updated_heatmap_image, updated_heatmap_stamps,
                resource_updates))
        {
            return false;
        }
        if (!tile.array_ready && tile.resource != nullptr
            && !ensureHeatmapFallbackTexture(
                tile, tile.resource, updated_heatmap_image,
                updated_heatmap_stamps,
                resource_updates))
        {
            return false;
        }
    }
    for (GlobeTile &tile : this->cap_tiles)
    {
        if (!ensureTileResource(tile, resource_updates))
            return false;
    }
    trimUnusedHeatmapArrayPages();
    return true;
}

void MapRhiGlobeRenderer::reportHeatmapProfile() const
{
    if (!this->heatmap_profile.enabled)
        return;
    if (this->heatmap_profile.dirty_tiles <= 0
        && this->heatmap_profile.raster_calls <= 0
        && this->heatmap_profile.fallback_uploads <= 0
        && this->heatmap_profile.array_uploads <= 0
        && this->heatmap_profile.gpu_visible_bake_passes <= 0
        && this->heatmap_profile.gpu_bake_passes <= 0
        && this->heatmap_profile.gpu_validation_readbacks <= 0)
    {
        return;
    }

    constexpr double NsecsPerMillisecond = 1000000.0;
    constexpr double BytesPerMebibyte = 1024.0 * 1024.0;
    qCDebug(globeHeatmapPerformanceLog).noquote().nospace()
        << "revision=" << this->heatmap_scene.revision()
        << " markers=" << this->heatmap_scene.markerCount()
        << " active_markers=" << this->heatmap_scene.activeMarkerCount()
        << " stamp_layout_revision="
        << this->heatmap_scene.layoutRevision()
        << " visible_tiles=" << this->heatmap_profile.visible_tiles
        << " dirty_tiles=" << this->heatmap_profile.dirty_tiles
        << " raster_calls=" << this->heatmap_profile.raster_calls
        << " content_tiles="
        << this->heatmap_profile.raster_tiles_with_content
        << " candidate_markers="
        << this->heatmap_profile.candidate_markers
        << " candidate_bucket_cells="
        << this->heatmap_profile.candidate_bucket_cells
        << " marker_tile_pairs="
        << this->heatmap_profile.marker_tile_pairs
        << " radius_m="
        << QString::number(this->heatmap_scene.radiusM(), 'f', 3)
        << " marker_index_levels="
        << this->heatmap_scene.bucketLevelCount()
        << " stamp_layout_cache_hits="
        << this->heatmap_profile.stamp_layout_cache_hits
        << " stamp_layout_cache_misses="
        << this->heatmap_profile.stamp_layout_cache_misses
        << " stamp_ms="
        << QString::number(
               double(this->heatmap_profile.stamp_ns)
                   / NsecsPerMillisecond,
               'f', 3)
        << " raster_ms="
        << QString::number(
               double(this->heatmap_profile.raster_ns)
                   / NsecsPerMillisecond,
               'f', 3)
        << " cpu_ms="
        << QString::number(
               double(this->heatmap_profile.cpu_ns)
                   / NsecsPerMillisecond,
               'f', 3)
        << " fallback_uploads="
        << this->heatmap_profile.fallback_uploads
        << " array_uploads=" << this->heatmap_profile.array_uploads
        << " upload_mib="
        << QString::number(
               double(this->heatmap_profile.upload_bytes)
                   / BytesPerMebibyte,
               'f', 3)
        << " gpu_visible_bake_passes="
        << this->heatmap_profile.gpu_visible_bake_passes
        << " gpu_visible_bake_stamps="
        << this->heatmap_profile.gpu_visible_bake_stamps
        << " gpu_visible_copies="
        << this->heatmap_profile.gpu_visible_copies
        << " gpu_visible_copy_batches="
        << this->heatmap_profile.gpu_visible_copy_batches
        << " gpu_visible_array_copies="
        << this->heatmap_profile.gpu_visible_array_copies
        << " gpu_bake_passes="
        << this->heatmap_profile.gpu_bake_passes
        << " gpu_bake_stamps="
        << this->heatmap_profile.gpu_bake_stamps
        << " gpu_validation_readbacks="
        << this->heatmap_profile.gpu_validation_readbacks
        << " array_config="
        << (guiConfiguration().map_performance.array_batching_enabled ? 1 : 0)
        << " texture_arrays="
        << (this->surface_backend.hasContext()
                && this->surface_backend.supportsTextureArrays()
            ? 1 : 0)
        << " imagery_array_pipeline="
        << (this->surface_backend.imageryArrayPipelineReady() ? 1 : 0)
        << " imagery_array_pages=" << this->surface_backend.imageryArrayPages().size()
        << " imagery_array_active=" << (arrayBatchingActive() ? 1 : 0)
        << " heatmap_array_pipeline="
        << (this->surface_backend.heatmapArrayPipelineReady() ? 1 : 0)
        << " heatmap_array_pages=" << this->surface_backend.heatmapArrayPages().size();
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
        request.globe_vertical_exaggeration =
            this->map_model->view3dVerticalExaggeration();
        request.globe_render_origin_x = this->render_origin_ecef.x;
        request.globe_render_origin_y = this->render_origin_ecef.y;
        request.globe_render_origin_z = this->render_origin_ecef.z;

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
            if (first_vertex < 0
                || first_vertex > this->window_vertices.size()
                || result.vertices.size()
                    > this->window_vertices.size() - first_vertex)
            {
                qWarning().noquote()
                    << QStringLiteral(
                           "Ignoring stale globe terrain mesh result outside "
                           "the current vertex window: request=%1 first=%2 "
                           "count=%3 window=%4")
                           .arg(result.request_id)
                           .arg(first_vertex)
                           .arg(result.vertices.size())
                           .arg(this->window_vertices.size());
                break;
            }

            for (qsizetype index = 0; index < result.vertices.size(); ++index)
            {
                const MapRhiTerrainMeshVertex &vertex = result.vertices.at(index);
                TileVertex &target = this->window_vertices[first_vertex + index];
                target.x = vertex.x;
                target.y = vertex.y;
                target.z = vertex.z;
                target.u = vertex.u;
                target.v = vertex.v;
            }

            tile.terrain_mesh_applied = true;
            tile.terrain_mesh_has_relief = true;
            updateTerrainRayBounds(&tile);
            wireframe_changed = true;

            if (!this->surface_backend.patchWindowVertices(
                    resource_updates, first_vertex, result.vertices.size(),
                    this->window_vertices))
            {
                this->surface_backend.invalidateWindowVertices();
            }

            break;
        }
    }

    if (wireframe_changed && this->wireframe_visible)
        rebuildWireframeVertices();
    return true;
}

bool MapRhiGlobeRenderer::createTileArrayPage()
{
    const bool created = this->surface_backend.createImageryArrayPage(
        QSize(MapModel::TileSize, MapModel::TileSize),
        GlobeTileArrayLayerCount, GlobeTileArrayMaximumPageCount);
    if (created)
    {
        this->tile_array_draw_indices_dirty = true;
        this->heatmap_array_draw_indices_dirty = true;
    }
    return created;
}

bool MapRhiGlobeRenderer::createTileArrayResources()
{
    if (this->map_model == nullptr
        || this->map_model->viewMode() != MapViewMode::Globe
        || !guiConfiguration().map_performance.array_batching_enabled
        || !this->surface_backend.hasContext())
    {
        return false;
    }

    if (this->surface_backend.imageryArrayPages().empty())
    {
        if (!this->surface_backend.supportsTextureArrays())
            return false;
        if (!createTileArrayPage())
            return false;
    }

    return this->surface_backend.ensureImageryArrayPipeline();
}

bool MapRhiGlobeRenderer::createHeatmapArrayPage(
    QRhiResourceUpdateBatch *resource_updates)
{
    const bool created = this->surface_backend.createHeatmapArrayPage(
        resource_updates,
        QSize(GlobeHeatmapTextureSize, GlobeHeatmapTextureSize),
        GlobeTileArrayLayerCount, GlobeTileArrayMaximumPageCount);
    if (created)
        this->heatmap_array_draw_indices_dirty = true;
    return created;
}

bool MapRhiGlobeRenderer::createHeatmapArrayResources(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (this->map_model == nullptr
        || this->map_model->viewMode() != MapViewMode::Globe
        || !guiConfiguration().map_performance.array_batching_enabled
        || !this->surface_backend.hasContext()
        || resource_updates == nullptr)
    {
        return false;
    }

    if (this->surface_backend.heatmapArrayPages().empty())
    {
        if (!this->surface_backend.supportsTextureArrays())
            return false;
        if (!createHeatmapArrayPage(resource_updates))
            return false;
    }

    return this->surface_backend.ensureHeatmapArrayPipeline();
}

bool MapRhiGlobeRenderer::ensureSharedResources()
{
    if (!this->surface_backend.hasContext())
        return false;

    if (!this->surface_backend.ensureSharedResources())
    {
        return false;
    }

    // Texture arrays are optional. Unsupported backends and allocation or
    // shader failures retain the already-created per-tile pipeline.
    createTileArrayResources();
    return true;
}

bool MapRhiGlobeRenderer::initialize(
    QRhi *rhi_instance, QRhiRenderPassDescriptor *render_pass_descriptor_instance,
    int sample_count_value)
{
    if (rhi_instance == nullptr || render_pass_descriptor_instance == nullptr)
        return false;

    const bool context_changed =
        !this->surface_backend.contextMatches(rhi_instance);
    const bool render_pass_changed =
        !this->surface_backend.renderPassMatches(
            render_pass_descriptor_instance, sample_count_value);

    if (context_changed)
    {
        // All QRhi resources belong to the QRhi that created them. This is
        // normally already handled by MapRhiWidget, but keeping the renderer
        // self-contained makes device/context recreation safe as well.
        releaseResources();
    }
    else if (render_pass_changed)
    {
        // Vulkan graphics pipelines are compatible with the render pass they
        // were created for. QRhiWidget may replace its render-pass descriptor
        // after resize/reinitialization, so every pipeline that draws into the
        // visible widget pass has to be recreated. Resource bindings, textures,
        // and buffers do not depend on that render pass and can be retained.
        this->surface_backend.invalidateRenderPassPipelines();
    }

    if (context_changed || render_pass_changed)
    {
        this->preparation_dirty = true;
        this->prepared_view_state_valid = false;
        this->full_prepare_clock.invalidate();
    }

    this->surface_backend.setContext(
        rhi_instance, render_pass_descriptor_instance, sample_count_value);

    buildCaps();
    return ensureSharedResources();
}

bool MapRhiGlobeRenderer::preparedViewSelectionMatches(
    const QSize &viewport_size) const
{
    if (!this->prepared_view_state_valid || this->map_model == nullptr)
        return false;

    return viewport_size == this->prepared_viewport_size
        && this->map_model->centerLon() == this->prepared_center_lon_deg
        && this->map_model->centerLat() == this->prepared_center_lat_deg
        && this->map_model->viewGlobeYawDeg() == this->prepared_yaw_deg
        && this->map_model->viewGlobePitchDeg() == this->prepared_pitch_deg
        && this->map_model->viewGlobeDistanceM() == this->prepared_distance_m;
}

bool MapRhiGlobeRenderer::canUseCameraOnlyPrepare(
    const QSize &viewport_size) const
{
    if (this->preparation_dirty || !this->prepared_view_state_valid
        || this->map_model == nullptr || this->window_dirty
        || !this->full_prepare_clock.isValid()
        || this->full_prepare_clock.elapsed()
            >= GlobeCameraOnlyMaximumReuseMs
        || this->terrain_lod_rebuild_pending
        || !this->heatmap_gpu_bake_jobs.isEmpty()
        || hasPendingTerrainMeshes())
    {
        return false;
    }

    // Height following deliberately omits vertical_offset and collision_lift
    // from this key. Every input that can alter tile selection, LOD, or the
    // geographic resource window must still match exactly.
    if (!preparedViewSelectionMatches(viewport_size))
        return false;

    // These flags should all be consumed by a successful full prepare. Keep
    // them as defensive guards so a partially initialized/recovered QRhi
    // resource can never enter the transform-only path.
    if (this->surface_backend.hasPendingGeometryUploads(
            this->prepared_surface_render_frame)
        || this->surface_backend.fallbackTextureUploadsPending()
        || this->tile_array_draw_indices_dirty
        || this->tile_array_draw_index_upload_pending
        || this->heatmap_array_draw_indices_dirty
        || this->heatmap_array_draw_index_upload_pending
        || (!this->heatmap_array_draw_indices.isEmpty()
            && this->heatmap_array_layer_upload_pending)
        || (this->map_visible && this->tile_repository != nullptr
            && !this->window_tiles_requested))
    {
        return false;
    }

    return true;
}

void MapRhiGlobeRenderer::rememberPreparedViewState(
    const QSize &viewport_size)
{
    this->prepared_viewport_size = viewport_size;
    this->prepared_center_lon_deg = this->map_model->centerLon();
    this->prepared_center_lat_deg = this->map_model->centerLat();
    this->prepared_yaw_deg = this->map_model->viewGlobeYawDeg();
    this->prepared_pitch_deg = this->map_model->viewGlobePitchDeg();
    this->prepared_distance_m = this->map_model->viewGlobeDistanceM();
    this->prepared_view_state_valid = true;
    this->full_prepare_clock.restart();
}

void MapRhiGlobeRenderer::refreshPreparedSurfaceRenderFrameState(
    const QSize &viewport_size)
{
    this->prepared_surface_render_frame.viewport_size = viewport_size;
    this->prepared_surface_render_frame.render_origin_ecef =
        this->render_origin_ecef;
    this->prepared_surface_render_frame.map_visible = this->map_visible;
    this->prepared_surface_render_frame.wireframe_visible =
        this->wireframe_visible;
    this->prepared_surface_render_frame.heatmap_opacity =
        this->heatmap_opacity;
    this->prepared_surface_render_frame.heatmap_revision =
        this->heatmap_scene.revision();
    this->prepared_surface_render_frame.heatmap_layout_revision =
        this->heatmap_scene.layoutRevision();
    this->prepared_surface_render_frame.active_heatmap_marker_count =
        this->heatmap_scene.activeMarkerCount();

    MapGlobeSurfaceRenderResources &resources =
        this->prepared_surface_render_frame.resources;
    resources.window_vertices = &this->window_vertices;
    resources.window_indices = &this->window_indices;
    resources.cap_vertices = &this->cap_vertices;
    resources.cap_indices = &this->cap_indices;
    resources.wireframe_vertices = &this->wireframe_vertices;
}

void MapRhiGlobeRenderer::rebuildPreparedSurfaceRenderFrame(
    const QSize &viewport_size)
{
    refreshPreparedSurfaceRenderFrameState(viewport_size);

    MapGlobeSurfaceRenderResources &resources =
        this->prepared_surface_render_frame.resources;
    resources.window_tiles.clear();
    resources.window_tiles.reserve(this->window_tiles.size());
    for (qsizetype index = 0; index < this->window_tiles.size(); ++index)
    {
        const GlobeTile &tile = this->window_tiles.at(index);
        MapGlobeSurfaceTileRenderState state;
        state.surface_tile_index = int(index);
        state.position_key = MapGlobeSurfaceScene::positionKey(
            tile.zoom, tile.tile_x, tile.tile_y);
        state.virtual_x = tile.virtual_x;
        state.tile_x = tile.tile_x;
        state.tile_y = tile.tile_y;
        state.zoom = tile.zoom;
        state.is_cap = tile.is_cap;
        state.imagery_key = tile.imagery_key;
        state.first_vertex = tile.first_vertex;
        state.vertex_count = tile.vertex_count;
        state.first_index = tile.first_index;
        state.index_count = tile.index_count;
        state.terrain_zoom = tile.terrain_zoom;
        state.terrain_key = tile.terrain_key;
        state.terrain_cell_count = tile.terrain_cell_count;
        state.terrain_stitch_top_cell_count =
            tile.terrain_stitch_top_cell_count;
        state.terrain_stitch_right_cell_count =
            tile.terrain_stitch_right_cell_count;
        state.terrain_stitch_bottom_cell_count =
            tile.terrain_stitch_bottom_cell_count;
        state.terrain_stitch_left_cell_count =
            tile.terrain_stitch_left_cell_count;
        state.terrain_mesh_applied = tile.terrain_mesh_applied;
        state.terrain_mesh_has_relief = tile.terrain_mesh_has_relief;
        if (tile.resource != nullptr)
        {
            state.imagery_ready = tileTextureReady(tile.resource);
            state.imagery_provisional = tile.resource->is_provisional;
            state.heatmap_has_content = tile.resource->heatmap_has_content;
        }
        resources.window_tiles.append(state);
    }

    resources.cap_tiles.clear();
    resources.cap_tiles.reserve(this->cap_tiles.size());
    for (qsizetype index = 0; index < this->cap_tiles.size(); ++index)
    {
        const GlobeTile &tile = this->cap_tiles.at(index);
        MapGlobeSurfaceTileRenderState state;
        state.surface_tile_index = int(index);
        state.position_key = (quint64(1) << 63) | quint64(index);
        state.virtual_x = tile.virtual_x;
        state.tile_x = tile.tile_x;
        state.tile_y = tile.tile_y;
        state.zoom = tile.zoom;
        state.is_cap = true;
        state.imagery_key = tile.imagery_key;
        state.first_vertex = tile.first_vertex;
        state.vertex_count = tile.vertex_count;
        state.first_index = tile.first_index;
        state.index_count = tile.index_count;
        if (tile.resource != nullptr)
        {
            state.imagery_ready = tileTextureReady(tile.resource);
            state.imagery_provisional = tile.resource->is_provisional;
            state.heatmap_has_content = tile.resource->heatmap_has_content;
        }
        resources.cap_tiles.append(state);
    }
}

const MapGlobeSurfaceRenderFrame &MapRhiGlobeRenderer::surfaceRenderFrame() const
{
    return this->prepared_surface_render_frame;
}

bool MapRhiGlobeRenderer::uploadCameraUniform(
    QRhiResourceUpdateBatch *resource_updates,
    const QMatrix4x4 &view_projection,
    const QColor &background_color, float background_opacity)
{
    return this->surface_backend.uploadCameraUniform(
        resource_updates, view_projection, this->heatmap_opacity,
        background_color, background_opacity);
}

bool MapRhiGlobeRenderer::prepare(
    QRhiResourceUpdateBatch *resource_updates, const QMatrix4x4 &view_projection,
    const QSize &viewport_size, float heatmap_opacity,
    const QColor &background_color, float background_opacity,
    bool allow_camera_only_prepare)
{
    if (!this->surface_backend.hasContext()
        || resource_updates == nullptr || this->map_model == nullptr)
        return false;
    if (!ensureSharedResources())
        return false;
    this->heatmap_opacity = qBound(0.0f, heatmap_opacity, 1.0f);
    buildCaps();

    // Terrain-follow animation changes only the camera matrix. Reuse the
    // retained window/resources when all selection inputs and dirty guards
    // still match; the visible pass below will draw the exact same buffers
    // with this frame's smoothly updated matrix.
    if (allow_camera_only_prepare
        && canUseCameraOnlyPrepare(viewport_size))
    {
        refreshPreparedSurfaceRenderFrameState(viewport_size);
        return uploadCameraUniform(
            resource_updates, view_projection,
            background_color, background_opacity);
    }

    // Walk the quadtree fresh every frame -- see the class comment for why
    // this replaced the old single-zoom rectangular window. The walk itself
    // is cheap (horizon/frustum culling plus the hard visit cap keep it to
    // at most a few hundred visited nodes in normal operation); only the
    // actual geometry rebuild below is comparatively expensive, and that is
    // gated on the resulting leaf set actually differing from what is
    // already built.
    const QVector<MapGlobeQuadtreeLeaf> desired_leaves = this->surface_scene.selectVisibleLeaves(
        *this->map_model, viewport_size, this->terrain_repository);

    bool leaves_match_window = !this->window_dirty
        && desired_leaves.size() == this->window_tiles.size()
        && this->window_position_keys.size() == this->window_tiles.size();
    if (leaves_match_window)
    {
        for (const MapGlobeQuadtreeLeaf &leaf : desired_leaves)
        {
            if (!this->window_position_keys.contains(
                    MapGlobeSurfaceScene::positionKey(leaf.zoom, leaf.tile_x, leaf.tile_y)))
            {
                leaves_match_window = false;
                break;
            }
        }
    }

    if (!leaves_match_window)
    {
        rebuildWindow(desired_leaves, viewport_size);
    }
    else
    {
        const bool terrain_enabled = this->terrain_repository != nullptr;
        const bool terrain_view_changed =
            !preparedViewSelectionMatches(viewport_size);
        const bool terrain_lod_check_needed = terrain_enabled
            && (terrain_view_changed || this->terrain_lod_rebuild_pending);

        if (!terrain_enabled)
        {
            this->terrain_lod_rebuild_pending = false;
        }
        else if (terrain_lod_check_needed
                 && this->terrain_lod_rebuild_clock.isValid()
                 && this->terrain_lod_rebuild_clock.elapsed()
                     < GlobeMinimumTerrainLodRebuildIntervalMs)
        {
            // Do not recompute the per-tile terrain LOD merely to discover
            // that the 120 ms rebuild debounce has not expired yet. The old
            // path still ran terrainCellCountForTile() for every visible tile
            // on every full preparation during camera motion, including an
            // orbitCameraBasis() construction per tile. Keep drawing the
            // retained geometry and evaluate the LOD once the debounce opens.
            this->terrain_lod_rebuild_pending = true;
        }
        else if (terrain_lod_check_needed)
        {
            if (!currentTerrainLodMatches(viewport_size))
                rebuildWindow(currentWindowLeaves(), viewport_size);
            else
                this->terrain_lod_rebuild_pending = false;
        }
    }

    if (!applyReadyTerrainMeshes(resource_updates))
        return false;
    requestMissingTerrainTiles();
    scheduleReadyTerrainMeshes();

    // The current Globe shaders do not sample the experimental R32F terrain
    // height-array cache. Running prepareTerrainHeightCache() therefore only
    // scans visible DEM state, allocates VRAM and uploads height textures that
    // cannot affect the rendered frame. Leave the retained implementation in
    // place for the later GPU-displacement work, but keep it off the active
    // path on every backend until a shader actually consumes it.

    // Resolve imagery and stamp array layers before a pending full geometry
    // upload. A rebuilt window then carries every already-ready layer in its
    // single upload instead of issuing one follow-up buffer patch per tile.
    if (this->map_visible && !requestMissingTiles(resource_updates))
        return false;

    // From here on the GPU path consumes a backend-neutral description of
    // the retained Globe surface. QRhi resource ownership lives behind the
    // concrete surface backend and is deliberately absent from the packet.
    rebuildPreparedSurfaceRenderFrame(viewport_size);

    if (!this->surface_backend.uploadGeometry(
            resource_updates,
            this->prepared_surface_render_frame))
    {
        return false;
    }

    if (this->map_visible
        && !uploadHeatmapArrayDrawIndices(resource_updates))
    {
        return false;
    }
    // Heatmap validation can move a tile back to the combined fallback.
    // Rebuild the imagery index stream afterwards so that tile is not also
    // submitted through a stale array range in this same frame.
    if (this->map_visible && !uploadTileArrayDrawIndices(resource_updates))
        return false;
    if (this->map_visible
        && !this->heatmap_array_draw_indices.isEmpty()
        && !uploadHeatmapArrayLayers(resource_updates))
    {
        return false;
    }

    if (!this->surface_backend.uploadPendingHeatmapDummyTexture(
            resource_updates))
    {
        return false;
    }

    if (!uploadCameraUniform(
            resource_updates, view_projection,
            background_color, background_opacity))
    {
        return false;
    }

    rememberPreparedViewState(viewport_size);
    this->preparation_dirty = false;
    return true;
}

void MapRhiGlobeRenderer::draw(QRhiCommandBuffer *command_buffer)
{
    if (command_buffer == nullptr
        || !this->prepared_surface_render_frame.isValid())
    {
        return;
    }

    MapRhiGlobeSurfaceDrawResources &draw_resources =
        this->surface_draw_resources;
    draw_resources.window_tiles.clear();
    draw_resources.cap_tiles.clear();
    draw_resources.imagery_array_batches.clear();
    draw_resources.heatmap_array_batches.clear();

    draw_resources.imagery_array_active = arrayBatchingActive()
        && !this->tile_array_draw_indices.isEmpty();
    draw_resources.heatmap_array_active = heatmapArrayBatchingActive()
        && !this->heatmap_array_draw_indices.isEmpty()
        && !this->heatmap_array_draw_batches.empty();

    draw_resources.window_tiles.reserve(this->window_tiles.size());
    for (const GlobeTile &tile : this->window_tiles)
    {
        MapRhiGlobeSurfaceTileDrawState state;
        state.array_ready = tile.array_ready;
        if (tile.resource != nullptr && tileBindingsReady(tile.resource))
            state.gpu_resource_id = tile.resource->gpu_resource_id;
        draw_resources.window_tiles.append(state);
    }

    draw_resources.cap_tiles.reserve(this->cap_tiles.size());
    for (const GlobeTile &tile : this->cap_tiles)
    {
        MapRhiGlobeSurfaceTileDrawState state;
        if (tile.resource != nullptr && tileBindingsReady(tile.resource))
            state.gpu_resource_id = tile.resource->gpu_resource_id;
        draw_resources.cap_tiles.append(state);
    }

    draw_resources.imagery_array_batches.reserve(
        this->surface_backend.imageryArrayPages().size());
    for (int page_index = 0;
         page_index < int(this->surface_backend.imageryArrayPages().size());
         ++page_index)
    {
        const TileArrayPage &page =
            this->surface_backend.imageryArrayPages()[page_index];
        if (!this->surface_backend.imageryArrayPageReady(page_index)
            || page.draw_index_count <= 0)
        {
            continue;
        }
        MapRhiGlobeSurfaceBatch batch;
        batch.imagery_page_index = page_index;
        batch.first_draw_index = page.first_draw_index;
        batch.draw_index_count = page.draw_index_count;
        draw_resources.imagery_array_batches.push_back(batch);
    }

    draw_resources.heatmap_array_batches.reserve(
        this->heatmap_array_draw_batches.size());
    for (const HeatmapArrayDrawBatch &page_batch :
         this->heatmap_array_draw_batches)
    {
        if (page_batch.draw_index_count <= 0)
            continue;
        MapRhiGlobeSurfaceBatch batch;
        batch.imagery_page_index = page_batch.imagery_page_index;
        batch.heatmap_page_index = page_batch.heatmap_page_index;
        batch.first_draw_index = page_batch.first_draw_index;
        batch.draw_index_count = page_batch.draw_index_count;
        draw_resources.heatmap_array_batches.push_back(batch);
    }

    this->surface_backend.draw(
        command_buffer, this->prepared_surface_render_frame,
        draw_resources);
}

void MapRhiGlobeRenderer::invalidateImagery()
{
    this->preparation_dirty = true;
    this->heatmap_gpu_bake_jobs.clear();
    resetWindowArrayLayers();
    this->surface_backend.clearTileGpuResources();
    this->tile_resources.clear();
    this->cap_resource = TileResource();
    for (TileArrayPage &page : this->surface_backend.imageryArrayPages())
    {
        page.free_layers.clear();
        page.free_layers.reserve(GlobeTileArrayUsableLayerCount);
        for (int layer = GlobeTileArrayLayerCount - 1; layer >= 1; --layer)
            page.free_layers.append(layer);
        page.first_draw_index = 0;
        page.draw_index_count = 0;
    }
    trimUnusedTileArrayPages();
    this->tile_array_draw_indices.clear();
    this->tile_array_draw_indices_dirty = true;
    this->tile_array_draw_index_upload_pending = false;
    for (HeatmapArrayPage &page : this->surface_backend.heatmapArrayPages())
    {
        page.free_layers.clear();
        page.free_layers.reserve(GlobeTileArrayUsableLayerCount);
        for (int layer = GlobeTileArrayLayerCount - 1; layer >= 1; --layer)
            page.free_layers.append(layer);
    }
    trimUnusedHeatmapArrayPages();
    this->heatmap_array_draw_indices.clear();
    this->heatmap_array_draw_batches.clear();
    this->heatmap_array_draw_indices_dirty = true;
    this->heatmap_array_draw_index_upload_pending = false;
    for (GlobeTile &tile : this->window_tiles)
        tile.resource = nullptr;
    for (GlobeTile &tile : this->cap_tiles)
        tile.resource = nullptr;
    this->window_tiles_requested = false;
}

void MapRhiGlobeRenderer::releaseResources()
{
    this->prepared_surface_render_frame = MapGlobeSurfaceRenderFrame();
    this->surface_draw_resources = MapRhiGlobeSurfaceDrawResources();
    this->preparation_dirty = true;
    this->prepared_view_state_valid = false;
    this->full_prepare_clock.invalidate();
    resetTerrainHeightCache();
    releaseVisibleHeatmapGpuBakeAtlasResources();
    releaseDiagnosticHeatmapGpuBakeResources();
    this->heatmap_gpu_bake_jobs.clear();
    this->heatmap_gpu_baking_disabled = false;
    this->diagnostic_heatmap_bake_instances.clear();
    this->diagnostic_heatmap_bake_cpu_reference = QImage();
    this->diagnostic_heatmap_bake_pending = false;
    this->diagnostic_heatmap_bake_revision = 0;
    this->diagnostic_heatmap_gpu_validation_attempted = false;
    this->heatmap_profile_report_pending = false;
    this->heatmap_array_draw_batches.clear();
    this->heatmap_array_draw_indices.clear();
    this->heatmap_array_draw_indices_dirty = true;
    this->heatmap_array_draw_index_upload_pending = false;
    this->heatmap_array_layer_upload_pending = true;
    this->tile_array_draw_indices.clear();
    this->tile_array_draw_indices_dirty = true;
    this->tile_array_draw_index_upload_pending = false;
    this->surface_backend.reset();
    this->window_dirty = true;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.invalidate();
    this->surface_scene.clearVisibilityHistory();
    invalidateImagery();
}
