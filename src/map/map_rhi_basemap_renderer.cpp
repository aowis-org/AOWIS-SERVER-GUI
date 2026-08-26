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
constexpr double TerrainVerticalScale = 1.0;
constexpr int TwoDPanRetentionMarginTiles = 4;
constexpr int HeatmapTextureSize = 256;

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
    if (this->heatmap_markers == markers
        && qFuzzyCompare(1.0 + this->heatmap_radius_world,
                         1.0 + bounded_radius_world)
        && qFuzzyCompare(1.0 + this->heatmap_solid_fraction,
                         1.0 + bounded_solid_fraction))
    {
        return;
    }

    this->heatmap_markers = markers;
    this->heatmap_radius_world = bounded_radius_world;
    this->heatmap_solid_fraction = bounded_solid_fraction;
    ++this->heatmap_revision;
    if (this->heatmap_revision == 0)
        this->heatmap_revision = 1;
}

void MapRhiBasemapRenderer::invalidate()
{
    this->layout_dirty = true;
    this->vertex_upload_pending = true;
}

void MapRhiBasemapRenderer::releaseResources()
{
    this->pipeline.reset();
    this->template_bindings.reset();
    this->dummy_texture.reset();
    this->sampler.reset();
    this->vertex_buffer.reset();
    this->tile_resources.clear();
    this->visible_tiles.clear();
    this->vertices.clear();
    this->dirty_terrain_keys.clear();
    this->layout_origin_world = QPointF();
    this->vertex_buffer_size = 0;
    this->vertex_upload_pending = true;
    this->dummy_texture_upload_pending = true;
    this->layout_dirty = true;
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
        this->pipeline.reset();

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

    pruneTextureCache();
    return true;
}

