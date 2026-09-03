#include "map_rhi_basemap_renderer.h"

#include "map_model.h"
#include "map_render_cache_math.h"
#include "map_rhi_scene.h"
#include "map_terrain_repository.h"
#include "map_terrain_tile.h"
#include "map_tile_repository.h"
#include "../geo_web_mercator.h"

#include <QFile>
#include <QImage>
#include <QPainter>
#include <QRadialGradient>
#include <QPixmap>
#include <QtMath>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>

#include <cmath>
#include <cstddef>
#include <limits>

namespace
{
constexpr int MaximumCachedGpuTiles = 160;
constexpr int TerrainReliefMinimumZoom = 8;
constexpr int TerrainReliefMaximumZoom = 14;
constexpr int TwoDPanRetentionMarginTiles = 4;
constexpr int TerrainMinimumLodCellCount = 1;
constexpr int TerrainBackgroundRequestMinimumLodCellCount = 4;
constexpr double TerrainTargetCellSizePixels = 32.0;
constexpr int HeatmapTextureSize = 256;
constexpr double HeatmapMarkerBucketWorldSize = 16384.0;
// A LOD-only terrain rebuild resamples the DEM for, and re-uploads, the
// entire retained tile apron. During continuous 3D camera motion (orbit
// drag, wheel zoom) the desired mesh density for some tile in the apron
// changes on nearly every input frame, so this rebuild is rate-limited
// instead of being allowed to run at input/vsync frequency. Imagery panning
// keeps using its own, already tile-grid-bounded gate and is unaffected.
constexpr qint64 MinimumTerrainLodRebuildIntervalMs = 120;

quint64 heatmapMarkerBucketKey(int bucket_x, int bucket_y)
{
    return (quint64(quint32(bucket_x)) << 32) | quint64(quint32(bucket_y));
}

quint64 tilePositionKey(int virtual_x, int y)
{
    return (quint64(quint32(virtual_x)) << 32) | quint64(quint32(y));
}

int parentVirtualTileX(int child_virtual_x)
{
    return int(std::floor(double(child_virtual_x) * 0.5));
}

int heatmapMarkerBucketCoordinate(double world_coordinate)
{
    return int(std::floor(world_coordinate / HeatmapMarkerBucketWorldSize));
}

bool reliefTerrainEnabled(const MapModel &map_model, const MapTerrainRepository *terrain_repository)
{
    return map_model.viewMode() == MapViewMode::ThreeD
        && terrain_repository != nullptr
        && map_model.zoom() >= TerrainReliefMinimumZoom;
}

int terrainZoomForImageryZoom(int imagery_zoom)
{
    return qBound(TerrainReliefMinimumZoom, imagery_zoom, TerrainReliefMaximumZoom);
}

QString terrainDatasetId()
{
    return QStringLiteral("copernicus-glo30");
}

float terrainSample(const MapTerrainTile &terrain_tile, int row, int column)
{
    const int bounded_row = qBound(0, row, MapTerrainTileGridSize - 1);
    const int bounded_column = qBound(0, column, MapTerrainTileGridSize - 1);
    return terrain_tile.elevations_m.at(
        bounded_row * MapTerrainTileGridSize + bounded_column);
}

float nearestFiniteTerrainSample(const MapTerrainTile &terrain_tile, int row, int column)
{
    const int bounded_row = qBound(0, row, MapTerrainTileGridSize - 1);
    const int bounded_column = qBound(0, column, MapTerrainTileGridSize - 1);
    const float direct = terrainSample(terrain_tile, bounded_row, bounded_column);
    if (std::isfinite(double(direct)))
        return direct;

    for (int ring = 1; ring < MapTerrainTileGridSize; ++ring)
    {
        const int row_minimum = qMax(0, bounded_row - ring);
        const int row_maximum = qMin(MapTerrainTileGridSize - 1, bounded_row + ring);
        const int column_minimum = qMax(0, bounded_column - ring);
        const int column_maximum = qMin(MapTerrainTileGridSize - 1, bounded_column + ring);

        for (int sample_column = column_minimum; sample_column <= column_maximum; ++sample_column)
        {
            const float top = terrainSample(terrain_tile, row_minimum, sample_column);
            if (std::isfinite(double(top)))
                return top;
            const float bottom = terrainSample(terrain_tile, row_maximum, sample_column);
            if (std::isfinite(double(bottom)))
                return bottom;
        }
        for (int sample_row = row_minimum + 1; sample_row < row_maximum; ++sample_row)
        {
            const float left = terrainSample(terrain_tile, sample_row, column_minimum);
            if (std::isfinite(double(left)))
                return left;
            const float right = terrainSample(terrain_tile, sample_row, column_maximum);
            if (std::isfinite(double(right)))
                return right;
        }
    }

    return 0.0f;
}

float bilinearTerrainSample(const MapTerrainTile &terrain_tile, double u, double v)
{
    const double sample_x = qBound(0.0, u, 1.0) * MapTerrainTileCellCount;
    const double sample_y = qBound(0.0, v, 1.0) * MapTerrainTileCellCount;
    const int x0 = qBound(0, int(std::floor(sample_x)), MapTerrainTileGridSize - 1);
    const int y0 = qBound(0, int(std::floor(sample_y)), MapTerrainTileGridSize - 1);
    const int x1 = qMin(x0 + 1, MapTerrainTileGridSize - 1);
    const int y1 = qMin(y0 + 1, MapTerrainTileGridSize - 1);
    const double tx = sample_x - x0;
    const double ty = sample_y - y0;

    const float samples[4] = {
        terrainSample(terrain_tile, y0, x0),
        terrainSample(terrain_tile, y0, x1),
        terrainSample(terrain_tile, y1, x0),
        terrainSample(terrain_tile, y1, x1)
    };
    const double weights[4] = {
        (1.0 - tx) * (1.0 - ty),
        tx * (1.0 - ty),
        (1.0 - tx) * ty,
        tx * ty
    };

    double weighted_sum = 0.0;
    double weight_sum = 0.0;
    for (int index = 0; index < 4; ++index)
    {
        if (!std::isfinite(double(samples[index])))
            continue;
        weighted_sum += double(samples[index]) * weights[index];
        weight_sum += weights[index];
    }
    if (weight_sum > 0.0)
        return float(weighted_sum / weight_sum);

    return nearestFiniteTerrainSample(
        terrain_tile, int(std::lround(sample_y)), int(std::lround(sample_x)));
}

QShader loadBasemapShader(const QString &resource_path)
{
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly))
        return QShader();
    return QShader::fromSerialized(file.readAll());
}

int boundedBufferSize(qsizetype vertex_count, qsizetype vertex_size)
{
    const qsizetype bytes = vertex_count * vertex_size;
    if (bytes <= 0)
        return 1;
    if (bytes > qsizetype(std::numeric_limits<int>::max()))
        return 0;
    return int(bytes);
}
}

MapRhiBasemapRenderer::MapRhiBasemapRenderer(
    MapModel *map_model, MapRhiScene *scene, MapTileRepository *tile_repository,
    MapTerrainRepository *terrain_repository)
    : map_model(map_model),
      scene(scene),
      tile_repository(tile_repository),
      terrain_repository(terrain_repository)
{
}

MapRhiBasemapRenderer::~MapRhiBasemapRenderer() = default;

void MapRhiBasemapRenderer::setTileRepository(MapTileRepository *tile_repository)
{
    if (this->tile_repository == tile_repository)
        return;

    this->tile_repository = tile_repository;
    invalidate();
}

void MapRhiBasemapRenderer::setTerrainRepository(MapTerrainRepository *terrain_repository)
{
    if (this->terrain_repository == terrain_repository)
        return;

    this->terrain_repository = terrain_repository;
    this->dirty_terrain_keys.clear();
    invalidate();
}

void MapRhiBasemapRenderer::notifyTerrainTileAvailable(const QString &key)
{
    if (!key.isEmpty())
        this->dirty_terrain_keys.insert(key);
}

void MapRhiBasemapRenderer::setHeatmapOverlay(
    const QVector<HeatmapMarker> &markers, double radius_world,
    double solid_fraction)
{
    const double bounded_radius_world = qMax(0.0, radius_world);
    const double bounded_solid_fraction = qBound(0.0, solid_fraction, 0.9);
    const bool markers_changed = this->heatmap_markers != markers;
    const bool style_changed =
        !qFuzzyCompare(1.0 + this->heatmap_radius_world,
                       1.0 + bounded_radius_world)
        || !qFuzzyCompare(1.0 + this->heatmap_solid_fraction,
                          1.0 + bounded_solid_fraction);
    if (!markers_changed && !style_changed)
        return;

    if (markers_changed)
    {
        this->heatmap_markers = markers;
        rebuildHeatmapMarkerBuckets();
    }
    this->heatmap_radius_world = bounded_radius_world;
    this->heatmap_solid_fraction = bounded_solid_fraction;
    ++this->heatmap_revision;
    if (this->heatmap_revision == 0)
        this->heatmap_revision = 1;
}

