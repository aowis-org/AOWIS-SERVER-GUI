#include "map/rhi/map_rhi_globe_renderer.h"


#include "map/core/map_model.h"
#include "map/data/map_tile_repository.h"
#include "map/data/map_terrain_repository.h"
#include "map/data/map_terrain_tile.h"
#include "config/gui_configuration.h"
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

// Matches MapRhiBasemapRenderer's HeatmapTextureSize exactly (one texel per
// Web Mercator pixel at whatever zoom a given tile is fetched at) -- see
// MapRhiGlobeRenderer::renderHeatmapTile().
constexpr int GlobeHeatmapTextureSize = MapGlobeHeatmapScene::TextureSize;
// A marker is retained once at every Web Mercator index level. Candidate
// queries choose a level whose expanded tile footprint is only a few cells
// wide, avoiding both the old zoom-18 empty-cell walks and full-network scans.
constexpr int GlobeHeatmapValidationTolerance = 8;

// The polar caps are visual fallback surfaces beyond the Web Mercator
// latitude limit, where basemap tiles do not exist. Keep these renderer-side
// colors until a backend-neutral visual-style contract is introduced.
const QColor GlobePolarCapColor(235, 240, 245);
// Missing imagery must never punch transparent/black holes through the planet
// while requests are still arriving. This fallback is visible only until the
// real tile texture (or a derived neighboring-tile placeholder) is uploaded.
const QColor GlobeMissingTileColor(18, 58, 72);

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
// Visible fallback heatmaps are baked into a single horizontal strip. Keep
// the strip to 16 MiB at RGBA8 on backends supporting a 16384-wide texture;
// lower texture-size limits automatically reduce the number of slots.
constexpr int GlobeHeatmapGpuBakeAtlasMaximumSlots = 64;

// Even a pure height animation periodically refreshes visibility/resource
// state. This bounds any horizon change caused by a large climb and also
// provides a safety net for a missed external dirty notification, while
// still removing roughly five out of six full preparations at 60 Hz.
constexpr int GlobeCameraOnlyMaximumReuseMs = 100;
constexpr int GlobeTileArrayMaximumPageCount =
    (MapGlobeSurfaceScene::MaximumLeafCount + GlobeTileArrayUsableLayerCount - 1)
    / GlobeTileArrayUsableLayerCount;

}


MapRhiGlobeRenderer::MapRhiGlobeRenderer(MapModel *map_model, MapTileRepository *tile_repository)
    : map_model(map_model),
      tile_repository(tile_repository),
      surface_preparation(map_model),
      heatmap_scene(MapModel::MaxZoom)
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

void MapRhiGlobeRenderer::setTerrainRepository(
    MapTerrainRepository *new_terrain_repository)
{
    if (this->terrain_repository == new_terrain_repository)
        return;

    this->terrain_height_cache.reset(this->surface_backend);
    this->terrain_repository = new_terrain_repository;
    this->surface_preparation.setTerrainRepository(new_terrain_repository);
    this->preparation_dirty = true;
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

void MapRhiGlobeRenderer::requestTerrainForCurrentView(
    const QSize &viewport_size)
{
    if (!this->surface_preparation.requestTerrainForCurrentView(viewport_size))
        return;

    this->handleWindowGeometryRebuilt();
    this->preparation_dirty = true;
}

void MapRhiGlobeRenderer::invalidateTerrainView()
{
    this->surface_preparation.invalidateTerrainView();
    this->preparation_dirty = true;
}

bool MapRhiGlobeRenderer::setRenderOriginEcef(
    const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef)
{
    if (!this->surface_preparation.setRenderOriginEcef(origin_ecef))
        return false;

    this->preparation_dirty = true;
    this->cap_tile_gpu_states.clear();
    this->surface_backend.invalidateCaps();
    this->surface_backend.invalidateWireframe();
    return true;
}

void MapRhiGlobeRenderer::notifyTerrainTileAvailable(const QString &key)
{
    if (key.isEmpty())
        return;

    this->preparation_dirty = true;
    this->terrain_height_cache.invalidate(key);

    this->surface_preparation.notifyTerrainTileAvailable(key);
}

void MapRhiGlobeRenderer::invalidateTerrain()
{
    this->preparation_dirty = true;
    this->surface_preparation.invalidateTerrain();
}

void MapRhiGlobeRenderer::setWireframeVisible(bool visible)
{
    if (!this->surface_preparation.setWireframeVisible(visible))
        return;

    this->prepared_surface_render_frame.wireframe_visible = visible;
    this->preparation_dirty = true;
    this->surface_backend.invalidateWireframe();
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
    const QVector<MapGlobeHeatmapMarker> &markers, double radius_m, double solid_fraction)
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
    return this->surface_preparation.hasPendingTerrainMeshes();
}