void MapRhiBasemapRenderer::draw(QRhiCommandBuffer *command_buffer)
{
    if (command_buffer == nullptr || !this->pipeline || !this->vertex_buffer)
        return;

    command_buffer->setGraphicsPipeline(this->pipeline.get());
    for (const VisibleTile &tile : this->visible_tiles)
    {
        if (tile.resource == nullptr || !tile.resource->bindings)
            continue;

        command_buffer->setShaderResources(tile.resource->bindings.get());
        const quint32 byte_offset = quint32(
            tile.first_vertex * int(sizeof(TileVertex)));
        const QRhiCommandBuffer::VertexInput binding(this->vertex_buffer.get(), byte_offset);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(tile.vertex_count));
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
        // Keep the existing 3D apron. Step 6 will replace this coarse coverage
        // policy with view-frustum culling and distance-dependent terrain LOD.
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
    if (!this->layout_dirty && layout_origin_matches
        && currentLayoutCoversForeground(
            imagery_zoom, foreground_start_x, foreground_start_y,
            foreground_tiles_x, foreground_tiles_y, tile_count))
    {
        if (relief_enabled && this->terrain_repository != nullptr)
        {
            const int foreground_end_x = foreground_start_x + foreground_tiles_x;
            const int foreground_end_y = foreground_start_y + foreground_tiles_y;
            for (const VisibleTile &tile : this->visible_tiles)
            {
                if (tile.virtual_x < foreground_start_x || tile.virtual_x >= foreground_end_x
                    || tile.y < foreground_start_y || tile.y >= foreground_end_y
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

    const QString request_layout_key = QStringLiteral("%1|%2|%3|%4|%5")
        .arg(this->map_model->tileCachePrefix(imagery_zoom))
        .arg(start_x)
        .arg(start_y)
        .arg(tiles_x)
        .arg(tiles_y);
    const quint64 request_batch = this->tile_repository->beginTileRequestBatch(
        this, request_layout_key);

    QVector<VisibleTile> next_tiles;
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

            if (relief_enabled)
            {
                MapTerrainTileAddress terrain_address;
                terrain_address.zoom = terrain_zoom;
                terrain_address.x = quint32(tile_x) >> terrain_zoom_delta;
                terrain_address.y = quint32(y) >> terrain_zoom_delta;
                tile.terrain_key = mapTerrainTileKey(terrainDatasetId(), terrain_address);
                if (tile.foreground)
                {
                    this->terrain_repository->requestTile(
                        terrainDatasetId(), terrain_address.zoom,
                        terrain_address.x, terrain_address.y);
                }
            }
            next_tiles.append(tile);

            if (this->tile_repository->tile(tile.imagery_key) == nullptr)
            {
                const int priority_x = virtual_x - center_tile_x;
                const int priority_y = y - center_tile_y;
                const int priority = priority_x * priority_x + priority_y * priority_y;
                this->tile_repository->requestTile(
                    this->map_model->tileEndpoint(tile_x, y),
                    tile.imagery_key, tile_x, y, priority, request_batch, tile.foreground);
            }
        }
    }

    // Keep the previous complete layer during both zoom-LOD handoffs and pan
    // window recenters until the replacement foreground is resident. The old
    // window deliberately extends beyond the actual viewport, so it can cover
    // the screen while the newly exposed edge tiles finish loading.
    //
    // The old geometry remains valid because it is expressed in the common
    // ReferenceZoom world coordinate system. In 2D, the continuous scale is
    // adjusted when the integer tile zoom changes, so an LOD handoff also
    // retains exactly the same apparent scale.
    const bool retained_layer_transition = !this->visible_tiles.isEmpty()
        && layout_origin_matches;
    if (retained_layer_transition)
    {
        bool foreground_ready = true;
        for (const VisibleTile &tile : next_tiles)
        {
            if (!tile.foreground)
                continue;

            const QPixmap *pixmap = this->tile_repository->tile(tile.imagery_key);
            if (pixmap == nullptr || pixmap->isNull())
            {
                foreground_ready = false;
                break;
            }

            if (relief_enabled && !tile.terrain_key.isEmpty()
                && this->terrain_repository != nullptr
                && this->terrain_repository->tile(tile.terrain_key) == nullptr)
            {
                foreground_ready = false;
                break;
            }
        }

        if (!foreground_ready)
        {
            // Keep drawing the retained layer. Tile-availability signals schedule
            // another frame, where this readiness check is repeated.
            return true;
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

    this->vertices.clear();
    this->vertices.reserve(next_tiles.size() * 6);
    for (VisibleTile &tile : next_tiles)
    {
        const float left = float(tile.virtual_x * tile_reference_size - origin_world.x());
        const float top = float(tile.y * tile_reference_size - origin_world.y());
        const float right = float(left + tile_reference_size);
        const float bottom = float(top + tile_reference_size);
        tile.first_vertex = this->vertices.size();

        bool relief_built = false;
        if (relief_enabled && !tile.terrain_key.isEmpty() && this->terrain_repository != nullptr)
        {
            const MapTerrainTile *terrain_tile = this->terrain_repository->tile(tile.terrain_key);
            // Build the full relief grid even before its DEM arrives. A flat
            // grid with identical vertex count lets a later terrain response
            // patch only this tile's vertex range instead of rebuilding the
            // entire basemap mesh.
            relief_built = appendReliefTileVertices(
                &this->vertices, &tile, terrain_tile,
                left, top, float(tile_reference_size));
        }

        if (!relief_built)
            appendFlatTileVertices(&this->vertices, &tile, left, top, right, bottom);
    }

    this->visible_tiles = next_tiles;
    this->layout_origin_world = origin_world;
    this->vertex_upload_pending = true;
    this->layout_dirty = false;
    return true;
}

bool MapRhiBasemapRenderer::currentLayoutCoversForeground(
    int imagery_zoom, int foreground_start_x, int foreground_start_y,
    int foreground_tiles_x, int foreground_tiles_y, int tile_count) const
{
    if (this->visible_tiles.isEmpty()
        || this->visible_tiles.constFirst().imagery_zoom != imagery_zoom)
    {
        return false;
    }

    int minimum_x = std::numeric_limits<int>::max();
    int maximum_x = std::numeric_limits<int>::min();
    int minimum_y = std::numeric_limits<int>::max();
    int maximum_y = std::numeric_limits<int>::min();
    for (const VisibleTile &tile : this->visible_tiles)
    {
        minimum_x = qMin(minimum_x, tile.virtual_x);
        maximum_x = qMax(maximum_x, tile.virtual_x);
        minimum_y = qMin(minimum_y, tile.y);
        maximum_y = qMax(maximum_y, tile.y);
    }

    const int required_minimum_x = foreground_start_x;
    const int required_maximum_x = foreground_start_x + foreground_tiles_x - 1;
    const int required_minimum_y = qMax(0, foreground_start_y);
    const int required_maximum_y = qMin(
        tile_count - 1, foreground_start_y + foreground_tiles_y - 1);

    return minimum_x <= required_minimum_x
        && maximum_x >= required_maximum_x
        && minimum_y <= required_minimum_y
        && maximum_y >= required_maximum_y;
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
    }

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
        return true;

    const QImage image = renderHeatmapTile(tile);
    resource->bindings.reset();
    resource->heatmap_texture.reset();

    if (!image.isNull())
    {
        resource->heatmap_texture.reset(this->rhi->newTexture(
            QRhiTexture::RGBA8, image.size()));
        if (!resource->heatmap_texture || !resource->heatmap_texture->create())
            return false;
        resource_updates->uploadTexture(resource->heatmap_texture.get(), image);
    }

    resource->heatmap_revision = this->heatmap_revision;
    return rebuildTileBindings(resource);
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

    bool intersects = false;
    for (const HeatmapMarker &marker : this->heatmap_markers)
    {
        if (marker.center.x() + radius < tile_left
            || marker.center.x() - radius > tile_right
            || marker.center.y() + radius < tile_top
            || marker.center.y() - radius > tile_bottom)
        {
            continue;
        }
        intersects = true;
        break;
    }
    if (!intersects)
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

    for (const HeatmapMarker &marker : this->heatmap_markers)
    {
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
    // The large 3D apron prevents horizon gaps. Keep those background tiles
    // relieved but coarse so this first terrain pass cannot allocate tens of
    // millions of vertices at terrain zoom 14. Foreground tiles retain every
    // DEM cell; Step 6 will replace this with proper distance/frustum LOD.
    const int cell_count = tile->foreground
        ? native_cell_count
        : qMin(native_cell_count, 8);
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
        * TerrainVerticalScale);
}