void MapRhiBasemapRenderer::setHeatmapStyle(
    double radius_world, double solid_fraction)
{
    const double bounded_radius_world = qMax(0.0, radius_world);
    const double bounded_solid_fraction = qBound(0.0, solid_fraction, 0.9);
    if (qFuzzyCompare(1.0 + this->heatmap_radius_world,
                      1.0 + bounded_radius_world)
        && qFuzzyCompare(1.0 + this->heatmap_solid_fraction,
                         1.0 + bounded_solid_fraction))
    {
        return;
    }

    this->heatmap_radius_world = bounded_radius_world;
    this->heatmap_solid_fraction = bounded_solid_fraction;
    if (this->heatmap_markers.isEmpty())
        return;

    ++this->heatmap_revision;
    if (this->heatmap_revision == 0)
        this->heatmap_revision = 1;
}


void MapRhiBasemapRenderer::setWireframeVisible(bool visible)
{
    if (this->wireframe_visible == visible)
        return;

    this->wireframe_visible = visible;
    if (visible)
        this->wireframe_vertex_upload_pending = true;
}

void MapRhiBasemapRenderer::setMapVisible(bool visible)
{
    if (this->map_visible == visible)
        return;

    this->map_visible = visible;
    invalidate();
}

void MapRhiBasemapRenderer::invalidate()
{
    this->layout_dirty = true;
    this->vertex_upload_pending = true;
    this->wireframe_vertex_upload_pending = true;
}

void MapRhiBasemapRenderer::releaseResources()
{
    this->pipeline.reset();
    this->wireframe_pipeline.reset();
    this->template_bindings.reset();
    this->wireframe_bindings.reset();
    this->dummy_texture.reset();
    this->sampler.reset();
    this->vertex_buffer.reset();
    this->wireframe_vertex_buffer.reset();
    this->tile_resources.clear();
    this->visible_tiles.clear();
    this->vertices.clear();
    this->wireframe_vertices.clear();
    this->dirty_terrain_keys.clear();
    this->layout_origin_world = QPointF();
    this->vertex_buffer_size = 0;
    this->wireframe_vertex_buffer_size = 0;
    this->vertex_upload_pending = true;
    this->wireframe_vertex_upload_pending = true;
    this->dummy_texture_upload_pending = true;
    this->layout_dirty = true;
    this->terrain_lod_rebuild_clock.invalidate();
    this->rhi = nullptr;
    this->render_pass_descriptor = nullptr;
    this->camera_uniform_buffer = nullptr;
}

bool MapRhiBasemapRenderer::initialize(
    QRhi *rhi, QRhiRenderPassDescriptor *render_pass_descriptor,
    QRhiBuffer *camera_uniform_buffer, int sample_count)
{
    if (rhi == nullptr || render_pass_descriptor == nullptr || camera_uniform_buffer == nullptr)
        return false;

    const bool context_changed = this->rhi != rhi
        || this->camera_uniform_buffer != camera_uniform_buffer;
    const bool render_pass_changed = this->render_pass_descriptor != render_pass_descriptor
        || this->sample_count != sample_count;
    if (context_changed)
        releaseResources();
    else if (render_pass_changed)
    {
        this->pipeline.reset();
        this->wireframe_pipeline.reset();
    }

    this->rhi = rhi;
    this->render_pass_descriptor = render_pass_descriptor;
    this->camera_uniform_buffer = camera_uniform_buffer;
    this->sample_count = qMax(1, sample_count);
    return createSharedResources();
}

bool MapRhiBasemapRenderer::prepare(
    QRhiResourceUpdateBatch *resource_updates,
    const QPointF &origin_world, const QSize &viewport_size)
{
    if (this->rhi == nullptr || this->map_model == nullptr
        || this->tile_repository == nullptr || resource_updates == nullptr)
    {
        return true;
    }

    if (!createSharedResources())
        return false;

    if (!rebuildVisibleTiles(origin_world, viewport_size))
        return false;

    const bool map_draw_enabled = this->map_model->viewMode() != MapViewMode::ThreeD
        || this->map_visible;
    if (map_draw_enabled)
    {
        if (this->dummy_texture_upload_pending && this->dummy_texture)
        {
            QImage transparent_pixel(1, 1, QImage::Format_RGBA8888);
            transparent_pixel.fill(Qt::transparent);
            resource_updates->uploadTexture(this->dummy_texture.get(), transparent_pixel);
            this->dummy_texture_upload_pending = false;
        }

        ++this->usage_serial;
        for (VisibleTile &tile : this->visible_tiles)
        {
            TileResource *resource = nullptr;
            if (!ensureTileResource(tile, &resource, resource_updates))
                return false;
            tile.resource = resource;
            if (resource != nullptr)
                resource->last_used_serial = this->usage_serial;
        }
    }
    else
    {
        for (VisibleTile &tile : this->visible_tiles)
            tile.resource = nullptr;
    }

    if (this->vertex_upload_pending && !this->vertices.isEmpty())
    {
        const int required_bytes = boundedBufferSize(
            this->vertices.size(), qsizetype(sizeof(TileVertex)));
        if (required_bytes <= 0)
            return false;

        if (!this->vertex_buffer || this->vertex_buffer_size != required_bytes)
        {
            this->vertex_buffer.reset(this->rhi->newBuffer(
                QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_bytes));
            if (!this->vertex_buffer || !this->vertex_buffer->create())
                return false;
            this->vertex_buffer_size = required_bytes;
        }

        resource_updates->updateDynamicBuffer(
            this->vertex_buffer.get(), 0, required_bytes, this->vertices.constData());
        this->vertex_upload_pending = false;
        this->dirty_terrain_keys.clear();
    }
    else if (!updateDirtyTerrainTiles(resource_updates))
    {
        return false;
    }

    if (this->map_model->viewMode() == MapViewMode::ThreeD
        && this->wireframe_visible
        && !uploadWireframeVertices(resource_updates))
    {
        return false;
    }

    if (map_draw_enabled)
        pruneTextureCache();
    return true;
}

