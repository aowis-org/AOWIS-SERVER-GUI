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
    invalidate();
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
    this->vertex_buffer_size = 0;
    this->vertex_upload_pending = true;
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

    ++this->usage_serial;
    for (VisibleTile &tile : this->visible_tiles)
    {
        TileResource *resource = nullptr;
        if (!ensureTileResource(tile.imagery_key, &resource, resource_updates))
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
    int tiles_x = foreground_tiles_x;
    int tiles_y = foreground_tiles_y;
    if (this->map_model->viewMode() == MapViewMode::ThreeD)
    {
        // Keep the existing 3D apron. Step 6 will replace this coarse coverage
        // policy with view-frustum culling and distance-dependent terrain LOD.
        tiles_x = tiles_x * 2 + 4;
        tiles_y = tiles_y * 3 + 6;
    }
    const int center_tile_x = int(std::floor(rendered_center_x));
    const int center_tile_y = int(std::floor(center.y()));
    const int start_x = center_tile_x - tiles_x / 2;
    const int start_y = center_tile_y - tiles_y / 2;
    const int foreground_start_x = center_tile_x - foreground_tiles_x / 2;
    const int foreground_start_y = center_tile_y - foreground_tiles_y / 2;
    const QString request_layout_key = QStringLiteral("%1|%2|%3|%4|%5")
        .arg(this->map_model->tileCachePrefix(imagery_zoom))
        .arg(start_x)
        .arg(start_y)
        .arg(tiles_x)
        .arg(tiles_y);
    const quint64 request_batch = this->tile_repository->beginTileRequestBatch(
        this, request_layout_key);
    const bool relief_enabled = reliefTerrainEnabled(*this->map_model, this->terrain_repository);
    const int terrain_zoom = relief_enabled
        ? terrainZoomForImageryZoom(imagery_zoom)
        : 0;
    const int terrain_zoom_delta = relief_enabled
        ? imagery_zoom - terrain_zoom
        : 0;

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

    // During an integer zoom/LOD transition, keep rendering the previous
    // complete layer until the replacement foreground tiles are actually
    // available. Switching visible_tiles immediately would make draw() skip
    // every not-yet-loaded texture and expose the clear color as black holes.
    //
    // The old geometry remains valid because it is expressed in the common
    // ReferenceZoom world coordinate system. In 2D, the continuous scale is
    // adjusted when the integer tile zoom changes, so the retained old layer
    // stays at exactly the same apparent scale while the new LOD loads.
    const bool zoom_transition = !this->visible_tiles.isEmpty()
        && this->visible_tiles.constFirst().imagery_zoom != imagery_zoom;
    if (zoom_transition)
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
            // Keep layout_dirty set. signalTileAvailable() schedules another
            // frame, at which point this readiness check is repeated.
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
            if (terrain_tile != nullptr)
            {
                relief_built = appendReliefTileVertices(
                    &tile, *terrain_tile, left, top, float(tile_reference_size));
            }
        }

        if (!relief_built)
            appendFlatTileVertices(&tile, left, top, right, bottom);
    }

    this->visible_tiles = next_tiles;
    this->vertex_upload_pending = true;
    this->layout_dirty = false;
    return true;
}

bool MapRhiBasemapRenderer::ensureTileResource(
    const QString &key, TileResource **resource,
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource == nullptr)
        return false;
    *resource = nullptr;

    std::map<QString, std::unique_ptr<TileResource>>::iterator iterator =
        this->tile_resources.find(key);

    const QPixmap *pixmap = this->tile_repository != nullptr
        ? this->tile_repository->tile(key)
        : nullptr;
    if (pixmap == nullptr || pixmap->isNull())
    {
        // A retained zoom-transition layer may outlive its CPU QPixmap cache
        // entry. Keep using the already-uploaded GPU texture instead of
        // turning that tile into a hole merely because the CPU cache evicted it.
        if (iterator != this->tile_resources.end()
            && iterator->second->texture
            && iterator->second->bindings)
        {
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

        tile_resource->bindings.reset(this->rhi->newShaderResourceBindings());
        if (!tile_resource->bindings)
            return false;
        tile_resource->bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                this->camera_uniform_buffer),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                tile_resource->texture.get(), this->sampler.get())
        });
        if (!tile_resource->bindings->create())
            return false;

        resource_updates->uploadTexture(tile_resource->texture.get(), image);
        tile_resource->pixmap_cache_key = cache_key;
    }

    *resource = tile_resource;
    return true;
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
    VisibleTile *tile, float left, float top, float right, float bottom)
{
    if (tile == nullptr)
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
        this->vertices.append(vertex);
    tile->vertex_count = 6;
}

bool MapRhiBasemapRenderer::appendReliefTileVertices(
    VisibleTile *tile, const MapTerrainTile &terrain_tile,
    float tile_left, float tile_top, float tile_world_size)
{
    if (tile == nullptr || terrain_tile.elevations_m.size() != MapTerrainTileSampleCount
        || tile->imagery_zoom < tile->terrain_zoom)
    {
        return false;
    }

    const int zoom_delta = tile->imagery_zoom - tile->terrain_zoom;
    const double subdivision_count = std::ldexp(1.0, zoom_delta);
    const quint32 terrain_x = terrain_tile.address.x;
    const quint32 terrain_y = terrain_tile.address.y;
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
            const float z00 = terrainElevationWorldZ(
                bilinearTerrainSample(terrain_tile, terrain_u0, terrain_v0));
            const float z10 = terrainElevationWorldZ(
                bilinearTerrainSample(terrain_tile, terrain_u1, terrain_v0));
            const float z11 = terrainElevationWorldZ(
                bilinearTerrainSample(terrain_tile, terrain_u1, terrain_v1));
            const float z01 = terrainElevationWorldZ(
                bilinearTerrainSample(terrain_tile, terrain_u0, terrain_v1));

            const TileVertex cell_vertices[6] = {
                {left,  top,    z00, u0, v0},
                {right, top,    z10, u1, v0},
                {right, bottom, z11, u1, v1},
                {left,  top,    z00, u0, v0},
                {right, bottom, z11, u1, v1},
                {left,  bottom, z01, u0, v1}
            };
            for (const TileVertex &vertex : cell_vertices)
                this->vertices.append(vertex);
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