void MapRhiGlobeRenderer::terrainMeshProgress(
    int *completed, int *total, bool *active) const
{
    this->surface_preparation.terrainMeshProgress(completed, total, active);
}

MapRhiGlobeRenderer::TileGpuState *MapRhiGlobeRenderer::tileGpuState(
    GlobeTile &tile)
{
    if (tile.surface_tile_index < 0)
        return nullptr;

    QVector<TileGpuState> &states = tile.is_cap
        ? this->cap_tile_gpu_states
        : this->window_tile_gpu_states;
    if (tile.surface_tile_index >= states.size())
        states.resize(tile.surface_tile_index + 1);
    return &states[tile.surface_tile_index];
}

const MapRhiGlobeRenderer::TileGpuState *
MapRhiGlobeRenderer::tileGpuState(const GlobeTile &tile) const
{
    if (tile.surface_tile_index < 0)
        return nullptr;

    const QVector<TileGpuState> &states = tile.is_cap
        ? this->cap_tile_gpu_states
        : this->window_tile_gpu_states;
    if (tile.surface_tile_index >= states.size())
        return nullptr;
    return &states.at(tile.surface_tile_index);
}

MapRhiGlobeRenderer::TileResource *MapRhiGlobeRenderer::tileResource(
    GlobeTile &tile)
{
    TileGpuState *state = this->tileGpuState(tile);
    return state != nullptr ? state->resource : nullptr;
}

const MapRhiGlobeRenderer::TileResource *MapRhiGlobeRenderer::tileResource(
    const GlobeTile &tile) const
{
    const TileGpuState *state = this->tileGpuState(tile);
    return state != nullptr ? state->resource : nullptr;
}

void MapRhiGlobeRenderer::setTileResource(
    GlobeTile &tile, TileResource *resource)
{
    TileGpuState *state = this->tileGpuState(tile);
    if (state != nullptr)
        state->resource = resource;
}

bool MapRhiGlobeRenderer::tileArrayReady(const GlobeTile &tile) const
{
    const TileGpuState *state = this->tileGpuState(tile);
    return state != nullptr && state->array_ready;
}

bool MapRhiGlobeRenderer::tileHeatmapArrayReady(
    const GlobeTile &tile) const
{
    const TileGpuState *state = this->tileGpuState(tile);
    return state != nullptr && state->heatmap_array_ready;
}

void MapRhiGlobeRenderer::handleWindowGeometryRebuilt()
{
    this->window_tile_gpu_states.clear();
    this->window_tile_gpu_states.resize(
        this->surface_preparation.windowTiles().size());
    this->window_tiles_requested = false;
    this->surface_backend.invalidateWindowGeometry();
    this->surface_backend.invalidateWireframe();
    this->window_imagery_array_layers.clear();
    this->imagery_array_layer_upload_pending = true;
    this->window_heatmap_array_layers.clear();
    this->heatmap_array_layer_upload_pending = true;
    this->tile_array_draw_indices_dirty = true;
    this->heatmap_array_draw_indices_dirty = true;
    this->pruneUnusedTileResources();
}

bool MapRhiGlobeRenderer::visibleTerrainRayIntersection(
    const GeoWgs84Ellipsoid::EcefPositionD &ray_origin_ecef,
    const QVector3D &ray_direction_ecef,
    GeoWgs84Ellipsoid::EcefPositionD *intersection_ecef,
    double *distance_m) const
{
    return this->surface_preparation.visibleTerrainRayIntersection(
        ray_origin_ecef, ray_direction_ecef, intersection_ecef, distance_m);
}

bool MapRhiGlobeRenderer::visibleTerrainSamplingAtCoordinate(
    const CoordinateWGS84 &coordinate,
    int *terrain_zoom,
    double *cell_size_m) const
{
    return this->surface_preparation.visibleTerrainSamplingAtCoordinate(
        coordinate, terrain_zoom, cell_size_m);
}