void MapRhiBasemapRenderer::draw(QRhiCommandBuffer *command_buffer)
{
    if (command_buffer == nullptr || !this->vertex_buffer)
        return;

    const bool three_d = this->map_model != nullptr
        && this->map_model->viewMode() == MapViewMode::ThreeD;
    if ((!three_d || this->map_visible) && this->pipeline)
    {
        command_buffer->setGraphicsPipeline(this->pipeline.get());
        for (const VisibleTile &tile : this->visible_tiles)
        {
            if (tile.resource == nullptr || !tile.resource->bindings)
                continue;

            command_buffer->setShaderResources(tile.resource->bindings.get());
            const quint32 byte_offset = quint32(
                tile.first_vertex * int(sizeof(TileVertex)));
            const QRhiCommandBuffer::VertexInput binding(
                this->vertex_buffer.get(), byte_offset);
            command_buffer->setVertexInput(0, 1, &binding);
            command_buffer->draw(quint32(tile.vertex_count));
        }
    }

    if (three_d && this->wireframe_visible
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

bool MapRhiBasemapRenderer::createSharedResources()
{
    if (this->rhi == nullptr || this->render_pass_descriptor == nullptr
        || this->camera_uniform_buffer == nullptr)
    {
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
        this->dummy_texture.reset(this->rhi->newTexture(
            QRhiTexture::RGBA8, QSize(1, 1)));
        if (!this->dummy_texture || !this->dummy_texture->create())
            return false;
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
                this->camera_uniform_buffer),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                this->dummy_texture.get(), this->sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage,
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
                0, QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                this->camera_uniform_buffer)
        });
        if (!this->wireframe_bindings->create())
            return false;
    }

    if (!this->pipeline)
    {
        const QShader vertex_shader = loadBasemapShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_basemap.vert.qsb"));
        const QShader fragment_shader = loadBasemapShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_basemap.frag.qsb"));
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
        const QShader vertex_shader = loadBasemapShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_wireframe.vert.qsb"));
        const QShader fragment_shader = loadBasemapShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_wireframe.frag.qsb"));
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

bool MapRhiBasemapRenderer::rebuildVisibleTiles(
    const QPointF &origin_world, const QSize &viewport_size)
{
    if (this->map_model == nullptr || !viewport_size.isValid())
        return true;

    const int imagery_zoom = this->map_model->zoom();
    const int tile_count = this->map_model->tileCount();
    const QPointF center = this->map_model->centerTile();
    const double tile_reference_size = MapModel::TileSize
        * std::pow(2.0, MapRenderCacheMath::ReferenceZoom - imagery_zoom);
    const double origin_tile_x = origin_world.x() / tile_reference_size;
    const double wrap_offset = std::round(
        (origin_tile_x - center.x()) / qMax(1, tile_count)) * tile_count;
    const double rendered_center_x = center.x() + wrap_offset;
    const double view_scale = this->map_model->viewMode() == MapViewMode::TwoD
        ? qMax(1e-9, this->map_model->view2dContinuousScale())
        : 1.0;
    const double rendered_tile_size = MapModel::TileSize * view_scale;
    const int foreground_tiles_x =
        int(std::ceil(viewport_size.width() / rendered_tile_size)) + 4;
    const int foreground_tiles_y =
        int(std::ceil(viewport_size.height() / rendered_tile_size)) + 4;
    int tiles_x = foreground_tiles_x + TwoDPanRetentionMarginTiles * 2;
    int tiles_y = foreground_tiles_y + TwoDPanRetentionMarginTiles * 2;
    if (this->map_model->viewMode() == MapViewMode::ThreeD)
    {
        // Keep the existing 3D apron for horizon coverage and request
        // retention. Terrain mesh detail is positioned independently below.
        tiles_x = foreground_tiles_x * 2 + 4;
        tiles_y = foreground_tiles_y * 3 + 6;
    }
    const int center_tile_x = int(std::floor(rendered_center_x));
    const int center_tile_y = int(std::floor(center.y()));
    const int start_x = center_tile_x - tiles_x / 2;
    const int start_y = center_tile_y - tiles_y / 2;
    const int foreground_start_x = center_tile_x - foreground_tiles_x / 2;
    const int foreground_start_y = center_tile_y - foreground_tiles_y / 2;
    const bool relief_enabled = reliefTerrainEnabled(*this->map_model, this->terrain_repository);

    const int terrain_zoom = relief_enabled
        ? terrainZoomForImageryZoom(imagery_zoom)
        : 0;
    const int terrain_zoom_delta = relief_enabled
        ? imagery_zoom - terrain_zoom
        : 0;
    const QString imagery_key_prefix =
        this->map_model->tileCachePrefix(imagery_zoom);
    const bool imagery_enabled = this->map_model->viewMode() != MapViewMode::ThreeD
        || this->map_visible;

    // The retained tile window is deliberately larger than the foreground.
    // While the foreground still fits, do not create a new request batch or
    // rebuild geometry merely because the center crossed an XYZ boundary.
    // All imagery in the retained window was already requested; only terrain
    // that has newly become foreground may need to be requested here. This
    // also avoids repeatedly reprioritizing/scanning a large request queue
    // during continuous panning.
    const bool layout_origin_matches =
        std::abs(origin_world.x() - this->layout_origin_world.x()) < 0.5
        && std::abs(origin_world.y() - this->layout_origin_world.y()) < 0.5;
    const bool current_layout_covers_foreground =
        !this->layout_dirty
        && layout_origin_matches
        && currentLayoutCoversForeground(
            imagery_zoom, foreground_start_x, foreground_start_y,
            foreground_tiles_x, foreground_tiles_y, tile_count,
            imagery_key_prefix);
    bool terrain_lod_matches =
        !relief_enabled || currentTerrainLodMatches(viewport_size);

    // See MinimumTerrainLodRebuildIntervalMs above: without this gate, a
    // mismatch here forced a full DEM resample of the whole apron on
    // essentially every frame while the 3D camera was being manipulated,
    // which was the dominant cost behind poor 3D frame times. Defer the
    // rebuild instead of performing it immediately; the retained mesh keeps
    // drawing in the meantime and the next frame past the interval catches
    // up.
    if (!terrain_lod_matches && current_layout_covers_foreground
        && this->terrain_lod_rebuild_clock.isValid()
        && this->terrain_lod_rebuild_clock.elapsed() < MinimumTerrainLodRebuildIntervalMs)
    {
        terrain_lod_matches = true;
    }

    if (current_layout_covers_foreground && terrain_lod_matches)
    {
        if (relief_enabled && this->terrain_repository != nullptr)
        {
            const int foreground_end_x = foreground_start_x + foreground_tiles_x;
            const int foreground_end_y = foreground_start_y + foreground_tiles_y;
            for (const VisibleTile &tile : this->visible_tiles)
            {
                const bool terrain_needed =
                    (tile.virtual_x >= foreground_start_x
                     && tile.virtual_x < foreground_end_x
                     && tile.y >= foreground_start_y
                     && tile.y < foreground_end_y)
                    || tile.terrain_cell_count > TerrainBackgroundRequestMinimumLodCellCount;
                if (!terrain_needed
                    || tile.terrain_key.isEmpty()
                    || this->terrain_repository->tile(tile.terrain_key) != nullptr)
                {
                    continue;
                }

                MapTerrainTileAddress terrain_address;
                terrain_address.zoom = tile.terrain_zoom;
                terrain_address.x = quint32(tile.tile_x) >> terrain_zoom_delta;
                terrain_address.y = quint32(tile.y) >> terrain_zoom_delta;
                this->terrain_repository->requestTile(
                    terrainDatasetId(), terrain_address.zoom,
                    terrain_address.x, terrain_address.y);
            }
        }
        return true;
    }

    const bool terrain_lod_only_rebuild =
        current_layout_covers_foreground && !terrain_lod_matches;
    const QString request_layout_key = QStringLiteral("%1|%2|%3|%4|%5")
        .arg(imagery_key_prefix)
        .arg(start_x)
        .arg(start_y)
        .arg(tiles_x)
        .arg(tiles_y);
    const quint64 request_batch = imagery_enabled && !terrain_lod_only_rebuild
        ? this->tile_repository->beginTileRequestBatch(this, request_layout_key)
        : 0;

    QVector<VisibleTile> next_tiles;
    if (terrain_lod_only_rebuild)
    {
        // LOD changes must not recenter the retained imagery apron. Reuse the
        // exact current tile positions and rebuild only their terrain meshes.
        next_tiles = this->visible_tiles;
        for (VisibleTile &tile : next_tiles)
        {
            tile.foreground =
                tile.virtual_x >= foreground_start_x
                && tile.virtual_x < foreground_start_x + foreground_tiles_x
                && tile.y >= foreground_start_y
                && tile.y < foreground_start_y + foreground_tiles_y;
            tile.terrain_cell_count =
                terrainCellCountForTile(tile, viewport_size);

            if (this->terrain_repository == nullptr
                || tile.terrain_key.isEmpty()
                || (!tile.foreground
                    && tile.terrain_cell_count <= TerrainBackgroundRequestMinimumLodCellCount)
                || this->terrain_repository->tile(tile.terrain_key) != nullptr)
            {
                continue;
            }

            MapTerrainTileAddress terrain_address;
            terrain_address.zoom = tile.terrain_zoom;
            terrain_address.x = quint32(tile.tile_x) >> terrain_zoom_delta;
            terrain_address.y = quint32(tile.y) >> terrain_zoom_delta;
            this->terrain_repository->requestTile(
                terrainDatasetId(), terrain_address.zoom,
                terrain_address.x, terrain_address.y);
        }
    }
    else
    {
        next_tiles.reserve(tiles_x * tiles_y);
        for (int delta_x = 0; delta_x < tiles_x; ++delta_x)
        {
            for (int delta_y = 0; delta_y < tiles_y; ++delta_y)
            {
                const int virtual_x = start_x + delta_x;
                const int y = start_y + delta_y;
                if (y < 0 || y >= tile_count)
                    continue;

                const int tile_x = GeoWebMercator::wrapTileX(virtual_x, imagery_zoom);
                VisibleTile tile;
                tile.imagery_key = this->map_model->tileCacheKey(tile_x, y);
                tile.virtual_x = virtual_x;
                tile.tile_x = tile_x;
                tile.y = y;
                tile.imagery_zoom = imagery_zoom;
                tile.terrain_zoom = terrain_zoom;
                tile.foreground =
                    virtual_x >= foreground_start_x
                    && virtual_x < foreground_start_x + foreground_tiles_x
                    && y >= foreground_start_y
                    && y < foreground_start_y + foreground_tiles_y;
                tile.terrain_cell_count = relief_enabled
                    ? terrainCellCountForTile(tile, viewport_size)
                    : 0;

                if (relief_enabled)
                {
                    MapTerrainTileAddress terrain_address;
                    terrain_address.zoom = terrain_zoom;
                    terrain_address.x = quint32(tile_x) >> terrain_zoom_delta;
                    terrain_address.y = quint32(y) >> terrain_zoom_delta;
                    tile.terrain_key = mapTerrainTileKey(
                        terrainDatasetId(), terrain_address);
                    if (tile.foreground
                        || tile.terrain_cell_count > TerrainBackgroundRequestMinimumLodCellCount)
                    {
                        this->terrain_repository->requestTile(
                            terrainDatasetId(), terrain_address.zoom,
                            terrain_address.x, terrain_address.y);
                    }
                }
                next_tiles.append(tile);

                if (imagery_enabled
                    && this->tile_repository->tile(tile.imagery_key) == nullptr)
                {
                    const int priority_x = virtual_x - center_tile_x;
                    const int priority_y = y - center_tile_y;
                    const int priority =
                        priority_x * priority_x + priority_y * priority_y;
                    this->tile_repository->requestTile(
                        this->map_model->tileEndpoint(tile_x, y),
                        tile.imagery_key, tile_x, y, priority,
                        request_batch, tile.foreground);
                }
            }
        }
    }

    // Retain old coverage while replacement data arrives. Provider changes are
    // deliberately progressive per XYZ position: a newly selected provider tile
    // replaces the old provider tile immediately when it becomes ready. This is
    // different from same-provider panning, where keeping the retained foreground
    // together avoids visible holes.
    //
    // Zoom LOD handoffs remain progressive per parent/child group. On zoom-in a
    // parent is replaced only when all four direct children are ready. On zoom-out
    // each ready parent immediately replaces its corresponding old children.
    //
    // Mixed-source and mixed-LOD geometry is safe because every tile is expressed
    // in the common ReferenceZoom world coordinate system.
    const bool retained_layer_transition = !this->visible_tiles.isEmpty()
        && layout_origin_matches;
    if (imagery_enabled && !terrain_lod_only_rebuild
        && retained_layer_transition)
    {
        bool same_lod = true;
        bool provider_transition = false;
        for (const VisibleTile &tile : this->visible_tiles)
        {
            if (tile.imagery_zoom != imagery_zoom)
            {
                same_lod = false;
                break;
            }
            if (!tile.imagery_key.startsWith(imagery_key_prefix))
                provider_transition = true;
        }

        if (same_lod && provider_transition)
        {
            const QVector<VisibleTile> progressive_tiles = progressiveProviderLayout(
                next_tiles, imagery_zoom, imagery_key_prefix, relief_enabled);
            if (progressive_tiles.isEmpty())
                return true;
            next_tiles = progressive_tiles;
        }
        else
        {
            bool foreground_ready = true;
            for (const VisibleTile &tile : next_tiles)
            {
                if (!tile.foreground)
                    continue;
                if (!tileReadyForZoomHandoff(tile, relief_enabled))
                {
                    foreground_ready = false;
                    break;
                }
            }

            if (!foreground_ready)
            {
                const QVector<VisibleTile> progressive_tiles = progressiveZoomLayout(
                    next_tiles, imagery_zoom, relief_enabled);
                if (progressive_tiles.isEmpty())
                {
                    // Same-provider, same-LOD pan/recenter, unsupported multi-level
                    // zoom jump, or no replaceable zoom group yet: keep drawing
                    // the retained layer.
                    return true;
                }
                next_tiles = progressive_tiles;
            }
        }
    }

    bool same_layout = !this->layout_dirty
        && next_tiles.size() == this->visible_tiles.size();
    if (same_layout)
    {
        for (qsizetype index = 0; index < next_tiles.size(); ++index)
        {
            if (!(next_tiles.at(index) == this->visible_tiles.at(index)))
            {
                same_layout = false;
                break;
            }
        }
    }

    if (same_layout)
        return true;

    // Reuse already-computed vertex data for tiles whose position, imagery
    // source, and terrain LOD/content are all unchanged from the previously
    // built mesh. Both the debounced LOD rebuild above and the progressive
    // zoom/provider handoff below advance in many small steps while imagery
    // or terrain streams in -- on any single step, only a handful of tiles
    // in the retained apron are actually newly promoted or re-leveled, the
    // rest are byte-for-byte the same tile as last frame. Without this, every
    // such step still re-resampled the DEM and rebuilt the mesh for the
    // ENTIRE apron (up to thousands of tiles) instead of just the tiles that
    // changed, which is what produced a hitch on every incoming terrain or
    // imagery tile while a zoom transition settled.
    //
    // Position (not tile identity/content) is the reuse key because a
    // retained tile from progressiveZoomLayout/progressiveProviderLayout is
    // the same VisibleTile value as before at the same (virtual_x, y) slot;
    // matching by position and then verifying its content is unchanged is
    // both sufficient and cheap. Reuse requires the world origin to be
    // unchanged too, since tile screen positions are computed relative to
    // it, and is skipped entirely right after an explicit invalidate() (map
    // visibility toggled, terrain/tile repository swapped, RHI reset, or a
    // detected mesh-size anomaly) so a structural reset always re-reads
    // current repository state instead of trusting old vertex data.
    QVector<TileVertex> previous_vertices;
    QHash<quint64, qsizetype> previous_tiles_by_position;
    const bool positions_reusable =
        !this->layout_dirty && layout_origin_matches && !this->vertices.isEmpty();
    if (positions_reusable)
    {
        previous_vertices = std::move(this->vertices);
        previous_tiles_by_position.reserve(this->visible_tiles.size());
        for (qsizetype index = 0; index < this->visible_tiles.size(); ++index)
        {
            const VisibleTile &tile = this->visible_tiles.at(index);
            previous_tiles_by_position.insert(
                tilePositionKey(tile.virtual_x, tile.y), index);
        }
    }

    this->vertices.clear();
    // A relief (terrain) tile contributes cell_count^2 * 6 vertices, not the
    // flat-tile constant of 6 -- near/focus tiles can reach 64 cells per side
    // (24576 vertices). Reserving as if every tile were flat left this
    // QVector to grow via repeated doubling reallocations (each copying
    // everything appended so far) whenever relief tiles were present, on top
    // of the DEM resampling cost itself. Estimate the real vertex count up
    // front so the buffer is sized in one allocation.
    qsizetype estimated_vertex_count = 0;
    for (const VisibleTile &tile : next_tiles)
    {
        const bool tile_has_relief_mesh = relief_enabled
            && !tile.terrain_key.isEmpty()
            && tile.imagery_zoom >= tile.terrain_zoom;
        if (tile_has_relief_mesh)
        {
            const qsizetype cell_count = qMax(1, tile.terrain_cell_count);
            estimated_vertex_count += cell_count * cell_count * 6;
        }
        else
        {
            estimated_vertex_count += 6;
        }
    }
    this->vertices.reserve(estimated_vertex_count);
    for (VisibleTile &tile : next_tiles)
    {
        const double visible_tile_reference_size = MapModel::TileSize
            * std::pow(2.0, MapRenderCacheMath::ReferenceZoom - tile.imagery_zoom);
        const float left = float(
            tile.virtual_x * visible_tile_reference_size - origin_world.x());
        const float top = float(
            tile.y * visible_tile_reference_size - origin_world.y());
        const float right = float(left + visible_tile_reference_size);
        const float bottom = float(top + visible_tile_reference_size);
        tile.first_vertex = this->vertices.size();

        bool reused = false;
        if (positions_reusable)
        {
            const QHash<quint64, qsizetype>::const_iterator previous_iterator =
                previous_tiles_by_position.constFind(
                    tilePositionKey(tile.virtual_x, tile.y));
            if (previous_iterator != previous_tiles_by_position.cend())
            {
                const VisibleTile &previous_tile =
                    this->visible_tiles.at(previous_iterator.value());
                const bool terrain_data_dirty = !tile.terrain_key.isEmpty()
                    && this->dirty_terrain_keys.contains(tile.terrain_key);
                if (!terrain_data_dirty
                    && previous_tile.vertex_count > 0
                    && previous_tile.imagery_key == tile.imagery_key
                    && previous_tile.terrain_key == tile.terrain_key
                    && previous_tile.terrain_cell_count == tile.terrain_cell_count)
                {
                    const TileVertex *source = previous_vertices.constData()
                        + previous_tile.first_vertex;
                    for (int index = 0; index < previous_tile.vertex_count; ++index)
                        this->vertices.append(source[index]);
                    tile.vertex_count = previous_tile.vertex_count;
                    reused = true;
                }
            }
        }

        bool relief_built = false;
        if (!reused && relief_enabled && !tile.terrain_key.isEmpty()
            && this->terrain_repository != nullptr)
        {
            const MapTerrainTile *terrain_tile = this->terrain_repository->tile(tile.terrain_key);
            // Build the full relief grid even before its DEM arrives. A flat
            // grid with identical vertex count lets a later terrain response
            // patch only this tile's vertex range instead of rebuilding the
            // entire basemap mesh.
            relief_built = appendReliefTileVertices(
                &this->vertices, &tile, terrain_tile,
                left, top, float(visible_tile_reference_size));
        }

        if (!reused && !relief_built)
            appendFlatTileVertices(&this->vertices, &tile, left, top, right, bottom);
    }

    this->visible_tiles = next_tiles;
    this->layout_origin_world = origin_world;
    this->vertex_upload_pending = true;
    this->wireframe_vertex_upload_pending = true;
    this->layout_dirty = false;
    this->terrain_lod_rebuild_clock.restart();
    return true;
}

bool MapRhiBasemapRenderer::tileReadyForZoomHandoff(
    const VisibleTile &tile, bool relief_enabled) const
{
    if (this->tile_repository == nullptr)
        return false;

    const QPixmap *pixmap = this->tile_repository->tile(tile.imagery_key);
    if (pixmap == nullptr || pixmap->isNull())
        return false;

    if (relief_enabled && !tile.terrain_key.isEmpty())
    {
        if (this->terrain_repository == nullptr
            || this->terrain_repository->tile(tile.terrain_key) == nullptr)
        {
            return false;
        }
    }

    return true;
}

QVector<MapRhiBasemapRenderer::VisibleTile>
MapRhiBasemapRenderer::progressiveProviderLayout(
    const QVector<VisibleTile> &target_tiles, int target_zoom,
    const QString &imagery_key_prefix, bool relief_enabled) const
{
    QVector<VisibleTile> result;
    if (target_tiles.isEmpty() || this->visible_tiles.isEmpty())
        return result;

    QHash<quint64, qsizetype> current_by_position;
    current_by_position.reserve(this->visible_tiles.size());
    for (qsizetype index = 0; index < this->visible_tiles.size(); ++index)
    {
        const VisibleTile &tile = this->visible_tiles.at(index);
        if (tile.imagery_zoom != target_zoom)
            return QVector<VisibleTile>();
        current_by_position.insert(
            tilePositionKey(tile.virtual_x, tile.y), index);
    }

    result.reserve(target_tiles.size());
    bool changed = false;
    for (const VisibleTile &target : target_tiles)
    {
        const quint64 position_key = tilePositionKey(target.virtual_x, target.y);
        const QHash<quint64, qsizetype>::const_iterator current_iterator =
            current_by_position.constFind(position_key);

        if (tileReadyForZoomHandoff(target, relief_enabled))
        {
            result.append(target);
            if (current_iterator == current_by_position.cend()
                || this->visible_tiles.at(current_iterator.value()).imagery_key
                    != target.imagery_key)
            {
                changed = true;
            }
            continue;
        }

        if (current_iterator == current_by_position.cend())
            continue;

        VisibleTile retained = this->visible_tiles.at(current_iterator.value());
        retained.foreground = target.foreground;
        retained.terrain_cell_count = target.terrain_cell_count;

        // Once a position already uses the new source, keep that exact target
        // entry. In normal operation it is also ready; this branch mainly keeps
        // terrain readiness changes from ever reverting a provider handoff.
        if (retained.imagery_key.startsWith(imagery_key_prefix))
        {
            result.append(target);
            continue;
        }

        result.append(retained);
    }

    if (!changed)
        result.clear();
    return result;
}

QVector<MapRhiBasemapRenderer::VisibleTile> MapRhiBasemapRenderer::progressiveZoomLayout(
    const QVector<VisibleTile> &target_tiles, int target_zoom,
    bool relief_enabled) const
{
    QVector<VisibleTile> result;
    if (target_tiles.isEmpty() || this->visible_tiles.isEmpty())
        return result;

    bool has_parent_source = false;
    bool has_child_source = false;
    bool unsupported_zoom = false;
    for (const VisibleTile &tile : this->visible_tiles)
    {
        if (tile.imagery_zoom == target_zoom)
            continue;
        if (tile.imagery_zoom == target_zoom - 1)
            has_parent_source = true;
        else if (tile.imagery_zoom == target_zoom + 1)
            has_child_source = true;
        else
            unsupported_zoom = true;
    }

    if (unsupported_zoom || (has_parent_source && has_child_source)
        || (!has_parent_source && !has_child_source))
    {
        return result;
    }

    QHash<quint64, qsizetype> target_by_position;
    target_by_position.reserve(target_tiles.size());
    for (qsizetype index = 0; index < target_tiles.size(); ++index)
    {
        const VisibleTile &tile = target_tiles.at(index);
        target_by_position.insert(tilePositionKey(tile.virtual_x, tile.y), index);
    }

    if (has_parent_source)
    {
        QHash<quint64, qsizetype> current_parents;
        QSet<quint64> appended_targets;
        current_parents.reserve(this->visible_tiles.size());
        appended_targets.reserve(target_tiles.size());
        result.reserve(this->visible_tiles.size() + 16);
        bool changed = false;

        // Keep child groups that were already promoted by an earlier frame.
        for (const VisibleTile &tile : this->visible_tiles)
        {
            if (tile.imagery_zoom != target_zoom)
                continue;

            const quint64 position_key = tilePositionKey(tile.virtual_x, tile.y);
            const QHash<quint64, qsizetype>::const_iterator target_iterator =
                target_by_position.constFind(position_key);
            if (target_iterator == target_by_position.cend())
                continue;

            result.append(target_tiles.at(target_iterator.value()));
            appended_targets.insert(position_key);
        }

        for (qsizetype index = 0; index < this->visible_tiles.size(); ++index)
        {
            const VisibleTile &parent = this->visible_tiles.at(index);
            if (parent.imagery_zoom != target_zoom - 1)
                continue;
            current_parents.insert(tilePositionKey(parent.virtual_x, parent.y), index);

            qsizetype child_indices[4] = {-1, -1, -1, -1};
            bool complete_child_group = true;
            bool replacement_ready = true;
            bool replacement_foreground = false;
            int replacement_terrain_cell_count = 0;
            bool has_target_child = false;
            int child_index = 0;
            for (int child_y = 0; child_y < 2; ++child_y)
            {
                for (int child_x = 0; child_x < 2; ++child_x)
                {
                    const int virtual_x = parent.virtual_x * 2 + child_x;
                    const int y = parent.y * 2 + child_y;
                    const quint64 child_key = tilePositionKey(virtual_x, y);
                    const QHash<quint64, qsizetype>::const_iterator target_iterator =
                        target_by_position.constFind(child_key);
                    if (target_iterator == target_by_position.cend())
                    {
                        complete_child_group = false;
                        replacement_ready = false;
                    }
                    else
                    {
                        has_target_child = true;
                        child_indices[child_index] = target_iterator.value();
                        const VisibleTile &child = target_tiles.at(target_iterator.value());
                        replacement_foreground = replacement_foreground || child.foreground;
                        replacement_terrain_cell_count = qMax(
                            replacement_terrain_cell_count,
                            child.terrain_cell_count);
                        if (!tileReadyForZoomHandoff(child, relief_enabled))
                            replacement_ready = false;
                    }
                    ++child_index;
                }
            }

            if (complete_child_group && replacement_ready)
            {
                for (int index_in_group = 0; index_in_group < 4; ++index_in_group)
                {
                    const VisibleTile &child = target_tiles.at(child_indices[index_in_group]);
                    const quint64 child_key = tilePositionKey(child.virtual_x, child.y);
                    if (appended_targets.contains(child_key))
                        continue;
                    result.append(child);
                    appended_targets.insert(child_key);
                }
                changed = true;
                continue;
            }

            if (!has_target_child)
            {
                changed = true;
                continue;
            }

            VisibleTile retained_parent = parent;
            retained_parent.foreground = replacement_foreground;
            retained_parent.terrain_cell_count = replacement_terrain_cell_count;
            result.append(retained_parent);
        }

        // A newly exposed target area may not have a retained parent in the old
        // apron. Add any ready target tile there rather than leaving a hole.
        for (const VisibleTile &tile : target_tiles)
        {
            const quint64 position_key = tilePositionKey(tile.virtual_x, tile.y);
            if (appended_targets.contains(position_key))
                continue;

            const quint64 parent_key = tilePositionKey(
                parentVirtualTileX(tile.virtual_x), tile.y / 2);
            if (current_parents.contains(parent_key))
                continue;
            if (!tileReadyForZoomHandoff(tile, relief_enabled))
                continue;

            result.append(tile);
            appended_targets.insert(position_key);
            changed = true;
        }

        if (!changed)
            result.clear();
        return result;
    }

    QHash<quint64, qsizetype> current_targets;
    QHash<quint64, qsizetype> current_children;
    current_targets.reserve(this->visible_tiles.size());
    current_children.reserve(this->visible_tiles.size());
    for (qsizetype index = 0; index < this->visible_tiles.size(); ++index)
    {
        const VisibleTile &tile = this->visible_tiles.at(index);
        const quint64 position_key = tilePositionKey(tile.virtual_x, tile.y);
        if (tile.imagery_zoom == target_zoom)
            current_targets.insert(position_key, index);
        else if (tile.imagery_zoom == target_zoom + 1)
            current_children.insert(position_key, index);
    }

    result.reserve(this->visible_tiles.size() + 16);
    bool changed = false;
    for (const VisibleTile &target : target_tiles)
    {
        const quint64 target_key = tilePositionKey(target.virtual_x, target.y);
        if (current_targets.contains(target_key))
        {
            result.append(target);
            continue;
        }

        if (tileReadyForZoomHandoff(target, relief_enabled))
        {
            result.append(target);
            changed = true;
            continue;
        }

        for (int child_y = 0; child_y < 2; ++child_y)
        {
            for (int child_x = 0; child_x < 2; ++child_x)
            {
                const quint64 child_key = tilePositionKey(
                    target.virtual_x * 2 + child_x,
                    target.y * 2 + child_y);
                const QHash<quint64, qsizetype>::const_iterator child_iterator =
                    current_children.constFind(child_key);
                if (child_iterator == current_children.cend())
                    continue;

                VisibleTile retained_child = this->visible_tiles.at(child_iterator.value());
                retained_child.foreground = target.foreground;
                retained_child.terrain_cell_count = target.terrain_cell_count;
                result.append(retained_child);
            }
        }
    }

    if (!changed)
        result.clear();
    return result;
}

bool MapRhiBasemapRenderer::currentLayoutCoversForeground(
    int imagery_zoom, int foreground_start_x, int foreground_start_y,
    int foreground_tiles_x, int foreground_tiles_y, int tile_count,
    const QString &imagery_key_prefix) const
{
    if (this->visible_tiles.isEmpty())
        return false;

    QSet<quint64> target_zoom_positions;
    target_zoom_positions.reserve(this->visible_tiles.size());
    for (const VisibleTile &tile : this->visible_tiles)
    {
        if (tile.imagery_zoom != imagery_zoom
            || !tile.imagery_key.startsWith(imagery_key_prefix))
        {
            return false;
        }
        target_zoom_positions.insert(tilePositionKey(tile.virtual_x, tile.y));
    }

    const int required_minimum_y = qMax(0, foreground_start_y);
    const int required_maximum_y = qMin(
        tile_count - 1, foreground_start_y + foreground_tiles_y - 1);
    for (int virtual_x = foreground_start_x;
         virtual_x < foreground_start_x + foreground_tiles_x; ++virtual_x)
    {
        for (int y = required_minimum_y; y <= required_maximum_y; ++y)
        {
            if (!target_zoom_positions.contains(tilePositionKey(virtual_x, y)))
                return false;
        }
    }

    return true;
}

int MapRhiBasemapRenderer::terrainCellCountForTile(
    const VisibleTile &tile, const QSize &viewport_size) const
{
    if (this->map_model == nullptr
        || tile.imagery_zoom < tile.terrain_zoom
        || !viewport_size.isValid())
    {
        return 1;
    }

    const int zoom_delta = tile.imagery_zoom - tile.terrain_zoom;
    const int cell_divisor = 1 << qMin(zoom_delta, 6);
    const int native_cell_count = qMax(
        1, MapTerrainTileCellCount / cell_divisor);

    const int maximum_cell_count = native_cell_count;
    const int minimum_cell_count = qMin(
        maximum_cell_count, TerrainMinimumLodCellCount);
    if (maximum_cell_count <= minimum_cell_count)
        return maximum_cell_count;

    const double tile_reference_size = MapModel::TileSize
        * std::pow(
            2.0,
            MapRenderCacheMath::ReferenceZoom - tile.imagery_zoom);
    const int tile_count = 1 << tile.imagery_zoom;
    const double raw_focus_tile_x = GeoWebMercator::lonToTileX(
        this->map_model->centerLon(), tile.imagery_zoom);
    const double focus_wrap_offset = std::round(
        (double(tile.virtual_x) + 0.5 - raw_focus_tile_x)
        / double(qMax(1, tile_count))) * double(qMax(1, tile_count));
    const double focus_tile_x = raw_focus_tile_x + focus_wrap_offset;
    const double focus_tile_y = GeoWebMercator::latToTileY(
        this->map_model->centerLat(), tile.imagery_zoom);
    const double delta_x_world =
        (double(tile.virtual_x) + 0.5 - focus_tile_x)
        * tile_reference_size;
    const double delta_y_world =
        (double(tile.y) + 0.5 - focus_tile_y)
        * tile_reference_size;
    const double ground_distance_from_focus_world = std::hypot(
        delta_x_world, delta_y_world);

    const double base_scale = GeoWebMercator::zoomScale(
        tile.imagery_zoom, MapRenderCacheMath::ReferenceZoom);
    const double safe_scale = qMax(1e-12, base_scale);
    const double half_height_world =
        double(qMax(1, viewport_size.height())) / (2.0 * safe_scale);
    const double native_camera_distance_world = half_height_world
        / std::tan(qDegreesToRadians(45.0 / 2.0));
    double camera_distance_world =
        this->map_model->view3dCameraDistanceWorld();
    if (!std::isfinite(camera_distance_world)
        || camera_distance_world <= 0.0)
    {
        camera_distance_world = native_camera_distance_world;
    }

    // Drive terrain LOD from projected screen size instead of multiplying a
    // discrete imagery-zoom cap by the native terrain resolution. The latter
    // made a 17 -> 16 tile-zoom handoff increase both factors at once, causing
    // a visible jump back toward maximum mesh density.
    //
    // Estimate the distance from the actual camera to this tile center. At the
    // crosshair this is the orbit distance; toward/away from the camera the LOD
    // then follows perspective naturally.
    const double pitch_rad = qDegreesToRadians(qBound(
        MapModel::MinView3dPitchDeg,
        this->map_model->view3dPitchDeg(),
        MapModel::MaxView3dPitchDeg));
    const double yaw_rad = qDegreesToRadians(
        this->map_model->view3dYawDeg());
    const double horizontal_camera_distance_world =
        camera_distance_world * std::cos(pitch_rad);
    const double camera_offset_x_world =
        std::sin(yaw_rad) * horizontal_camera_distance_world;
    const double camera_offset_y_world =
        std::cos(yaw_rad) * horizontal_camera_distance_world;
    const double camera_height_world =
        camera_distance_world * std::sin(pitch_rad)
        + this->map_model->view3dCameraCollisionLiftWorld();
    const double camera_to_tile_distance_world = std::sqrt(
        std::pow(delta_x_world - camera_offset_x_world, 2.0)
        + std::pow(delta_y_world - camera_offset_y_world, 2.0)
        + camera_height_world * camera_height_world);
    const double camera_to_focus_distance_world = std::sqrt(
        horizontal_camera_distance_world * horizontal_camera_distance_world
        + camera_height_world * camera_height_world);
    const double focus_falloff_distance_world = std::hypot(
        camera_to_focus_distance_world, ground_distance_from_focus_world);

    // Keep the crosshair/focus as the highest-detail location. True camera
    // distance may lower detail further, but a tile merely being underneath
    // an oblique camera must never become more detailed than the focus target.
    const double lod_distance_world = qMax(
        camera_to_tile_distance_world, focus_falloff_distance_world);

    // A tile at the native camera distance is split into about eight cells
    // across a 256 px tile. The desired count is screen-space based instead of
    // native-resolution based, so switching imagery tile zoom cannot multiply
    // the LOD and spike the mesh. Pulling back, zooming out, or moving farther
    // from the focus/camera progressively lowers the mesh density.
    const double projected_tile_scale = qBound(
        0.0,
        native_camera_distance_world
            / qMax(1e-9, lod_distance_world),
        4.0);
    const double desired_cell_count =
        (double(MapModel::TileSize) / TerrainTargetCellSizePixels)
        * projected_tile_scale;

    // Meshes change only in powers of two. Geometric-mean thresholds avoid
    // immediately dropping a 64-cell focus tile to 32 for tiny movements.
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

bool MapRhiBasemapRenderer::currentTerrainLodMatches(
    const QSize &viewport_size) const
{
    for (const VisibleTile &tile : this->visible_tiles)
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

bool MapRhiBasemapRenderer::updateDirtyTerrainTiles(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (this->dirty_terrain_keys.isEmpty() || resource_updates == nullptr
        || this->vertex_buffer == nullptr || this->terrain_repository == nullptr)
    {
        return true;
    }

    const QSet<QString> dirty_keys = this->dirty_terrain_keys;
    this->dirty_terrain_keys.clear();
    bool wireframe_changed = false;

    for (VisibleTile &tile : this->visible_tiles)
    {
        if (tile.terrain_key.isEmpty() || !dirty_keys.contains(tile.terrain_key))
            continue;

        const MapTerrainTile *terrain_tile = this->terrain_repository->tile(tile.terrain_key);
        if (terrain_tile == nullptr)
            continue;

        const double tile_reference_size = MapModel::TileSize
            * std::pow(2.0, MapRenderCacheMath::ReferenceZoom - tile.imagery_zoom);
        const float left = float(
            tile.virtual_x * tile_reference_size - this->layout_origin_world.x());
        const float top = float(
            tile.y * tile_reference_size - this->layout_origin_world.y());

        QVector<TileVertex> replacement;
        replacement.reserve(tile.vertex_count);
        VisibleTile replacement_tile = tile;
        replacement_tile.first_vertex = 0;
        if (!appendReliefTileVertices(
                &replacement, &replacement_tile, terrain_tile,
                left, top, float(tile_reference_size))
            || replacement.size() != tile.vertex_count)
        {
            // The mesh density changed unexpectedly. Fall back to a complete
            // rebuild on the next frame rather than corrupting vertex ranges.
            this->layout_dirty = true;
            this->vertex_upload_pending = true;
            return true;
        }

        const qsizetype first_vertex = tile.first_vertex;
        for (qsizetype index = 0; index < replacement.size(); ++index)
            this->vertices[first_vertex + index] = replacement.at(index);

        const int byte_offset = int(first_vertex * qsizetype(sizeof(TileVertex)));
        const int byte_count = boundedBufferSize(
            replacement.size(), qsizetype(sizeof(TileVertex)));
        if (byte_count <= 0)
            return false;

        resource_updates->updateDynamicBuffer(
            this->vertex_buffer.get(), byte_offset, byte_count,
            replacement.constData());
        wireframe_changed = true;
    }

    if (wireframe_changed)
        this->wireframe_vertex_upload_pending = true;
    return true;
}

bool MapRhiBasemapRenderer::ensureTileResource(
    const VisibleTile &tile, TileResource **resource,
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource == nullptr)
        return false;
    *resource = nullptr;

    const QString &key = tile.imagery_key;
    std::map<QString, std::unique_ptr<TileResource>>::iterator iterator =
        this->tile_resources.find(key);

    const QPixmap *pixmap = this->tile_repository != nullptr
        ? this->tile_repository->tile(key)
        : nullptr;
    if (pixmap == nullptr || pixmap->isNull())
    {
        if (iterator != this->tile_resources.end()
            && iterator->second->texture
            && iterator->second->bindings)
        {
            if (!ensureHeatmapTexture(tile, iterator->second.get(), resource_updates))
                return false;
            *resource = iterator->second.get();
        }
        return true;
    }

    if (iterator == this->tile_resources.end())
    {
        std::unique_ptr<TileResource> created = std::make_unique<TileResource>();
        std::pair<std::map<QString, std::unique_ptr<TileResource>>::iterator, bool> inserted =
            this->tile_resources.emplace(key, std::move(created));
        iterator = inserted.first;
    }

    TileResource *tile_resource = iterator->second.get();
    const qint64 cache_key = pixmap->cacheKey();
    if (!tile_resource->texture || tile_resource->pixmap_cache_key != cache_key)
    {
        QImage image = pixmap->toImage().convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull())
            return true;

        tile_resource->bindings.reset();
        tile_resource->texture.reset(this->rhi->newTexture(
            QRhiTexture::RGBA8, image.size()));
        if (!tile_resource->texture || !tile_resource->texture->create())
            return false;

        resource_updates->uploadTexture(tile_resource->texture.get(), image);
        tile_resource->pixmap_cache_key = cache_key;
    }

    if (!ensureHeatmapTexture(tile, tile_resource, resource_updates))
        return false;
    if (!tile_resource->bindings && !rebuildTileBindings(tile_resource))
        return false;

    *resource = tile_resource;
    return true;
}

bool MapRhiBasemapRenderer::ensureHeatmapTexture(
    const VisibleTile &tile, TileResource *resource,
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource == nullptr || resource_updates == nullptr)
        return false;
    if (resource->heatmap_revision == this->heatmap_revision)
        return resource->bindings != nullptr || rebuildTileBindings(resource);

    QImage image = renderHeatmapTile(tile);
    bool bindings_changed = false;
    if (!image.isNull())
    {
        if (!resource->heatmap_texture)
        {
            resource->heatmap_texture.reset(this->rhi->newTexture(
                QRhiTexture::RGBA8, image.size()));
            if (!resource->heatmap_texture || !resource->heatmap_texture->create())
                return false;
            bindings_changed = true;
        }
        resource_updates->uploadTexture(resource->heatmap_texture.get(), image);
    }
    else if (resource->heatmap_texture)
    {
        image = QImage(
            HeatmapTextureSize, HeatmapTextureSize,
            QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        resource_updates->uploadTexture(resource->heatmap_texture.get(), image);
    }

    resource->heatmap_revision = this->heatmap_revision;
    if (bindings_changed || resource->bindings == nullptr)
        return rebuildTileBindings(resource);
    return true;
}

bool MapRhiBasemapRenderer::rebuildTileBindings(TileResource *resource)
{
    if (resource == nullptr || !resource->texture || !this->dummy_texture
        || !this->sampler || this->camera_uniform_buffer == nullptr)
    {
        return false;
    }

    QRhiTexture *heatmap_texture = resource->heatmap_texture
        ? resource->heatmap_texture.get()
        : this->dummy_texture.get();

    resource->bindings.reset(this->rhi->newShaderResourceBindings());
    if (!resource->bindings)
        return false;
    resource->bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage
                | QRhiShaderResourceBinding::FragmentStage,
            this->camera_uniform_buffer),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage,
            resource->texture.get(), this->sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(
            2, QRhiShaderResourceBinding::FragmentStage,
            heatmap_texture, this->sampler.get())
    });
    return resource->bindings->create();
}

