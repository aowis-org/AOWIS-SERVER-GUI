#include "map/rhi/map_rhi_basemap_renderer.h"

#include "map/core/map_model.h"
#include "map/render/map_render_cache_math.h"
#include "map/data/map_tile_repository.h"
#include "geo/geo_web_mercator.h"
#include "config/gui_configuration.h"

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
constexpr int TwoDPanRetentionMarginTiles = 4;
constexpr int HeatmapTextureSize = 256;
constexpr double HeatmapMarkerBucketWorldSize = 16384.0;
// Capacity of the shared texture array used to batch basemap tile draw
// calls (see arrayBatchingActive()). 256 layers at 256x256 RGBA8 is ~64 MB
// at full occupancy -- comfortable headroom over what the frustum-culled,
// LRU-capped working set (MaximumCachedGpuTiles) actually needs at once.
constexpr int TileArrayLayerCount = 256;

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
    this->tile_array_texture.reset();
    this->array_bindings.reset();
    this->array_pipeline.reset();
    this->free_array_layers.clear();
    this->tile_resources.clear();
    this->visible_tiles.clear();
    this->vertices.clear();
    this->pending_vertex_patch_ranges.clear();
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
    {
        // Graphics pipelines are render-pass/sample-count specific on Vulkan.
        // The OpenGL backend is permissive enough that retaining the optional
        // texture-array pipeline happened to work, but Vulkan requires every
        // pipeline targeting the QRhiWidget pass to be recreated when Qt
        // replaces that pass (for example after widget resize/reinitialization).
        this->pipeline.reset();
        this->array_pipeline.reset();
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

    if (this->dummy_texture_upload_pending && this->dummy_texture)
    {
        QImage transparent_pixel(1, 1, QImage::Format_RGBA8888);
        transparent_pixel.fill(Qt::transparent);
        resource_updates->uploadTexture(this->dummy_texture.get(), transparent_pixel);
        this->dummy_texture_upload_pending = false;
    }

    ++this->usage_serial;
    const bool array_batching_supported = arrayBatchingActive();
    for (VisibleTile &tile : this->visible_tiles)
    {
        TileResource *resource = nullptr;
        if (!ensureTileResource(tile, &resource, resource_updates))
            return false;
        tile.resource = resource;
        if (resource != nullptr)
            resource->last_used_serial = this->usage_serial;

        tile.array_ready = false;
        if (array_batching_supported)
        {
            TileResource *array_resource = nullptr;
            if (!ensureTileArrayLayer(tile, &array_resource, resource_updates))
                return false;
            stampTileArrayLayerIfNeeded(tile, array_resource);
            tile.array_ready = array_resource != nullptr
                && array_resource->array_layer >= 0;
        }
    }

    // Evict (and, per resetVertexArrayLayerForKey, invalidate any stale
    // vertex references to) over-budget cached tile resources before
    // deciding below whether this frame does a full vertex-buffer upload or
    // a set of targeted patches -- either way must include any reset this
    // step just queued, or a tile whose layer was just freed and reassigned
    // could keep showing the wrong content for another frame.
    pruneTextureCache();

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
        this->pending_vertex_patch_ranges.clear();
    }
    else
    {
        uploadPendingVertexPatchRanges(resource_updates);
    }

    return true;
}

