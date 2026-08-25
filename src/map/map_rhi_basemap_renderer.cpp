#include "map_rhi_basemap_renderer.h"

#include "map_model.h"
#include "map_render_cache_math.h"
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
    MapModel *map_model, MapTileRepository *tile_repository)
    : map_model(map_model),
      tile_repository(tile_repository)
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
        if (!ensureTileResource(tile.key, &resource, resource_updates))
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
        command_buffer->draw(6);
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

    const int zoom = this->map_model->zoom();
    const int tile_count = this->map_model->tileCount();
    const QPointF center = this->map_model->centerTile();
    const double tile_reference_size = MapModel::TileSize
        * std::pow(2.0, MapRenderCacheMath::ReferenceZoom - zoom);
    const double origin_tile_x = origin_world.x() / tile_reference_size;
    const double wrap_offset = std::round(
        (origin_tile_x - center.x()) / qMax(1, tile_count)) * tile_count;
    const double rendered_center_x = center.x() + wrap_offset;
    int tiles_x = viewport_size.width() / MapModel::TileSize + 4;
    int tiles_y = viewport_size.height() / MapModel::TileSize + 4;
    if (this->map_model->viewMode() == MapViewMode::ThreeD)
    {
        // The pitched camera sees substantially farther along the ground plane
        // than the orthographic viewport. Keep a wider tile apron so orbiting
        // never exposes the clear color around the map.
        tiles_x = tiles_x * 2 + 4;
        tiles_y = tiles_y * 3 + 6;
    }
    const int center_tile_x = int(std::floor(rendered_center_x));
    const int center_tile_y = int(std::floor(center.y()));
    const int start_x = center_tile_x - tiles_x / 2;
    const int start_y = center_tile_y - tiles_y / 2;
    const quint64 request_batch = this->tile_repository->beginTileRequestBatch();

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

            const int tile_x = GeoWebMercator::wrapTileX(virtual_x, zoom);
            VisibleTile tile;
            tile.key = this->map_model->tileCacheKey(tile_x, y);
            tile.virtual_x = virtual_x;
            tile.y = y;
            next_tiles.append(tile);

            if (this->tile_repository->tile(tile.key) == nullptr)
            {
                const int priority_x = virtual_x - center_tile_x;
                const int priority_y = y - center_tile_y;
                const int priority = priority_x * priority_x + priority_y * priority_y;
                this->tile_repository->requestTile(
                    this->map_model->tileEndpoint(tile_x, y),
                    tile.key, tile_x, y, priority, request_batch);
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

    this->vertices.clear();
    this->vertices.reserve(next_tiles.size() * 6);
    for (VisibleTile &tile : next_tiles)
    {
        const float left = float(tile.virtual_x * tile_reference_size - origin_world.x());
        const float top = float(tile.y * tile_reference_size - origin_world.y());
        const float right = float(left + tile_reference_size);
        const float bottom = float(top + tile_reference_size);
        tile.first_vertex = this->vertices.size();

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

    const QPixmap *pixmap = this->tile_repository != nullptr
        ? this->tile_repository->tile(key)
        : nullptr;
    if (pixmap == nullptr || pixmap->isNull())
        return true;

    std::map<QString, std::unique_ptr<TileResource>>::iterator iterator =
        this->tile_resources.find(key);
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