QImage MapRhiBasemapRenderer::renderHeatmapTile(const VisibleTile &tile) const
{
    if (this->heatmap_markers.isEmpty()
        || !(this->heatmap_radius_world > 0.0)
        || tile.imagery_zoom < 0)
    {
        return QImage();
    }

    const double tile_world_size = MapModel::TileSize
        * std::pow(2.0, MapRenderCacheMath::ReferenceZoom - tile.imagery_zoom);
    if (!std::isfinite(tile_world_size) || tile_world_size <= 0.0)
        return QImage();

    const double tile_left = double(tile.virtual_x) * tile_world_size
        - this->layout_origin_world.x();
    const double tile_top = double(tile.y) * tile_world_size
        - this->layout_origin_world.y();
    const double tile_right = tile_left + tile_world_size;
    const double tile_bottom = tile_top + tile_world_size;
    const double radius = this->heatmap_radius_world;

    const QVector<int> candidate_indices = heatmapMarkerCandidates(
        tile_left, tile_top, tile_right, tile_bottom, radius);
    if (candidate_indices.isEmpty())
        return QImage();

    QImage image(
        HeatmapTextureSize, HeatmapTextureSize,
        QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setPen(Qt::NoPen);

    const double pixels_per_world = double(HeatmapTextureSize) / tile_world_size;
    const double radius_pixels = radius * pixels_per_world;
    if (!std::isfinite(radius_pixels) || radius_pixels <= 0.0)
        return QImage();

    const double half_fraction = this->heatmap_solid_fraction
        + (1.0 - this->heatmap_solid_fraction) * 0.4375;

    for (int marker_index : candidate_indices)
    {
        if (marker_index < 0 || marker_index >= this->heatmap_markers.size())
            continue;
        const HeatmapMarker &marker = this->heatmap_markers.at(marker_index);
        if (marker.center.x() + radius < tile_left
            || marker.center.x() - radius > tile_right
            || marker.center.y() + radius < tile_top
            || marker.center.y() - radius > tile_bottom)
        {
            continue;
        }

        const QPointF center_pixels(
            (marker.center.x() - tile_left) * pixels_per_world,
            (marker.center.y() - tile_top) * pixels_per_world);
        QColor full_color = marker.color;
        full_color.setAlpha(255);
        QColor half_color = marker.color;
        half_color.setAlpha(128);
        QColor edge_color = marker.color;
        edge_color.setAlpha(0);

        QRadialGradient gradient(center_pixels, radius_pixels);
        gradient.setColorAt(0.0, full_color);
        if (this->heatmap_solid_fraction > 0.0)
            gradient.setColorAt(this->heatmap_solid_fraction, full_color);
        gradient.setColorAt(half_fraction, half_color);
        gradient.setColorAt(1.0, edge_color);
        painter.setBrush(gradient);
        painter.drawEllipse(center_pixels, radius_pixels, radius_pixels);
    }
    painter.end();

    return image.convertToFormat(QImage::Format_RGBA8888);
}

void MapRhiBasemapRenderer::rebuildHeatmapMarkerBuckets()
{
    this->heatmap_marker_buckets.clear();
    for (int marker_index = 0; marker_index < this->heatmap_markers.size(); ++marker_index)
    {
        const HeatmapMarker &marker = this->heatmap_markers.at(marker_index);
        const int bucket_x = heatmapMarkerBucketCoordinate(marker.center.x());
        const int bucket_y = heatmapMarkerBucketCoordinate(marker.center.y());
        this->heatmap_marker_buckets[heatmapMarkerBucketKey(bucket_x, bucket_y)]
            .append(marker_index);
    }
}

QVector<int> MapRhiBasemapRenderer::heatmapMarkerCandidates(
    double tile_left, double tile_top, double tile_right, double tile_bottom,
    double radius_world) const
{
    QVector<int> result;
    if (this->heatmap_marker_buckets.isEmpty())
        return result;

    const int minimum_bucket_x = heatmapMarkerBucketCoordinate(
        tile_left - radius_world);
    const int maximum_bucket_x = heatmapMarkerBucketCoordinate(
        tile_right + radius_world);
    const int minimum_bucket_y = heatmapMarkerBucketCoordinate(
        tile_top - radius_world);
    const int maximum_bucket_y = heatmapMarkerBucketCoordinate(
        tile_bottom + radius_world);

    for (int bucket_y = minimum_bucket_y; bucket_y <= maximum_bucket_y; ++bucket_y)
    {
        for (int bucket_x = minimum_bucket_x; bucket_x <= maximum_bucket_x; ++bucket_x)
        {
            const quint64 key = heatmapMarkerBucketKey(bucket_x, bucket_y);
            const QHash<quint64, QVector<int>>::const_iterator iterator =
                this->heatmap_marker_buckets.constFind(key);
            if (iterator == this->heatmap_marker_buckets.cend())
                continue;
            result.append(iterator.value());
        }
    }
    return result;
}

void MapRhiBasemapRenderer::pruneTextureCache()
{
    while (int(this->tile_resources.size()) > MaximumCachedGpuTiles)
    {
        std::map<QString, std::unique_ptr<TileResource>>::iterator oldest =
            this->tile_resources.end();
        for (std::map<QString, std::unique_ptr<TileResource>>::iterator iterator =
                 this->tile_resources.begin();
             iterator != this->tile_resources.end(); ++iterator)
        {
            if (iterator->second->last_used_serial == this->usage_serial)
                continue;
            if (oldest == this->tile_resources.end()
                || iterator->second->last_used_serial < oldest->second->last_used_serial)
            {
                oldest = iterator;
            }
        }

        if (oldest == this->tile_resources.end())
            break;
        this->tile_resources.erase(oldest);
    }
}


void MapRhiBasemapRenderer::rebuildWireframeVertices()
{
    this->wireframe_vertices.clear();
    this->wireframe_vertices.reserve(this->vertices.size() * 2);

    for (qsizetype index = 0; index + 2 < this->vertices.size(); index += 3)
    {
        const TileVertex &a = this->vertices.at(index);
        const TileVertex &b = this->vertices.at(index + 1);
        const TileVertex &c = this->vertices.at(index + 2);
        const WireframeVertex wa{a.x, a.y, a.z};
        const WireframeVertex wb{b.x, b.y, b.z};
        const WireframeVertex wc{c.x, c.y, c.z};

        this->wireframe_vertices.append(wa);
        this->wireframe_vertices.append(wb);
        this->wireframe_vertices.append(wb);
        this->wireframe_vertices.append(wc);
        this->wireframe_vertices.append(wc);
        this->wireframe_vertices.append(wa);
    }
}

bool MapRhiBasemapRenderer::uploadWireframeVertices(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (!this->wireframe_vertex_upload_pending)
        return true;
    if (resource_updates == nullptr || this->rhi == nullptr)
        return false;

    rebuildWireframeVertices();
    if (this->wireframe_vertices.isEmpty())
    {
        this->wireframe_vertex_upload_pending = false;
        return true;
    }

    const int required_bytes = boundedBufferSize(
        this->wireframe_vertices.size(), qsizetype(sizeof(WireframeVertex)));
    if (required_bytes <= 0)
        return false;

    if (!this->wireframe_vertex_buffer
        || this->wireframe_vertex_buffer_size != required_bytes)
    {
        this->wireframe_vertex_buffer.reset(this->rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_bytes));
        if (!this->wireframe_vertex_buffer
            || !this->wireframe_vertex_buffer->create())
        {
            return false;
        }
        this->wireframe_vertex_buffer_size = required_bytes;
    }

    resource_updates->updateDynamicBuffer(
        this->wireframe_vertex_buffer.get(), 0, required_bytes,
        this->wireframe_vertices.constData());
    this->wireframe_vertex_upload_pending = false;
    return true;
}