void MapRhiBasemapRenderer::draw(QRhiCommandBuffer *command_buffer)
{
    if (command_buffer == nullptr || !this->vertex_buffer)
        return;

    const bool use_array = arrayBatchingActive();
    if (use_array)
    {
        command_buffer->setGraphicsPipeline(this->array_pipeline.get());
        command_buffer->setShaderResources(this->array_bindings.get());
        const QRhiCommandBuffer::VertexInput array_binding(
            this->vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &array_binding);
        command_buffer->draw(quint32(this->vertices.size()));
    }

    if (!this->pipeline)
        return;

    command_buffer->setGraphicsPipeline(this->pipeline.get());
    for (const VisibleTile &tile : this->visible_tiles)
    {
        if (tile.resource == nullptr || !tile.resource->bindings)
            continue;
        if (use_array && tile.array_ready)
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

    // Array-batched rendering is a perf optimization on top of the always-
    // available per-tile path above, not a requirement, so its failure (or
    // the backend simply not supporting texture arrays) must not fail
    // resource creation as a whole -- draw()/prepare() already fall back to
    // the per-tile path whenever tile_array_texture/array_pipeline/
    // array_bindings aren't all present.
    createTileArrayResources();

    return true;
}

bool MapRhiBasemapRenderer::createTileArrayResources()
{
    if (this->rhi == nullptr || this->render_pass_descriptor == nullptr
        || this->camera_uniform_buffer == nullptr || !this->sampler)
    {
        return false;
    }

    if (!this->tile_array_texture)
    {
        if (!this->rhi->isFeatureSupported(QRhi::TextureArrays))
            return false;

        this->tile_array_texture.reset(this->rhi->newTextureArray(
            QRhiTexture::RGBA8, TileArrayLayerCount,
            QSize(MapModel::TileSize, MapModel::TileSize)));
        if (!this->tile_array_texture || !this->tile_array_texture->create())
        {
            this->tile_array_texture.reset();
            return false;
        }

        // Layer 0 is deliberately never handed out below: the array
        // fragment shader (map_rhi_basemap_array.frag) uses the vertex
        // "layer" attribute directly as the sampler2DArray layer coordinate
        // -- there is no subtract-one step, it only tests the value against
        // 0.5 to decide whether to discard. So the "not assigned yet"
        // sentinel (TileVertex::layer's default of 0.0f) has to correspond
        // to a layer index that is never actually uploaded to, rather than
        // being offset from the real array index; array_layer is used as
        // the vertex value as-is (see stampTileArrayLayerIfNeeded).
        this->free_array_layers.clear();
        this->free_array_layers.reserve(TileArrayLayerCount - 1);
        for (int layer = TileArrayLayerCount - 1; layer >= 1; --layer)
            this->free_array_layers.append(layer);
    }

    if (!this->array_bindings)
    {
        this->array_bindings.reset(this->rhi->newShaderResourceBindings());
        if (!this->array_bindings)
            return false;
        this->array_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                this->camera_uniform_buffer),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                this->tile_array_texture.get(), this->sampler.get())
        });
        if (!this->array_bindings->create())
            return false;
    }

    if (!this->array_pipeline)
    {
        const QShader vertex_shader = loadBasemapShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_basemap_array.vert.qsb"));
        const QShader fragment_shader = loadBasemapShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_basemap_array.frag.qsb"));
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
             quint32(offsetof(TileVertex, u))},
            {0, 2, QRhiVertexInputAttribute::Float,
             quint32(offsetof(TileVertex, layer))}
        });

        this->array_pipeline.reset(this->rhi->newGraphicsPipeline());
        if (!this->array_pipeline)
            return false;
        this->array_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->array_pipeline->setVertexInputLayout(input_layout);
        this->array_pipeline->setShaderResourceBindings(this->array_bindings.get());
        this->array_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->array_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->array_pipeline->setSampleCount(this->sample_count);
        this->array_pipeline->setDepthTest(true);
        this->array_pipeline->setDepthWrite(true);
        this->array_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if (!this->array_pipeline->create())
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
    const double view_scale = qMax(
        1e-9, this->map_model->view2dContinuousScale());
    const double rendered_tile_size = MapModel::TileSize * view_scale;
    const int foreground_tiles_x =
        int(std::ceil(viewport_size.width() / rendered_tile_size)) + 4;
    const int foreground_tiles_y =
        int(std::ceil(viewport_size.height() / rendered_tile_size)) + 4;
    const int tiles_x = foreground_tiles_x + TwoDPanRetentionMarginTiles * 2;
    const int tiles_y = foreground_tiles_y + TwoDPanRetentionMarginTiles * 2;
    const int center_tile_x = int(std::floor(rendered_center_x));
    const int center_tile_y = int(std::floor(center.y()));
    const int start_x = center_tile_x - tiles_x / 2;
    const int start_y = center_tile_y - tiles_y / 2;
    const int foreground_start_x = center_tile_x - foreground_tiles_x / 2;
    const int foreground_start_y = center_tile_y - foreground_tiles_y / 2;
    const QString imagery_key_prefix =
        this->map_model->tileCachePrefix(imagery_zoom);

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
    if (current_layout_covers_foreground)
        return true;

    const QString request_layout_key = QStringLiteral("%1|%2|%3|%4|%5")
        .arg(imagery_key_prefix)
        .arg(start_x)
        .arg(start_y)
        .arg(tiles_x)
        .arg(tiles_y);
    const quint64 request_batch =
        this->tile_repository->beginTileRequestBatch(this, request_layout_key);

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
            tile.foreground =
                virtual_x >= foreground_start_x
                && virtual_x < foreground_start_x + foreground_tiles_x
                && y >= foreground_start_y
                && y < foreground_start_y + foreground_tiles_y;
            next_tiles.append(tile);

            if (this->tile_repository->tile(tile.imagery_key) == nullptr)
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

    const bool retained_layer_transition = !this->visible_tiles.isEmpty()
        && layout_origin_matches;
    if (retained_layer_transition)
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
            const QVector<VisibleTile> progressive_tiles =
                progressiveProviderLayout(
                    next_tiles, imagery_zoom, imagery_key_prefix);
            if (progressive_tiles.isEmpty())
                return true;
            next_tiles = progressive_tiles;
        }
        else if (!same_lod)
        {
            const QVector<VisibleTile> progressive_tiles =
                progressiveZoomLayout(next_tiles, imagery_zoom);
            if (!progressive_tiles.isEmpty())
            {
                next_tiles = progressive_tiles;
            }
            else
            {
                bool direct_zoom_handoff = true;
                bool has_parent_source = false;
                bool has_child_source = false;
                for (const VisibleTile &tile : this->visible_tiles)
                {
                    if (tile.imagery_zoom == imagery_zoom)
                        continue;
                    if (tile.imagery_zoom == imagery_zoom - 1)
                        has_parent_source = true;
                    else if (tile.imagery_zoom == imagery_zoom + 1)
                        has_child_source = true;
                    else
                    {
                        direct_zoom_handoff = false;
                        break;
                    }
                }
                if (has_parent_source && has_child_source)
                    direct_zoom_handoff = false;

                if (direct_zoom_handoff)
                    return true;

                for (const VisibleTile &tile : next_tiles)
                {
                    if (tile.foreground && !tileReadyForZoomHandoff(tile))
                        return true;
                }
            }
        }
        else
        {
            for (const VisibleTile &tile : next_tiles)
            {
                if (tile.foreground && !tileReadyForZoomHandoff(tile))
                    return true;
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
    this->vertices.reserve(next_tiles.size() * 6);
    for (VisibleTile &tile : next_tiles)
    {
        const double visible_tile_reference_size = MapModel::TileSize
            * std::pow(
                2.0, MapRenderCacheMath::ReferenceZoom - tile.imagery_zoom);
        const float left = float(
            tile.virtual_x * visible_tile_reference_size - origin_world.x());
        const float top = float(
            tile.y * visible_tile_reference_size - origin_world.y());
        const float right = float(
            (tile.virtual_x + 1) * visible_tile_reference_size - origin_world.x());
        const float bottom = float(
            (tile.y + 1) * visible_tile_reference_size - origin_world.y());
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
                if (previous_tile.vertex_count == 6
                    && previous_tile.imagery_key == tile.imagery_key
                    && previous_tile.imagery_zoom == tile.imagery_zoom)
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

        if (!reused)
            appendFlatTileVertices(&this->vertices, &tile, left, top, right, bottom);
    }

    this->visible_tiles = next_tiles;
    this->layout_origin_world = origin_world;
    this->vertex_upload_pending = true;
    this->layout_dirty = false;
    return true;
}


bool MapRhiBasemapRenderer::tileReadyForZoomHandoff(
    const VisibleTile &tile) const
{
    if (this->tile_repository == nullptr)
        return false;

    const QPixmap *pixmap = this->tile_repository->tile(tile.imagery_key);
    return pixmap != nullptr && !pixmap->isNull();
}


QVector<MapRhiBasemapRenderer::VisibleTile>
MapRhiBasemapRenderer::progressiveProviderLayout(
    const QVector<VisibleTile> &target_tiles, int target_zoom,
    const QString &imagery_key_prefix) const
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

        if (tileReadyForZoomHandoff(target))
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

        // Once a position already uses the new source, keep that exact target
        // entry so a provider transition never regresses to the old source.
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
    const QVector<VisibleTile> &target_tiles, int target_zoom) const
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
        bool target_foreground_ready = true;
        for (const VisibleTile &tile : target_tiles)
        {
            if (!tile.foreground)
                continue;
            if (!tileReadyForZoomHandoff(tile))
            {
                target_foreground_ready = false;
                break;
            }
        }

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
            bool replacement_ready = true;
            bool replacement_foreground = false;
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
                    if (target_iterator != target_by_position.cend())
                    {
                        has_target_child = true;
                        child_indices[child_index] = target_iterator.value();
                        const VisibleTile &child = target_tiles.at(target_iterator.value());
                        replacement_foreground = replacement_foreground || child.foreground;
                        if (!tileReadyForZoomHandoff(child))
                            replacement_ready = false;
                    }
                    ++child_index;
                }
            }

            if (has_target_child && replacement_ready)
            {
                for (int index_in_group = 0; index_in_group < 4; ++index_in_group)
                {
                    if (child_indices[index_in_group] < 0)
                        continue;
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
                // The target apron can become smaller in world space when the
                // XYZ level increases. Do not let that fact alone strip the old
                // outer coverage at the start of the transition: retain it
                // until the new foreground is complete, then it is safe to
                // discard because it lies outside the requested target apron.
                if (!target_foreground_ready)
                    result.append(parent);
                else
                    changed = true;
                continue;
            }

            VisibleTile retained_parent = parent;
            retained_parent.foreground = replacement_foreground;
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
            if (!tileReadyForZoomHandoff(tile))
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

        if (tileReadyForZoomHandoff(target))
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






void MapRhiBasemapRenderer::uploadPendingVertexPatchRanges(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (this->pending_vertex_patch_ranges.isEmpty())
        return;

    // Safe to issue targeted, in-place buffer updates here: this is only
    // ever called from the branch of prepare() that runs when no full
    // vertex-buffer (re)upload is happening this frame, so the buffer's
    // current size and layout are guaranteed to already match this->vertices.
    if (resource_updates != nullptr && this->vertex_buffer)
    {
        for (const PendingVertexPatchRange &range : this->pending_vertex_patch_ranges)
        {
            const int byte_offset = int(qsizetype(range.first_vertex) * qsizetype(sizeof(TileVertex)));
            const int byte_count = boundedBufferSize(range.vertex_count, qsizetype(sizeof(TileVertex)));
            if (byte_count <= 0)
                continue;

            resource_updates->updateDynamicBuffer(
                this->vertex_buffer.get(), byte_offset, byte_count,
                this->vertices.constData() + range.first_vertex);
        }
    }

    this->pending_vertex_patch_ranges.clear();
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

bool MapRhiBasemapRenderer::arrayBatchingActive() const
{
    // This says the array-batched pass is POSSIBLE and gets issued this
    // frame -- not that every tile ends up in it (see draw()'s use of
    // VisibleTile::array_ready for the per-tile part of that). Heatmap
    // overlay tiles blend a second, per-tile-dynamic texture that the array
    // shader (map_rhi_basemap_array.frag) does not sample, so the batched
    // pass is only ever considered while no heatmap overlay is showing.
    // This is a global, not per-tile, condition: heatmapMarkers is only
    // ever non-empty while the overlay is actually active (see
    // MapRhiWidget::syncBasemapHeatmapOverlay), matching how
    // ensureTileResource's per-tile heatmap texture already behaves as a
    // no-op (transparent dummy texture) whenever it's empty.
    // User-adjustable (Settings > Map Settings > Map Performance > Enable
    // draw-call batching): purely a perf path, so disabling it just always
    // falls back to the per-tile pass below -- never changes what's drawn.
    return guiConfiguration().map_performance.array_batching_enabled
        && this->heatmap_markers.isEmpty()
        && this->tile_array_texture && this->array_pipeline && this->array_bindings;
}

bool MapRhiBasemapRenderer::ensureTileArrayLayer(
    const VisibleTile &tile, TileResource **resource,
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource == nullptr || !this->tile_array_texture)
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
        // No image yet: if this tile already has a layer from an earlier
        // frame, keep showing it rather than losing the resource.
        if (iterator != this->tile_resources.end() && iterator->second->array_layer >= 0)
            *resource = iterator->second.get();
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
    if (tile_resource->array_layer < 0 || tile_resource->pixmap_cache_key != cache_key)
    {
        if (tile_resource->array_layer < 0)
        {
            if (this->free_array_layers.isEmpty())
            {
                // Every layer is in use. This tile simply doesn't render via
                // the batched path until one frees up (its vertices keep the
                // "not assigned" sentinel, see TileVertex::layer) -- the
                // frustum culling and LRU eviction above already keep the
                // working set well under TileArrayLayerCount in practice, so
                // this is expected to be rare.
                return true;
            }
            tile_resource->array_layer = this->free_array_layers.takeLast();
        }

        if (resource_updates == nullptr)
            return false;

        QImage image = pixmap->toImage().convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull())
        {
            *resource = tile_resource;
            return true;
        }

        // Real-world XYZ tile providers are essentially always exactly
        // MapModel::TileSize square, but nothing in the fetch/decode
        // pipeline enforces that (map_tile_repository.cpp just decodes
        // whatever bytes the provider returned). Every layer in the shared
        // array has the same fixed pixel size, so guard against a provider
        // that happens to return a different resolution (e.g. an @2x tile)
        // rather than risk an out-of-bounds or corrupted upload -- this is a
        // no-op in the expected, overwhelmingly common case where the size
        // already matches.
        const QSize array_layer_size(MapModel::TileSize, MapModel::TileSize);
        if (image.size() != array_layer_size)
        {
            image = image.scaled(
                array_layer_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        if (image.isNull())
        {
            *resource = tile_resource;
            return true;
        }

        const QRhiTextureSubresourceUploadDescription subresource(image);
        const QRhiTextureUploadEntry entry(tile_resource->array_layer, 0, subresource);
        resource_updates->uploadTexture(
            this->tile_array_texture.get(), QRhiTextureUploadDescription(entry));
        tile_resource->pixmap_cache_key = cache_key;
    }

    *resource = tile_resource;
    return true;
}

void MapRhiBasemapRenderer::stampTileArrayLayerIfNeeded(
    VisibleTile &tile, const TileResource *resource)
{
    if (resource == nullptr || resource->array_layer < 0)
        return;
    if (tile.vertex_count <= 0 || tile.first_vertex < 0
        || qsizetype(tile.first_vertex) + tile.vertex_count > this->vertices.size())
    {
        return;
    }

    // Array-layer indices are small non-negative integers, exactly
    // representable in float, so a direct comparison is safe and avoids a
    // pointless per-tile buffer patch (and GPU upload) once a tile's
    // vertices already carry the layer they're assigned. Mismatches happen
    // rarely: the tile's mesh was just (re)built (layer defaults to the 0
    // "unassigned" sentinel), or its layer was just reassigned after
    // eviction.
    //
    // No +1 here: array_layer is already never 0 (see the reservation
    // comment in createTileArrayResources), and the array shader samples
    // this value directly as the array layer with no offset of its own, so
    // upload index and sample index have to be the exact same number.
    const float expected_layer = float(resource->array_layer);
    if (this->vertices.at(tile.first_vertex).layer == expected_layer)
        return;

    for (int index = 0; index < tile.vertex_count; ++index)
        this->vertices[tile.first_vertex + index].layer = expected_layer;

    PendingVertexPatchRange range;
    range.first_vertex = tile.first_vertex;
    range.vertex_count = tile.vertex_count;
    this->pending_vertex_patch_ranges.append(range);
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
        if (oldest->second->array_layer >= 0)
        {
            // The layer being freed here is about to be handed to a
            // different tile and re-uploaded with different content. If
            // this tile's own vertex range still bakes in that same layer
            // index -- which it will, until it happens to be reprocessed by
            // ensureTileArrayLayer()/stampTileArrayLayerIfNeeded() again --
            // it would keep sampling whatever the layer now holds, i.e. the
            // wrong tile's imagery, until then. Reset it to the "not
            // assigned" sentinel now so that window shows as not-yet-loaded
            // instead of as the wrong tile.
            resetVertexArrayLayerForKey(oldest->first, oldest->second->array_layer);
            this->free_array_layers.append(oldest->second->array_layer);
            oldest->second->array_layer = -1;
        }
        this->tile_resources.erase(oldest);
    }
}

void MapRhiBasemapRenderer::resetVertexArrayLayerForKey(
    const QString &imagery_key, int stale_layer)
{
    const float stale_layer_value = float(stale_layer);
    for (VisibleTile &tile : this->visible_tiles)
    {
        if (tile.imagery_key != imagery_key)
            continue;
        if (tile.vertex_count <= 0 || tile.first_vertex < 0
            || qsizetype(tile.first_vertex) + tile.vertex_count > this->vertices.size())
        {
            continue;
        }
        // Only reset if the vertex data still actually points at the layer
        // being freed -- it may already have been re-stamped to something
        // else this frame, in which case there is nothing stale to fix.
        if (this->vertices.at(tile.first_vertex).layer != stale_layer_value)
            continue;

        for (int index = 0; index < tile.vertex_count; ++index)
            this->vertices[tile.first_vertex + index].layer = 0.0f;

        PendingVertexPatchRange range;
        range.first_vertex = tile.first_vertex;
        range.vertex_count = tile.vertex_count;
        this->pending_vertex_patch_ranges.append(range);
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