void MapRhiGlobeRenderer::pruneUnusedTileResources()
{
    QSet<QString> keys_in_use;
    keys_in_use.reserve(this->surface_preparation.windowTiles().size());
    for (const GlobeTile &tile : this->surface_preparation.windowTiles())
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
        this->setTileHeatmapArrayReady(tile, false);

    TileGpuState *state = this->tileGpuState(tile);
    if (state == nullptr || state->array_ready == ready)
        return;

    state->array_ready = ready;
    this->tile_array_draw_indices_dirty = true;
    this->heatmap_array_draw_indices_dirty = true;
}

void MapRhiGlobeRenderer::setTileHeatmapArrayReady(
    GlobeTile &tile, bool ready)
{
    TileGpuState *state = this->tileGpuState(tile);
    if (state == nullptr || state->heatmap_array_ready == ready)
        return;

    state->heatmap_array_ready = ready;
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
    for (GlobeTile &tile : this->surface_preparation.windowTiles())
    {
        setTileArrayReady(tile, false);
        setTileHeatmapArrayReady(tile, false);
    }

    if (!this->window_imagery_array_layers.isEmpty())
    {
        this->window_imagery_array_layers.clear();
        this->imagery_array_layer_upload_pending = true;
    }
    if (!this->window_heatmap_array_layers.isEmpty())
    {
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
    for (GlobeTile &tile : this->surface_preparation.windowTiles())
    {
        if (!this->tileArrayReady(tile))
            continue;

        const bool valid_page_index = this->tileResource(tile) != nullptr
            && this->tileResource(tile)->array_page >= 0
            && this->tileResource(tile)->array_page < int(this->surface_backend.imageryArrayPages().size());
        bool valid_page = false;
        if (valid_page_index)
        {
            valid_page = this->surface_backend.imageryArrayPageReady(
                this->tileResource(tile)->array_page);
        }
        const bool valid_resource = valid_page
            && this->tileResource(tile)->array_layer > 0
            && this->tileResource(tile)->array_layer < GlobeTileArrayLayerCount;
        const bool valid_geometry = tile.first_index >= 0
            && tile.index_count > 0
            && qsizetype(tile.first_index) + tile.index_count
                <= this->surface_preparation.windowIndices().size();
        if (!valid_resource || !valid_geometry)
            setTileArrayReady(tile, false);
    }

    this->tile_array_draw_indices.reserve(this->surface_preparation.windowIndices().size());
    for (int page_index = 0;
         page_index < int(this->surface_backend.imageryArrayPages().size()); ++page_index)
    {
        TileArrayPage &page = this->surface_backend.imageryArrayPages()[page_index];
        page.first_draw_index = this->tile_array_draw_indices.size();
        for (const GlobeTile &tile : this->surface_preparation.windowTiles())
        {
            if (!this->tileArrayReady(tile) || this->tileResource(tile) == nullptr
                || this->tileResource(tile)->array_page != page_index)
            {
                continue;
            }

            const qsizetype destination_first =
                this->tile_array_draw_indices.size();
            this->tile_array_draw_indices.resize(
                destination_first + tile.index_count);
            std::copy_n(
                this->surface_preparation.windowIndices().constData() + tile.first_index,
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
            this->surface_preparation.windowIndices().size()))
    {
        return false;
    }

    this->tile_array_draw_index_upload_pending = false;
    return true;
}

bool MapRhiGlobeRenderer::uploadImageryArrayLayers(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource_updates == nullptr)
        return false;
    if (!this->imagery_array_layer_upload_pending)
        return true;
    if (this->window_imagery_array_layers.isEmpty())
    {
        this->imagery_array_layer_upload_pending = false;
        return true;
    }

    if (!this->surface_backend.uploadImageryArrayLayers(
            resource_updates, this->window_imagery_array_layers))
    {
        return false;
    }

    this->imagery_array_layer_upload_pending = false;
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
    for (GlobeTile &tile : this->surface_preparation.windowTiles())
    {
        if (!this->tileArrayReady(tile))
            continue;

        const bool valid_imagery_page_index = this->tileResource(tile) != nullptr
            && this->tileResource(tile)->array_page >= 0
            && this->tileResource(tile)->array_page
                < int(this->surface_backend.imageryArrayPages().size());
        bool valid_imagery_page = false;
        if (valid_imagery_page_index)
        {
            valid_imagery_page = this->surface_backend.imageryArrayPageReady(
                this->tileResource(tile)->array_page);
        }

        const bool has_heatmap = this->tileResource(tile) != nullptr
            && this->tileResource(tile)->heatmap_has_content;
        const bool valid_heatmap_page_index = has_heatmap
            && this->tileResource(tile)->heatmap_array_page >= 0
            && this->tileResource(tile)->heatmap_array_page
                < int(this->surface_backend.heatmapArrayPages().size());
        bool valid_heatmap_page = !has_heatmap;
        if (valid_heatmap_page_index)
        {
            valid_heatmap_page = this->surface_backend.heatmapArrayPageReady(
                this->tileResource(tile)->heatmap_array_page);
        }
        const bool valid_heatmap = valid_heatmap_page
            && (!has_heatmap
                || (this->tileHeatmapArrayReady(tile)
                    && this->tileResource(tile)->heatmap_revision
                        == this->heatmap_scene.revision()
                    && this->tileResource(tile)->heatmap_array_revision
                        == this->heatmap_scene.revision()
                    && this->tileResource(tile)->heatmap_array_layer > 0
                    && this->tileResource(tile)->heatmap_array_layer
                        < GlobeTileArrayLayerCount));
        const bool valid_resource = valid_imagery_page
            && valid_heatmap
            && this->tileResource(tile)->array_layer > 0
            && this->tileResource(tile)->array_layer < GlobeTileArrayLayerCount;
        const bool valid_geometry = tile.first_index >= 0
            && tile.index_count > 0
            && qsizetype(tile.first_index) + tile.index_count
                <= this->surface_preparation.windowIndices().size();
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

    this->heatmap_array_draw_indices.reserve(this->surface_preparation.windowIndices().size());
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
            for (const GlobeTile &tile : this->surface_preparation.windowTiles())
            {
                if (!this->tileArrayReady(tile) || this->tileResource(tile) == nullptr
                    || this->tileResource(tile)->array_page != imagery_page_index)
                {
                    continue;
                }

                const int effective_heatmap_page =
                    this->tileResource(tile)->heatmap_has_content
                    ? this->tileResource(tile)->heatmap_array_page : 0;
                if (effective_heatmap_page != heatmap_page_index)
                    continue;

                const qsizetype destination_first =
                    this->heatmap_array_draw_indices.size();
                this->heatmap_array_draw_indices.resize(
                    destination_first + tile.index_count);
                std::copy_n(
                    this->surface_preparation.windowIndices().constData() + tile.first_index,
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
            this->heatmap_array_draw_indices, this->surface_preparation.windowIndices().size()))
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
            != this->surface_preparation.windowVertices().size())
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
            > this->surface_preparation.windowVertices().size())
    {
        return false;
    }

    if (this->window_imagery_array_layers.size()
        != this->surface_preparation.windowVertices().size())
    {
        this->window_imagery_array_layers.fill(
            0.0f, this->surface_preparation.windowVertices().size());
        this->imagery_array_layer_upload_pending = true;
    }

    const float expected_layer = float(resource.array_layer);
    if (this->window_imagery_array_layers.at(tile.first_vertex)
        == expected_layer)
    {
        return true;
    }

    for (int index = 0; index < tile.vertex_count; ++index)
    {
        this->window_imagery_array_layers[tile.first_vertex + index] =
            expected_layer;
    }

    if (!this->imagery_array_layer_upload_pending
        && !this->surface_backend.patchImageryArrayLayers(
            resource_updates, tile.first_vertex, tile.vertex_count,
            this->window_imagery_array_layers))
    {
        this->imagery_array_layer_upload_pending = true;
    }
    return true;
}