void MapRhiBasemapRenderer::appendFlatTileVertices(
    QVector<TileVertex> *target, VisibleTile *tile,
    float left, float top, float right, float bottom)
{
    if (target == nullptr || tile == nullptr)
        return;

    const TileVertex tile_vertices[6] = {
        {left,  top,    0.0f, 0.0f, 0.0f},
        {right, top,    0.0f, 1.0f, 0.0f},
        {right, bottom, 0.0f, 1.0f, 1.0f},
        {left,  top,    0.0f, 0.0f, 0.0f},
        {right, bottom, 0.0f, 1.0f, 1.0f},
        {left,  bottom, 0.0f, 0.0f, 1.0f}
    };
    for (const TileVertex &vertex : tile_vertices)
        target->append(vertex);
    tile->vertex_count = 6;
}

bool MapRhiBasemapRenderer::appendReliefTileVertices(
    QVector<TileVertex> *target, VisibleTile *tile,
    const MapTerrainTile *terrain_tile,
    float tile_left, float tile_top, float tile_world_size)
{
    if (target == nullptr || tile == nullptr
        || tile->imagery_zoom < tile->terrain_zoom)
    {
        return false;
    }

    const bool terrain_available = terrain_tile != nullptr
        && terrain_tile->elevations_m.size() == MapTerrainTileSampleCount;

    const int zoom_delta = tile->imagery_zoom - tile->terrain_zoom;
    const double subdivision_count = std::ldexp(1.0, zoom_delta);
    const quint32 terrain_x = terrain_available ? terrain_tile->address.x
                                                : (quint32(tile->tile_x) >> zoom_delta);
    const quint32 terrain_y = terrain_available ? terrain_tile->address.y
                                                : (quint32(tile->y) >> zoom_delta);
    const double local_tile_x = double(tile->tile_x) - double(terrain_x) * subdivision_count;
    const double local_tile_y = double(tile->y) - double(terrain_y) * subdivision_count;
    const double terrain_u_min = local_tile_x / subdivision_count;
    const double terrain_v_min = local_tile_y / subdivision_count;
    const double terrain_u_span = 1.0 / subdivision_count;
    const double terrain_v_span = 1.0 / subdivision_count;

    const int cell_divisor = 1 << qMin(zoom_delta, 6);
    const int native_cell_count = qMax(1, MapTerrainTileCellCount / cell_divisor);
    // Mesh density is selected per tile from the current 3D viewing scale.
    // It is highest around the crosshair/focus target, then falls off with
    // camera distance and ground distance from that target.
    const int cell_count = qBound(
        1, tile->terrain_cell_count, native_cell_count);
    const float step = tile_world_size / float(cell_count);

    for (int row = 0; row < cell_count; ++row)
    {
        for (int column = 0; column < cell_count; ++column)
        {
            const float left = tile_left + float(column) * step;
            const float right = left + step;
            const float top = tile_top + float(row) * step;
            const float bottom = top + step;
            const float u0 = float(column) / float(cell_count);
            const float u1 = float(column + 1) / float(cell_count);
            const float v0 = float(row) / float(cell_count);
            const float v1 = float(row + 1) / float(cell_count);
            const double terrain_u0 = terrain_u_min + double(u0) * terrain_u_span;
            const double terrain_u1 = terrain_u_min + double(u1) * terrain_u_span;
            const double terrain_v0 = terrain_v_min + double(v0) * terrain_v_span;
            const double terrain_v1 = terrain_v_min + double(v1) * terrain_v_span;
            const float z00 = terrain_available
                ? terrainElevationWorldZ(
                    bilinearTerrainSample(*terrain_tile, terrain_u0, terrain_v0))
                : 0.0f;
            const float z10 = terrain_available
                ? terrainElevationWorldZ(
                    bilinearTerrainSample(*terrain_tile, terrain_u1, terrain_v0))
                : 0.0f;
            const float z11 = terrain_available
                ? terrainElevationWorldZ(
                    bilinearTerrainSample(*terrain_tile, terrain_u1, terrain_v1))
                : 0.0f;
            const float z01 = terrain_available
                ? terrainElevationWorldZ(
                    bilinearTerrainSample(*terrain_tile, terrain_u0, terrain_v1))
                : 0.0f;

            const TileVertex cell_vertices[6] = {
                {left,  top,    z00, u0, v0},
                {right, top,    z10, u1, v0},
                {right, bottom, z11, u1, v1},
                {left,  top,    z00, u0, v0},
                {right, bottom, z11, u1, v1},
                {left,  bottom, z01, u0, v1}
            };
            for (const TileVertex &vertex : cell_vertices)
                target->append(vertex);
        }
    }

    tile->vertex_count = cell_count * cell_count * 6;
    return tile->vertex_count > 0;
}

float MapRhiBasemapRenderer::terrainElevationWorldZ(double elevation_m) const
{
    if (this->scene != nullptr && this->scene->hasGeometry())
    {
        // MapRhiScene intentionally lifts network geometry by one reference-world
        // pixel above its elevation plane. Keep terrain on the plane so pipes and
        // nodes at ground elevation remain visible instead of z-fighting it.
        return this->scene->terrainElevationToWorldZ(elevation_m) - 1.0f;
    }

    if (!std::isfinite(elevation_m) || this->map_model == nullptr)
        return 0.0f;

    const double meters_per_world_pixel = GeoWebMercator::metersPerPixel(
        this->map_model->centerLat(), MapRenderCacheMath::ReferenceZoom);
    if (!std::isfinite(meters_per_world_pixel) || meters_per_world_pixel <= 0.0)
        return 0.0f;

    return float(elevation_m / meters_per_world_pixel
        * this->map_model->view3dVerticalExaggeration());
}