bool MapRhiGlobeRenderer::ensureTileArrayLayer(
    GlobeTile &tile, const QImage &updated_image,
    QRhiResourceUpdateBatch *resource_updates)
{
    if (!arrayBatchingActive() || tile.is_cap || this->tileResource(tile) == nullptr
        || !tileTextureReady(this->tileResource(tile)) || resource_updates == nullptr)
    {
        setTileArrayReady(tile, false);
        return true;
    }

    TileResource *resource = this->tileResource(tile);
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
            > this->surface_preparation.windowVertices().size())
    {
        return false;
    }

    if (this->window_heatmap_array_layers.size()
        != this->surface_preparation.windowVertices().size())
    {
        this->window_heatmap_array_layers.fill(
            0.0f, this->surface_preparation.windowVertices().size());
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
    if (this->tileResource(tile) == nullptr)
    {
        setTileHeatmapArrayReady(tile, false);
        return true;
    }

    TileResource *resource = this->tileResource(tile);
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

    if (!this->tileArrayReady(tile) || resource_updates == nullptr
        || !createHeatmapArrayResources(resource_updates))
    {
        // An affected tile must retain the ordinary combined
        // imagery/heatmap draw when the heatmap array path is unavailable.
        setTileHeatmapArrayReady(tile, false);
        if (this->tileArrayReady(tile))
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

        this->setTileResource(tile, &this->cap_resource);
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
        // this->tileResource(tile) is only actually set on some of
        // ensureProvisionalTileResource()'s success paths -- see its own
        // comment on the "nothing to derive a placeholder from yet" case,
        // which deliberately leaves it untouched so draw() falls back to
        // template_bindings instead. No heatmap texture to prepare for a
        // tile that isn't even going to use this resource.
        if (this->tileResource(tile) == resource
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

    this->setTileResource(tile, resource);
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
        this->setTileResource(tile, resource);
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

        this->setTileResource(tile, resource);
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
            this->setTileResource(tile, resource);
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

        this->setTileResource(tile, resource);
        return true;
    }

    // Neither direct children nor any ancestor are loaded yet (e.g. the
    // very first tiles requested right after startup, or a fresh area with
    // nothing cached at any nearby zoom) -- nothing to derive a placeholder
    // from. Leaving this->tileResource(tile) untouched here falls through to
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
    this->heatmap_profile.visible_tiles = this->surface_preparation.windowTiles().size();
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
        for (const GlobeTile &tile : this->surface_preparation.windowTiles())
        {
            if (this->tile_repository->tile(tile.imagery_key) != nullptr)
                continue;

            const int priority = mapGlobeSurfaceTileRequestPriority(
                tile.tile_x, tile.tile_y, tile.zoom,
                this->map_model->centerLon(), this->map_model->centerLat());
            this->tile_repository->requestTile(
                this->map_model->tileEndpointAtZoom(tile.tile_x, tile.tile_y, tile.zoom),
                tile.imagery_key, tile.tile_x, tile.tile_y, priority, batch, true);
        }
        this->window_tiles_requested = true;
    }

    for (GlobeTile &tile : this->surface_preparation.windowTiles())
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
        if (!this->tileArrayReady(tile) && this->tileResource(tile) != nullptr
            && !ensureHeatmapFallbackTexture(
                tile, this->tileResource(tile), updated_heatmap_image,
                updated_heatmap_stamps,
                resource_updates))
        {
            return false;
        }
    }
    for (GlobeTile &tile : this->surface_preparation.capTiles())
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

    if (this->surface_preparation.ensureCapsBuilt())
    {
        this->cap_tile_gpu_states.clear();
        this->cap_tile_gpu_states.resize(
            this->surface_preparation.capTiles().size());
        this->surface_backend.invalidateCaps();
        this->surface_backend.invalidateWireframe();
    }
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
        || this->map_model == nullptr || this->surface_preparation.windowDirty()
        || !this->full_prepare_clock.isValid()
        || this->full_prepare_clock.elapsed()
            >= GlobeCameraOnlyMaximumReuseMs
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
        || (!this->tile_array_draw_indices.isEmpty()
            && this->imagery_array_layer_upload_pending)
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
    {
        return false;
    }
    if (!this->ensureSharedResources())
        return false;

    this->heatmap_opacity = qBound(0.0f, heatmap_opacity, 1.0f);
    if (this->surface_preparation.ensureCapsBuilt())
    {
        this->cap_tile_gpu_states.clear();
        this->cap_tile_gpu_states.resize(
            this->surface_preparation.capTiles().size());
        this->surface_backend.invalidateCaps();
        this->surface_backend.invalidateWireframe();
    }

    // Terrain-follow animation changes only the camera matrix. Reuse the
    // retained surface/resources when all selection inputs and dirty guards
    // still match; surface preparation itself is now backend-neutral.
    if (allow_camera_only_prepare
        && this->canUseCameraOnlyPrepare(viewport_size))
    {
        this->surface_preparation.refreshRenderFrame(
            &this->prepared_surface_render_frame, viewport_size,
            this->map_visible, this->heatmap_opacity,
            this->heatmap_scene.revision(),
            this->heatmap_scene.layoutRevision(),
            this->heatmap_scene.activeMarkerCount());
        return this->uploadCameraUniform(
            resource_updates, view_projection,
            background_color, background_opacity);
    }

    if (this->surface_preparation.prepareVisibleWindow(viewport_size))
        this->handleWindowGeometryRebuilt();

    QVector<MapGlobeSurfaceVertexPatch> terrain_vertex_patches;
    const bool terrain_geometry_changed =
        this->surface_preparation.applyReadyTerrainMeshes(
            &terrain_vertex_patches);
    if (terrain_geometry_changed)
    {
        for (const MapGlobeSurfaceVertexPatch &patch : terrain_vertex_patches)
        {
            if (!this->surface_backend.patchWindowVertices(
                    resource_updates, patch.first_vertex, patch.vertex_count,
                    this->surface_preparation.windowVertices()))
            {
                this->surface_backend.invalidateWindowVertices();
                break;
            }
        }
        if (this->surface_preparation.wireframeVisible())
            this->surface_backend.invalidateWireframe();
    }

    this->surface_preparation.requestMissingTerrainTiles();
    this->surface_preparation.scheduleReadyTerrainMeshes();

    // The current Globe shaders do not sample the experimental R32F terrain
    // height-array cache. The retained implementation remains QRhi-owned, but
    // stays off the active path until a shader consumes it.

    // Resolve QRhi-owned imagery and heatmap array-layer streams before the
    // retained surface is drawn. Neutral Globe geometry contains only
    // position/UV data; array assignments are uploaded separately below.
    if (this->map_visible && !this->requestMissingTiles(resource_updates))
        return false;

    this->surface_preparation.rebuildRenderFrame(
        &this->prepared_surface_render_frame, viewport_size,
        this->map_visible, this->heatmap_opacity,
        this->heatmap_scene.revision(),
        this->heatmap_scene.layoutRevision(),
        this->heatmap_scene.activeMarkerCount());
    if (!this->surface_backend.uploadGeometry(
            resource_updates, this->prepared_surface_render_frame))
    {
        return false;
    }

    if (this->map_visible
        && !this->uploadHeatmapArrayDrawIndices(resource_updates))
    {
        return false;
    }
    if (this->map_visible
        && !this->uploadTileArrayDrawIndices(resource_updates))
    {
        return false;
    }
    if (this->map_visible
        && !this->tile_array_draw_indices.isEmpty()
        && !uploadImageryArrayLayers(resource_updates))
    {
        return false;
    }
    if (this->map_visible
        && !this->heatmap_array_draw_indices.isEmpty()
        && !this->uploadHeatmapArrayLayers(resource_updates))
    {
        return false;
    }

    if (!this->surface_backend.uploadPendingHeatmapDummyTexture(
            resource_updates))
    {
        return false;
    }

    if (!this->uploadCameraUniform(
            resource_updates, view_projection,
            background_color, background_opacity))
    {
        return false;
    }

    this->rememberPreparedViewState(viewport_size);
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

    draw_resources.window_tiles.reserve(this->surface_preparation.windowTiles().size());
    for (const GlobeTile &tile : this->surface_preparation.windowTiles())
    {
        MapRhiGlobeSurfaceTileDrawState state;
        state.array_ready = this->tileArrayReady(tile);
        if (this->tileResource(tile) != nullptr && tileBindingsReady(this->tileResource(tile)))
            state.gpu_resource_id = this->tileResource(tile)->gpu_resource_id;
        draw_resources.window_tiles.append(state);
    }

    draw_resources.cap_tiles.reserve(this->surface_preparation.capTiles().size());
    for (const GlobeTile &tile : this->surface_preparation.capTiles())
    {
        MapRhiGlobeSurfaceTileDrawState state;
        if (this->tileResource(tile) != nullptr && tileBindingsReady(this->tileResource(tile)))
            state.gpu_resource_id = this->tileResource(tile)->gpu_resource_id;
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
    for (TileGpuState &state : this->window_tile_gpu_states)
    {
        state.resource = nullptr;
        state.array_ready = false;
        state.heatmap_array_ready = false;
    }
    for (TileGpuState &state : this->cap_tile_gpu_states)
    {
        state.resource = nullptr;
        state.array_ready = false;
        state.heatmap_array_ready = false;
    }
    this->window_tiles_requested = false;
}

void MapRhiGlobeRenderer::releaseResources()
{
    this->prepared_surface_render_frame = MapGlobeSurfaceRenderFrame();
    this->surface_draw_resources = MapRhiGlobeSurfaceDrawResources();
    this->preparation_dirty = true;
    this->prepared_view_state_valid = false;
    this->full_prepare_clock.invalidate();
    this->terrain_height_cache.reset(this->surface_backend);
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
    this->surface_preparation.invalidateTerrainView();
    this->window_tile_gpu_states.clear();
    this->cap_tile_gpu_states.clear();
    this->invalidateImagery();
}
