#include "map/rhi/map_rhi_globe_surface_backend.h"

#include <QFile>
#include <QImage>

#include <rhi/qshader.h>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace
{
bool byteCountFitsInt(qsizetype count, qsizetype element_size, int *bytes)
{
    if (bytes == nullptr || count < 0 || element_size <= 0)
        return false;

    const qsizetype byte_count = count * element_size;
    if (byte_count < 0
        || byte_count > qsizetype(std::numeric_limits<int>::max()))
    {
        return false;
    }

    *bytes = int(byte_count);
    return true;
}

QShader loadSurfaceShader(const QString &resource_path)
{
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly))
        return QShader();
    return QShader::fromSerialized(file.readAll());
}

constexpr int SurfaceCameraUniformFloatCount = 24;
constexpr int SurfaceCameraUniformBytes =
    SurfaceCameraUniformFloatCount * int(sizeof(float));

bool ensureDynamicBuffer(
    QRhi *rhi,
    std::unique_ptr<QRhiBuffer> *buffer,
    int *buffer_size,
    QRhiBuffer::UsageFlags usage,
    int required_bytes)
{
    if (rhi == nullptr || buffer == nullptr || buffer_size == nullptr
        || required_bytes <= 0)
    {
        return false;
    }

    if (*buffer && *buffer_size == required_bytes)
        return true;

    buffer->reset(rhi->newBuffer(QRhiBuffer::Dynamic, usage, required_bytes));
    if (!*buffer || !(*buffer)->create())
        return false;

    *buffer_size = required_bytes;
    return true;
}
}

MapRhiGlobeSurfaceBackend::MapRhiGlobeSurfaceBackend() = default;
MapRhiGlobeSurfaceBackend::~MapRhiGlobeSurfaceBackend() = default;

bool MapRhiGlobeSurfaceBackend::ensureSharedResources(
    QRhi *rhi,
    QRhiRenderPassDescriptor *render_pass_descriptor,
    int sample_count)
{
    if (rhi == nullptr || render_pass_descriptor == nullptr)
    {
        return false;
    }

    if (!this->camera_uniform_buffer)
    {
        this->camera_uniform_buffer.reset(rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
            SurfaceCameraUniformBytes));
        if (!this->camera_uniform_buffer
            || !this->camera_uniform_buffer->create())
        {
            return false;
        }
    }

    if (!this->sampler_resource)
    {
        this->sampler_resource.reset(rhi->newSampler(
            QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
            QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!this->sampler_resource || !this->sampler_resource->create())
            return false;
    }

    if (!this->dummy_texture)
    {
        this->dummy_texture.reset(
            rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!this->dummy_texture || !this->dummy_texture->create())
            return false;
        this->dummy_texture_upload_pending = true;
    }

    if (!this->heatmap_dummy_texture)
    {
        this->heatmap_dummy_texture.reset(
            rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!this->heatmap_dummy_texture
            || !this->heatmap_dummy_texture->create())
        {
            return false;
        }
        this->heatmap_dummy_texture_upload_pending = true;
    }

    if (!this->template_bindings)
    {
        this->template_bindings.reset(rhi->newShaderResourceBindings());
        if (!this->template_bindings)
            return false;
        this->template_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                this->camera_uniform_buffer.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                this->dummy_texture.get(), this->sampler_resource.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage,
                this->heatmap_dummy_texture.get(), this->sampler_resource.get())
        });
        if (!this->template_bindings->create())
            return false;
    }

    if (!this->wireframe_bindings)
    {
        this->wireframe_bindings.reset(rhi->newShaderResourceBindings());
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

    if (!this->tile_pipeline)
    {
        const QShader vertex_shader = loadSurfaceShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe.vert.qsb"));
        const QShader fragment_shader = loadSurfaceShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
            return false;

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapGlobeSurfaceVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapGlobeSurfaceVertex, x))},
            {0, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapGlobeSurfaceVertex, u))}
        });

        this->tile_pipeline.reset(rhi->newGraphicsPipeline());
        if (!this->tile_pipeline)
            return false;
        this->tile_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->tile_pipeline->setVertexInputLayout(input_layout);
        this->tile_pipeline->setShaderResourceBindings(
            this->template_bindings.get());
        this->tile_pipeline->setRenderPassDescriptor(render_pass_descriptor);
        this->tile_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->tile_pipeline->setSampleCount(sample_count);
        this->tile_pipeline->setDepthTest(true);
        this->tile_pipeline->setDepthWrite(true);
        this->tile_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if (!this->tile_pipeline->create())
        {
            this->tile_pipeline.reset();
            return false;
        }
    }

    if (!this->wireframe_pipeline)
    {
        const QShader vertex_shader = loadSurfaceShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe_wireframe.vert.qsb"));
        const QShader fragment_shader = loadSurfaceShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe_wireframe.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
            return false;

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapGlobeSurfaceWireframeVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapGlobeSurfaceWireframeVertex, x))}
        });

        this->wireframe_pipeline.reset(rhi->newGraphicsPipeline());
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
            render_pass_descriptor);
        this->wireframe_pipeline->setTopology(QRhiGraphicsPipeline::Lines);
        this->wireframe_pipeline->setSampleCount(sample_count);
        this->wireframe_pipeline->setDepthTest(true);
        this->wireframe_pipeline->setDepthWrite(false);
        this->wireframe_pipeline->setDepthOp(
            QRhiGraphicsPipeline::LessOrEqual);
        if (!this->wireframe_pipeline->create())
        {
            this->wireframe_pipeline.reset();
            return false;
        }
    }

    return true;
}

bool MapRhiGlobeSurfaceBackend::ensureImageryArrayPipeline(
    QRhi *rhi,
    QRhiRenderPassDescriptor *render_pass_descriptor,
    int sample_count)
{
    if (rhi == nullptr || render_pass_descriptor == nullptr
        || this->imagery_array_pages.empty())
    {
        return false;
    }
    if (this->imagery_array_pipeline)
        return true;

    const QShader vertex_shader = loadSurfaceShader(
        QStringLiteral(":/aowis/map/rhi/map_rhi_globe_array.vert.qsb"));
    const QShader fragment_shader = loadSurfaceShader(
        QStringLiteral(":/aowis/map/rhi/map_rhi_globe_array.frag.qsb"));
    if (!vertex_shader.isValid() || !fragment_shader.isValid())
        return false;

    QRhiVertexInputLayout input_layout;
    input_layout.setBindings({
        {quint32(sizeof(MapGlobeSurfaceVertex))}
    });
    input_layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3,
         quint32(offsetof(MapGlobeSurfaceVertex, x))},
        {0, 1, QRhiVertexInputAttribute::Float2,
         quint32(offsetof(MapGlobeSurfaceVertex, u))},
        {0, 2, QRhiVertexInputAttribute::Float,
         quint32(offsetof(MapGlobeSurfaceVertex, layer))}
    });

    this->imagery_array_pipeline.reset(rhi->newGraphicsPipeline());
    if (!this->imagery_array_pipeline)
        return false;
    this->imagery_array_pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertex_shader},
        {QRhiShaderStage::Fragment, fragment_shader}
    });
    this->imagery_array_pipeline->setVertexInputLayout(input_layout);
    this->imagery_array_pipeline->setShaderResourceBindings(
        this->imagery_array_pages.front().bindings.get());
    this->imagery_array_pipeline->setRenderPassDescriptor(
        render_pass_descriptor);
    this->imagery_array_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    this->imagery_array_pipeline->setSampleCount(sample_count);
    this->imagery_array_pipeline->setDepthTest(true);
    this->imagery_array_pipeline->setDepthWrite(true);
    this->imagery_array_pipeline->setDepthOp(
        QRhiGraphicsPipeline::LessOrEqual);
    if (!this->imagery_array_pipeline->create())
    {
        this->imagery_array_pipeline.reset();
        return false;
    }
    return true;
}

bool MapRhiGlobeSurfaceBackend::ensureHeatmapArrayPipeline(
    QRhi *rhi,
    QRhiRenderPassDescriptor *render_pass_descriptor,
    int sample_count)
{
    if (rhi == nullptr || render_pass_descriptor == nullptr
        || this->imagery_array_pages.empty()
        || this->heatmap_array_pages.empty()
        || this->camera_uniform_buffer == nullptr
        || this->sampler_resource == nullptr)
    {
        return false;
    }

    if (!this->heatmap_array_template_bindings)
    {
        this->heatmap_array_template_bindings.reset(
            rhi->newShaderResourceBindings());
        if (!this->heatmap_array_template_bindings)
            return false;
        this->heatmap_array_template_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                this->camera_uniform_buffer.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                this->imagery_array_pages.front().texture.get(),
                this->sampler_resource.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage,
                this->heatmap_array_pages.front().texture.get(),
                this->sampler_resource.get())
        });
        if (!this->heatmap_array_template_bindings->create())
        {
            this->heatmap_array_template_bindings.reset();
            return false;
        }
    }

    if (this->heatmap_array_pipeline)
        return true;

    const QShader vertex_shader = loadSurfaceShader(QStringLiteral(
        ":/aowis/map/rhi/map_rhi_globe_heatmap_array.vert.qsb"));
    const QShader fragment_shader = loadSurfaceShader(QStringLiteral(
        ":/aowis/map/rhi/map_rhi_globe_heatmap_array.frag.qsb"));
    if (!vertex_shader.isValid() || !fragment_shader.isValid())
        return false;

    QRhiVertexInputLayout input_layout;
    input_layout.setBindings({
        {quint32(sizeof(MapGlobeSurfaceVertex))},
        {quint32(sizeof(float))}
    });
    input_layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3,
         quint32(offsetof(MapGlobeSurfaceVertex, x))},
        {0, 1, QRhiVertexInputAttribute::Float2,
         quint32(offsetof(MapGlobeSurfaceVertex, u))},
        {0, 2, QRhiVertexInputAttribute::Float,
         quint32(offsetof(MapGlobeSurfaceVertex, layer))},
        {1, 3, QRhiVertexInputAttribute::Float, 0}
    });

    this->heatmap_array_pipeline.reset(rhi->newGraphicsPipeline());
    if (!this->heatmap_array_pipeline)
        return false;
    this->heatmap_array_pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertex_shader},
        {QRhiShaderStage::Fragment, fragment_shader}
    });
    this->heatmap_array_pipeline->setVertexInputLayout(input_layout);
    this->heatmap_array_pipeline->setShaderResourceBindings(
        this->heatmap_array_template_bindings.get());
    this->heatmap_array_pipeline->setRenderPassDescriptor(
        render_pass_descriptor);
    this->heatmap_array_pipeline->setTopology(
        QRhiGraphicsPipeline::Triangles);
    this->heatmap_array_pipeline->setSampleCount(sample_count);
    this->heatmap_array_pipeline->setDepthTest(true);
    this->heatmap_array_pipeline->setDepthWrite(true);
    this->heatmap_array_pipeline->setDepthOp(
        QRhiGraphicsPipeline::LessOrEqual);
    if (!this->heatmap_array_pipeline->create())
    {
        this->heatmap_array_pipeline.reset();
        return false;
    }
    return true;
}

void MapRhiGlobeSurfaceBackend::invalidateRenderPassPipelines()
{
    this->heatmap_array_pipeline.reset();
    this->imagery_array_pipeline.reset();
    this->wireframe_pipeline.reset();
    this->tile_pipeline.reset();
}

bool MapRhiGlobeSurfaceBackend::uploadPendingMissingTileTexture(
    QRhiResourceUpdateBatch *resource_updates,
    const QColor &missing_tile_color)
{
    if (resource_updates == nullptr)
        return false;

    if (this->dummy_texture_upload_pending && this->dummy_texture)
    {
        QImage image(1, 1, QImage::Format_RGBA8888);
        image.fill(missing_tile_color);
        resource_updates->uploadTexture(this->dummy_texture.get(), image);
        this->dummy_texture_upload_pending = false;
    }
    return true;
}

bool MapRhiGlobeSurfaceBackend::uploadPendingHeatmapDummyTexture(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource_updates == nullptr)
        return false;

    if (this->heatmap_dummy_texture_upload_pending
        && this->heatmap_dummy_texture)
    {
        QImage image(1, 1, QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        resource_updates->uploadTexture(
            this->heatmap_dummy_texture.get(), image);
        this->heatmap_dummy_texture_upload_pending = false;
    }
    return true;
}

bool MapRhiGlobeSurfaceBackend::uploadCameraUniform(
    QRhiResourceUpdateBatch *resource_updates,
    const QMatrix4x4 &view_projection,
    float heatmap_opacity,
    const QColor &background_color,
    float background_opacity)
{
    if (resource_updates == nullptr || !this->camera_uniform_buffer)
        return false;

    float uniform_data[SurfaceCameraUniformFloatCount] = {};
    std::copy(
        view_projection.constData(), view_projection.constData() + 16,
        uniform_data);
    uniform_data[17] = heatmap_opacity;
    uniform_data[20] = background_color.redF();
    uniform_data[21] = background_color.greenF();
    uniform_data[22] = background_color.blueF();
    uniform_data[23] = qBound(0.0f, background_opacity, 1.0f);
    resource_updates->updateDynamicBuffer(
        this->camera_uniform_buffer.get(), 0,
        SurfaceCameraUniformBytes, uniform_data);
    return true;
}

bool MapRhiGlobeSurfaceBackend::rebuildTileBindings(
    QRhi *rhi,
    QRhiTexture *imagery_texture,
    QRhiTexture *heatmap_texture,
    std::unique_ptr<QRhiShaderResourceBindings> *bindings)
{
    if (rhi == nullptr || imagery_texture == nullptr || bindings == nullptr
        || this->camera_uniform_buffer == nullptr
        || this->sampler_resource == nullptr
        || this->heatmap_dummy_texture == nullptr)
    {
        return false;
    }

    QRhiTexture *resolved_heatmap_texture = heatmap_texture != nullptr
        ? heatmap_texture
        : this->heatmap_dummy_texture.get();
    bindings->reset(rhi->newShaderResourceBindings());
    if (!*bindings)
        return false;
    (*bindings)->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage
                | QRhiShaderResourceBinding::FragmentStage,
            this->camera_uniform_buffer.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage,
            imagery_texture, this->sampler_resource.get()),
        QRhiShaderResourceBinding::sampledTexture(
            2, QRhiShaderResourceBinding::FragmentStage,
            resolved_heatmap_texture, this->sampler_resource.get())
    });
    return (*bindings)->create();
}

bool MapRhiGlobeSurfaceBackend::rebuildHeatmapArrayBindings(
    QRhi *rhi,
    QRhiTexture *imagery_texture,
    QRhiTexture *heatmap_texture,
    std::unique_ptr<QRhiShaderResourceBindings> *bindings)
{
    if (rhi == nullptr || imagery_texture == nullptr
        || heatmap_texture == nullptr || bindings == nullptr
        || this->camera_uniform_buffer == nullptr
        || this->sampler_resource == nullptr)
    {
        return false;
    }

    bindings->reset(rhi->newShaderResourceBindings());
    if (!*bindings)
        return false;
    (*bindings)->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage
                | QRhiShaderResourceBinding::FragmentStage,
            this->camera_uniform_buffer.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage,
            imagery_texture, this->sampler_resource.get()),
        QRhiShaderResourceBinding::sampledTexture(
            2, QRhiShaderResourceBinding::FragmentStage,
            heatmap_texture, this->sampler_resource.get())
    });
    return (*bindings)->create();
}

bool MapRhiGlobeSurfaceBackend::fallbackTextureUploadsPending() const
{
    return this->dummy_texture_upload_pending
        || this->heatmap_dummy_texture_upload_pending;
}

bool MapRhiGlobeSurfaceBackend::imageryArrayPipelineReady() const
{
    return this->imagery_array_pipeline != nullptr;
}

bool MapRhiGlobeSurfaceBackend::heatmapArrayPipelineReady() const
{
    return this->heatmap_array_pipeline != nullptr
        && this->heatmap_array_template_bindings != nullptr;
}

void MapRhiGlobeSurfaceBackend::populateSharedDrawResources(
    MapRhiGlobeSurfaceDrawResources *draw_resources) const
{
    if (draw_resources == nullptr)
        return;
    draw_resources->tile_pipeline = this->tile_pipeline.get();
    draw_resources->array_pipeline = this->imagery_array_pipeline.get();
    draw_resources->heatmap_array_pipeline = this->heatmap_array_pipeline.get();
    draw_resources->wireframe_pipeline = this->wireframe_pipeline.get();
    draw_resources->template_bindings = this->template_bindings.get();
    draw_resources->wireframe_bindings = this->wireframe_bindings.get();
}

void MapRhiGlobeSurfaceBackend::reset()
{
    clearTileGpuResources();
    this->next_tile_gpu_resource_id = 1;
    this->heatmap_array_pipeline.reset();
    this->imagery_array_pipeline.reset();
    this->wireframe_pipeline.reset();
    this->tile_pipeline.reset();
    this->heatmap_array_template_bindings.reset();
    this->wireframe_bindings.reset();
    this->template_bindings.reset();
    this->heatmap_dummy_texture.reset();
    this->dummy_texture.reset();
    this->sampler_resource.reset();
    this->camera_uniform_buffer.reset();
    this->dummy_texture_upload_pending = true;
    this->heatmap_dummy_texture_upload_pending = true;

    this->heatmap_bake_resources = MapRhiGlobeHeatmapBakeResources();
    this->terrain_height_array_pages.clear();
    this->heatmap_array_pages.clear();
    this->imagery_array_pages.clear();

    this->heatmap_array_layer_buffer.reset();
    this->heatmap_array_draw_index_buffer.reset();
    this->tile_array_draw_index_buffer.reset();
    this->wireframe_vertex_buffer.reset();
    this->cap_index_buffer.reset();
    this->cap_vertex_buffer.reset();
    this->window_index_buffer.reset();
    this->window_vertex_buffer.reset();

    this->heatmap_array_layer_buffer_size = 0;
    this->heatmap_array_draw_index_buffer_size = 0;
    this->tile_array_draw_index_buffer_size = 0;
    this->wireframe_vertex_buffer_size = 0;
    this->cap_index_buffer_size = 0;
    this->cap_vertex_buffer_size = 0;
    this->window_index_buffer_size = 0;
    this->window_vertex_buffer_size = 0;

    this->window_vertex_upload_pending = true;
    this->window_index_upload_pending = true;
    this->cap_vertex_upload_pending = true;
    this->cap_index_upload_pending = true;
    this->wireframe_vertex_upload_pending = true;
}

std::vector<MapRhiGlobeImageryArrayPage> &
MapRhiGlobeSurfaceBackend::imageryArrayPages()
{
    return this->imagery_array_pages;
}

const std::vector<MapRhiGlobeImageryArrayPage> &
MapRhiGlobeSurfaceBackend::imageryArrayPages() const
{
    return this->imagery_array_pages;
}

std::vector<MapRhiGlobeHeatmapArrayPage> &
MapRhiGlobeSurfaceBackend::heatmapArrayPages()
{
    return this->heatmap_array_pages;
}

const std::vector<MapRhiGlobeHeatmapArrayPage> &
MapRhiGlobeSurfaceBackend::heatmapArrayPages() const
{
    return this->heatmap_array_pages;
}

std::vector<MapRhiGlobeTerrainHeightArrayPage> &
MapRhiGlobeSurfaceBackend::terrainHeightArrayPages()
{
    return this->terrain_height_array_pages;
}

const std::vector<MapRhiGlobeTerrainHeightArrayPage> &
MapRhiGlobeSurfaceBackend::terrainHeightArrayPages() const
{
    return this->terrain_height_array_pages;
}

void MapRhiGlobeSurfaceBackend::invalidateWindowGeometry()
{
    this->window_vertex_upload_pending = true;
    this->window_index_upload_pending = true;
}

void MapRhiGlobeSurfaceBackend::invalidateWindowVertices()
{
    this->window_vertex_upload_pending = true;
}

void MapRhiGlobeSurfaceBackend::invalidateCaps()
{
    this->cap_vertex_upload_pending = true;
    this->cap_index_upload_pending = true;
}

void MapRhiGlobeSurfaceBackend::invalidateWireframe()
{
    this->wireframe_vertex_upload_pending = true;
}

bool MapRhiGlobeSurfaceBackend::hasPendingGeometryUploads(
    const MapGlobeSurfaceRenderFrame &frame) const
{
    if (!frame.isValid())
        return true;

    const MapGlobeSurfaceRenderResources &resources = frame.resources;
    return (this->window_vertex_upload_pending
            && !resources.window_vertices->isEmpty())
        || (this->window_index_upload_pending
            && !resources.window_indices->isEmpty())
        || (this->cap_vertex_upload_pending
            && !resources.cap_vertices->isEmpty())
        || (this->cap_index_upload_pending
            && !resources.cap_indices->isEmpty())
        || (frame.wireframe_visible
            && this->wireframe_vertex_upload_pending);
}

bool MapRhiGlobeSurfaceBackend::uploadGeometry(
    QRhi *rhi,
    QRhiResourceUpdateBatch *resource_updates,
    const MapGlobeSurfaceRenderFrame &frame)
{
    if (rhi == nullptr || resource_updates == nullptr || !frame.isValid())
        return false;

    const MapGlobeSurfaceRenderResources &resources = frame.resources;

    if (this->window_vertex_upload_pending)
    {
        if (resources.window_vertices->isEmpty())
        {
            this->window_vertex_upload_pending = false;
        }
        else
        {
            int required_bytes = 0;
            if (!byteCountFitsInt(
                    resources.window_vertices->size(),
                    qsizetype(sizeof(MapGlobeSurfaceVertex)),
                    &required_bytes)
                || !ensureDynamicBuffer(
                    rhi, &this->window_vertex_buffer,
                    &this->window_vertex_buffer_size,
                    QRhiBuffer::VertexBuffer, required_bytes))
            {
                return false;
            }

            resource_updates->updateDynamicBuffer(
                this->window_vertex_buffer.get(), 0, required_bytes,
                resources.window_vertices->constData());
            this->window_vertex_upload_pending = false;
        }
    }

    if (this->window_index_upload_pending)
    {
        if (resources.window_indices->isEmpty())
        {
            this->window_index_upload_pending = false;
        }
        else
        {
            int required_bytes = 0;
            if (!byteCountFitsInt(
                    resources.window_indices->size(),
                    qsizetype(sizeof(quint32)), &required_bytes)
                || !ensureDynamicBuffer(
                    rhi, &this->window_index_buffer,
                    &this->window_index_buffer_size,
                    QRhiBuffer::IndexBuffer, required_bytes))
            {
                return false;
            }

            resource_updates->updateDynamicBuffer(
                this->window_index_buffer.get(), 0, required_bytes,
                resources.window_indices->constData());
            this->window_index_upload_pending = false;
        }
    }

    if (this->cap_vertex_upload_pending)
    {
        if (resources.cap_vertices->isEmpty())
        {
            this->cap_vertex_upload_pending = false;
        }
        else
        {
            int required_bytes = 0;
            if (!byteCountFitsInt(
                    resources.cap_vertices->size(),
                    qsizetype(sizeof(MapGlobeSurfaceVertex)),
                    &required_bytes)
                || !ensureDynamicBuffer(
                    rhi, &this->cap_vertex_buffer,
                    &this->cap_vertex_buffer_size,
                    QRhiBuffer::VertexBuffer, required_bytes))
            {
                return false;
            }

            resource_updates->updateDynamicBuffer(
                this->cap_vertex_buffer.get(), 0, required_bytes,
                resources.cap_vertices->constData());
            this->cap_vertex_upload_pending = false;
        }
    }

    if (this->cap_index_upload_pending)
    {
        if (resources.cap_indices->isEmpty())
        {
            this->cap_index_upload_pending = false;
        }
        else
        {
            int required_bytes = 0;
            if (!byteCountFitsInt(
                    resources.cap_indices->size(),
                    qsizetype(sizeof(quint32)), &required_bytes)
                || !ensureDynamicBuffer(
                    rhi, &this->cap_index_buffer,
                    &this->cap_index_buffer_size,
                    QRhiBuffer::IndexBuffer, required_bytes))
            {
                return false;
            }

            resource_updates->updateDynamicBuffer(
                this->cap_index_buffer.get(), 0, required_bytes,
                resources.cap_indices->constData());
            this->cap_index_upload_pending = false;
        }
    }

    if (frame.wireframe_visible && this->wireframe_vertex_upload_pending)
    {
        if (resources.wireframe_vertices->isEmpty())
        {
            this->wireframe_vertex_buffer.reset();
            this->wireframe_vertex_buffer_size = 0;
            this->wireframe_vertex_upload_pending = false;
        }
        else
        {
            int required_bytes = 0;
            if (!byteCountFitsInt(
                    resources.wireframe_vertices->size(),
                    qsizetype(sizeof(MapGlobeSurfaceWireframeVertex)),
                    &required_bytes)
                || !ensureDynamicBuffer(
                    rhi, &this->wireframe_vertex_buffer,
                    &this->wireframe_vertex_buffer_size,
                    QRhiBuffer::VertexBuffer, required_bytes))
            {
                return false;
            }

            resource_updates->updateDynamicBuffer(
                this->wireframe_vertex_buffer.get(), 0, required_bytes,
                resources.wireframe_vertices->constData());
            this->wireframe_vertex_upload_pending = false;
        }
    }

    return true;
}

bool MapRhiGlobeSurfaceBackend::recreateRgba8Texture(
    QRhi *rhi,
    const QSize &size,
    std::unique_ptr<QRhiTexture> *texture)
{
    if (rhi == nullptr || texture == nullptr || !size.isValid())
        return false;

    texture->reset(rhi->newTexture(QRhiTexture::RGBA8, size));
    if (!*texture || !(*texture)->create())
    {
        texture->reset();
        return false;
    }
    return true;
}

bool MapRhiGlobeSurfaceBackend::supportsTextureArrays(QRhi *rhi) const
{
    return rhi != nullptr && rhi->isFeatureSupported(QRhi::TextureArrays);
}

bool MapRhiGlobeSurfaceBackend::supportsR32fTextures(QRhi *rhi) const
{
    return rhi != nullptr && rhi->isTextureFormatSupported(QRhiTexture::R32F);
}

bool MapRhiGlobeSurfaceBackend::isYUpInNdc(QRhi *rhi) const
{
    return rhi != nullptr && rhi->isYUpInNDC();
}

void MapRhiGlobeSurfaceBackend::uploadTextureImage(
    QRhiResourceUpdateBatch *resource_updates,
    QRhiTexture *texture,
    const QImage &image) const
{
    if (resource_updates == nullptr || texture == nullptr || image.isNull())
        return;

    resource_updates->uploadTexture(texture, image);
}

void MapRhiGlobeSurfaceBackend::uploadTextureArrayLayer(
    QRhiResourceUpdateBatch *resource_updates,
    QRhiTexture *texture,
    int layer,
    const QImage &image) const
{
    if (resource_updates == nullptr || texture == nullptr || layer < 0
        || image.isNull())
    {
        return;
    }

    const QRhiTextureSubresourceUploadDescription subresource(image);
    const QRhiTextureUploadEntry entry(layer, 0, subresource);
    resource_updates->uploadTexture(
        texture, QRhiTextureUploadDescription(entry));
}

void MapRhiGlobeSurfaceBackend::uploadTextureArrayLayerRaw(
    QRhiResourceUpdateBatch *resource_updates,
    QRhiTexture *texture,
    int layer,
    const QByteArray &data) const
{
    if (resource_updates == nullptr || texture == nullptr || layer < 0
        || data.isEmpty())
    {
        return;
    }

    const QRhiTextureSubresourceUploadDescription subresource(data);
    const QRhiTextureUploadEntry entry(layer, 0, subresource);
    resource_updates->uploadTexture(
        texture, QRhiTextureUploadDescription(entry));
}


MapRhiGlobeSurfaceBackend::TileGpuResource *
MapRhiGlobeSurfaceBackend::tileGpuResource(quint64 resource_id)
{
    if (resource_id == 0)
        return nullptr;
    std::map<quint64, TileGpuResource>::iterator iterator =
        this->tile_gpu_resources.find(resource_id);
    return iterator != this->tile_gpu_resources.end()
        ? &iterator->second : nullptr;
}

const MapRhiGlobeSurfaceBackend::TileGpuResource *
MapRhiGlobeSurfaceBackend::tileGpuResource(quint64 resource_id) const
{
    if (resource_id == 0)
        return nullptr;
    std::map<quint64, TileGpuResource>::const_iterator iterator =
        this->tile_gpu_resources.find(resource_id);
    return iterator != this->tile_gpu_resources.end()
        ? &iterator->second : nullptr;
}

quint64 MapRhiGlobeSurfaceBackend::createTileGpuResource()
{
    quint64 resource_id = this->next_tile_gpu_resource_id++;
    if (resource_id == 0)
        resource_id = this->next_tile_gpu_resource_id++;
    this->tile_gpu_resources.emplace(resource_id, TileGpuResource());
    return resource_id;
}

void MapRhiGlobeSurfaceBackend::releaseTileGpuResource(quint64 resource_id)
{
    if (resource_id != 0)
        this->tile_gpu_resources.erase(resource_id);
}

void MapRhiGlobeSurfaceBackend::clearTileGpuResources()
{
    this->tile_gpu_resources.clear();
}

bool MapRhiGlobeSurfaceBackend::hasTileTexture(quint64 resource_id) const
{
    const TileGpuResource *resource = tileGpuResource(resource_id);
    return resource != nullptr && resource->texture != nullptr;
}

bool MapRhiGlobeSurfaceBackend::hasTileHeatmapTexture(quint64 resource_id) const
{
    const TileGpuResource *resource = tileGpuResource(resource_id);
    return resource != nullptr && resource->heatmap_texture != nullptr;
}

bool MapRhiGlobeSurfaceBackend::hasTileBindings(quint64 resource_id) const
{
    const TileGpuResource *resource = tileGpuResource(resource_id);
    return resource != nullptr && resource->bindings != nullptr;
}

QRhiTexture *MapRhiGlobeSurfaceBackend::tileTexture(quint64 resource_id) const
{
    const TileGpuResource *resource = tileGpuResource(resource_id);
    return resource != nullptr ? resource->texture.get() : nullptr;
}

QRhiTexture *MapRhiGlobeSurfaceBackend::tileHeatmapTexture(
    quint64 resource_id) const
{
    const TileGpuResource *resource = tileGpuResource(resource_id);
    return resource != nullptr ? resource->heatmap_texture.get() : nullptr;
}

QRhiShaderResourceBindings *MapRhiGlobeSurfaceBackend::tileBindings(
    quint64 resource_id) const
{
    const TileGpuResource *resource = tileGpuResource(resource_id);
    return resource != nullptr ? resource->bindings.get() : nullptr;
}

void MapRhiGlobeSurfaceBackend::invalidateTileBindings(quint64 resource_id)
{
    TileGpuResource *resource = tileGpuResource(resource_id);
    if (resource != nullptr)
        resource->bindings.reset();
}

void MapRhiGlobeSurfaceBackend::clearTileHeatmapTexture(quint64 resource_id)
{
    TileGpuResource *resource = tileGpuResource(resource_id);
    if (resource == nullptr)
        return;
    resource->bindings.reset();
    resource->heatmap_texture.reset();
}

bool MapRhiGlobeSurfaceBackend::recreateTileTexture(
    QRhi *rhi, quint64 resource_id, const QSize &size)
{
    TileGpuResource *resource = tileGpuResource(resource_id);
    if (resource == nullptr)
        return false;
    resource->bindings.reset();
    return recreateRgba8Texture(rhi, size, &resource->texture);
}

bool MapRhiGlobeSurfaceBackend::recreateTileHeatmapTexture(
    QRhi *rhi, quint64 resource_id, const QSize &size)
{
    TileGpuResource *resource = tileGpuResource(resource_id);
    if (resource == nullptr)
        return false;
    resource->bindings.reset();
    return recreateRgba8Texture(rhi, size, &resource->heatmap_texture);
}

bool MapRhiGlobeSurfaceBackend::rebuildTileBindings(
    QRhi *rhi, quint64 resource_id)
{
    TileGpuResource *resource = tileGpuResource(resource_id);
    if (resource == nullptr || resource->texture == nullptr)
        return false;
    return rebuildTileBindings(
        rhi, resource->texture.get(), resource->heatmap_texture.get(),
        &resource->bindings);
}

void MapRhiGlobeSurfaceBackend::releaseDiagnosticHeatmapBakeResources()
{
    this->heatmap_bake_resources.diagnostic_pipeline.reset();
    this->heatmap_bake_resources.diagnostic_bindings.reset();
    this->heatmap_bake_resources.diagnostic_target.reset();
    this->heatmap_bake_resources.diagnostic_render_pass_descriptor.reset();
    this->heatmap_bake_resources.diagnostic_texture.reset();
    this->heatmap_bake_resources.diagnostic_vertex_buffer.reset();
    this->heatmap_bake_resources.diagnostic_instance_buffer.reset();
    this->heatmap_bake_resources.diagnostic_instance_buffer_size = 0;
    this->heatmap_bake_resources.diagnostic_vertex_upload_pending = true;
}

void MapRhiGlobeSurfaceBackend::releaseVisibleHeatmapBakeAtlasResources()
{
    this->heatmap_bake_resources.visible_atlases.clear();
    this->heatmap_bake_resources.maximum_visible_atlas_slots = 0;
}

bool MapRhiGlobeSurfaceBackend::ensureHeatmapBakeResources(
    QRhi *rhi,
    int texture_size)
{
    static_assert(
        sizeof(MapRhiGlobeHeatmapBakeVertex) == 2 * sizeof(float),
        "Heatmap bake vertex layout must stay tightly packed");
    static_assert(
        sizeof(MapRhiGlobeHeatmapBakeInstance) == 9 * sizeof(float),
        "Heatmap bake instance layout must stay tightly packed");

    if (rhi == nullptr || texture_size <= 0)
        return false;

    if (!this->heatmap_bake_resources.diagnostic_texture)
    {
        this->heatmap_bake_resources.diagnostic_texture.reset(
            rhi->newTexture(
                QRhiTexture::RGBA8,
                QSize(texture_size, texture_size), 1,
                QRhiTexture::RenderTarget
                    | QRhiTexture::UsedAsTransferSource));
        if (!this->heatmap_bake_resources.diagnostic_texture
            || !this->heatmap_bake_resources.diagnostic_texture->create())
        {
            releaseDiagnosticHeatmapBakeResources();
            return false;
        }
    }

    if (!this->heatmap_bake_resources.diagnostic_target)
    {
        const QRhiTextureRenderTargetDescription target_description(
            QRhiColorAttachment(
                this->heatmap_bake_resources.diagnostic_texture.get()));
        this->heatmap_bake_resources.diagnostic_target.reset(
            rhi->newTextureRenderTarget(target_description));
        if (!this->heatmap_bake_resources.diagnostic_target)
        {
            releaseDiagnosticHeatmapBakeResources();
            return false;
        }
        this->heatmap_bake_resources.diagnostic_render_pass_descriptor.reset(
            this->heatmap_bake_resources.diagnostic_target
                ->newCompatibleRenderPassDescriptor());
        if (!this->heatmap_bake_resources.diagnostic_render_pass_descriptor)
        {
            releaseDiagnosticHeatmapBakeResources();
            return false;
        }
        this->heatmap_bake_resources.diagnostic_target->setRenderPassDescriptor(
            this->heatmap_bake_resources.diagnostic_render_pass_descriptor.get());
        if (!this->heatmap_bake_resources.diagnostic_target->create())
        {
            releaseDiagnosticHeatmapBakeResources();
            return false;
        }
    }

    if (!this->heatmap_bake_resources.diagnostic_bindings)
    {
        this->heatmap_bake_resources.diagnostic_bindings.reset(
            rhi->newShaderResourceBindings());
        if (!this->heatmap_bake_resources.diagnostic_bindings)
        {
            releaseDiagnosticHeatmapBakeResources();
            return false;
        }
        this->heatmap_bake_resources.diagnostic_bindings->setBindings({});
        if (!this->heatmap_bake_resources.diagnostic_bindings->create())
        {
            releaseDiagnosticHeatmapBakeResources();
            return false;
        }
    }

    if (!this->heatmap_bake_resources.diagnostic_vertex_buffer)
    {
        constexpr int VertexCount = 6;
        const int vertex_bytes =
            VertexCount * int(sizeof(MapRhiGlobeHeatmapBakeVertex));
        this->heatmap_bake_resources.diagnostic_vertex_buffer.reset(
            rhi->newBuffer(
                QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                vertex_bytes));
        if (!this->heatmap_bake_resources.diagnostic_vertex_buffer
            || !this->heatmap_bake_resources.diagnostic_vertex_buffer->create())
        {
            releaseDiagnosticHeatmapBakeResources();
            return false;
        }
        this->heatmap_bake_resources.diagnostic_vertex_upload_pending = true;
    }

    if (!this->heatmap_bake_resources.diagnostic_pipeline)
    {
        const QShader vertex_shader = loadSurfaceShader(QStringLiteral(
            ":/aowis/map/rhi/map_rhi_globe_heatmap_bake.vert.qsb"));
        const QShader fragment_shader = loadSurfaceShader(QStringLiteral(
            ":/aowis/map/rhi/map_rhi_globe_heatmap_bake.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            releaseDiagnosticHeatmapBakeResources();
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiGlobeHeatmapBakeVertex))},
            {quint32(sizeof(MapRhiGlobeHeatmapBakeInstance)),
             QRhiVertexInputBinding::PerInstance}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiGlobeHeatmapBakeVertex, corner_x))},
            {1, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiGlobeHeatmapBakeInstance, center_x_pixels))},
            {1, 2, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiGlobeHeatmapBakeInstance, radius_pixels))},
            {1, 3, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiGlobeHeatmapBakeInstance, red))},
            {1, 4, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiGlobeHeatmapBakeInstance, target_scale_x))}
        });

        this->heatmap_bake_resources.diagnostic_pipeline.reset(
            rhi->newGraphicsPipeline());
        if (!this->heatmap_bake_resources.diagnostic_pipeline)
        {
            releaseDiagnosticHeatmapBakeResources();
            return false;
        }
        this->heatmap_bake_resources.diagnostic_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->heatmap_bake_resources.diagnostic_pipeline->setVertexInputLayout(
            input_layout);
        this->heatmap_bake_resources.diagnostic_pipeline->setShaderResourceBindings(
            this->heatmap_bake_resources.diagnostic_bindings.get());
        this->heatmap_bake_resources.diagnostic_pipeline->setRenderPassDescriptor(
            this->heatmap_bake_resources.diagnostic_render_pass_descriptor.get());
        this->heatmap_bake_resources.diagnostic_pipeline->setTopology(
            QRhiGraphicsPipeline::Triangles);
        this->heatmap_bake_resources.diagnostic_pipeline->setSampleCount(1);
        this->heatmap_bake_resources.diagnostic_pipeline->setCullMode(
            QRhiGraphicsPipeline::None);
        this->heatmap_bake_resources.diagnostic_pipeline->setDepthTest(false);
        this->heatmap_bake_resources.diagnostic_pipeline->setDepthWrite(false);
        QRhiGraphicsPipeline::TargetBlend heatmap_blend;
        heatmap_blend.enable = true;
        this->heatmap_bake_resources.diagnostic_pipeline->setTargetBlends({
            heatmap_blend
        });
        if (!this->heatmap_bake_resources.diagnostic_pipeline->create())
        {
            releaseDiagnosticHeatmapBakeResources();
            return false;
        }
    }

    return true;
}

MapRhiGlobeHeatmapBakeAtlas *
MapRhiGlobeSurfaceBackend::ensureVisibleHeatmapBakeAtlasResources(
    QRhi *rhi,
    int slot_count,
    int texture_size)
{
    if (rhi == nullptr || slot_count <= 0 || texture_size <= 0)
        return nullptr;

    const std::map<int, MapRhiGlobeHeatmapBakeAtlas>::iterator existing =
        this->heatmap_bake_resources.visible_atlases.find(slot_count);
    if (existing != this->heatmap_bake_resources.visible_atlases.end())
    {
        MapRhiGlobeHeatmapBakeAtlas &atlas = existing->second;
        if (atlas.texture && atlas.render_pass_descriptor && atlas.target)
            return &atlas;
        this->heatmap_bake_resources.visible_atlases.erase(existing);
    }

    MapRhiGlobeHeatmapBakeAtlas atlas;
    atlas.slot_count = slot_count;
    const QSize atlas_size(slot_count * texture_size, texture_size);
    atlas.texture.reset(rhi->newTexture(
        QRhiTexture::RGBA8, atlas_size, 1,
        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!atlas.texture || !atlas.texture->create())
        return nullptr;

    const QRhiTextureRenderTargetDescription target_description(
        QRhiColorAttachment(atlas.texture.get()));
    atlas.target.reset(rhi->newTextureRenderTarget(target_description));
    if (!atlas.target)
        return nullptr;
    atlas.render_pass_descriptor.reset(
        atlas.target->newCompatibleRenderPassDescriptor());
    if (!atlas.render_pass_descriptor)
        return nullptr;
    atlas.target->setRenderPassDescriptor(atlas.render_pass_descriptor.get());
    if (!atlas.target->create())
        return nullptr;

    this->heatmap_bake_resources.visible_atlases[slot_count] = std::move(atlas);
    return &this->heatmap_bake_resources.visible_atlases.at(slot_count);
}

int MapRhiGlobeSurfaceBackend::maximumVisibleHeatmapBakeAtlasSlots(
    QRhi *rhi,
    int texture_size,
    int maximum_slots)
{
    if (this->heatmap_bake_resources.maximum_visible_atlas_slots > 0)
        return this->heatmap_bake_resources.maximum_visible_atlas_slots;
    if (rhi == nullptr || texture_size <= 0 || maximum_slots <= 0)
        return 0;

    const int maximum_texture_size = qMax(
        texture_size, rhi->resourceLimit(QRhi::TextureSizeMax));
    const int maximum_candidate = qBound(
        1, maximum_texture_size / texture_size, maximum_slots);
    int slot_count = 1;
    while (slot_count <= maximum_candidate / 2)
        slot_count *= 2;

    while (slot_count > 0)
    {
        if (ensureVisibleHeatmapBakeAtlasResources(
                rhi, slot_count, texture_size) != nullptr)
        {
            this->heatmap_bake_resources.maximum_visible_atlas_slots =
                slot_count;
            return slot_count;
        }
        slot_count /= 2;
    }
    return 0;
}

bool MapRhiGlobeSurfaceBackend::ensureVisibleHeatmapBakeInstanceBuffer(
    QRhi *rhi,
    int required_bytes)
{
    if (rhi == nullptr || required_bytes <= 0)
        return false;
    if (this->heatmap_bake_resources.visible_instance_buffer
        && this->heatmap_bake_resources.visible_instance_buffer_size
            >= required_bytes)
    {
        return true;
    }

    this->heatmap_bake_resources.visible_instance_buffer.reset(
        rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
            required_bytes));
    if (!this->heatmap_bake_resources.visible_instance_buffer
        || !this->heatmap_bake_resources.visible_instance_buffer->create())
    {
        this->heatmap_bake_resources.visible_instance_buffer.reset();
        this->heatmap_bake_resources.visible_instance_buffer_size = 0;
        return false;
    }
    this->heatmap_bake_resources.visible_instance_buffer_size = required_bytes;
    return true;
}

bool MapRhiGlobeSurfaceBackend::ensureDiagnosticHeatmapBakeInstanceBuffer(
    QRhi *rhi,
    int required_bytes)
{
    if (rhi == nullptr || required_bytes <= 0)
        return false;
    if (this->heatmap_bake_resources.diagnostic_instance_buffer
        && this->heatmap_bake_resources.diagnostic_instance_buffer_size
            >= required_bytes)
    {
        return true;
    }

    this->heatmap_bake_resources.diagnostic_instance_buffer.reset(
        rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
            required_bytes));
    if (!this->heatmap_bake_resources.diagnostic_instance_buffer
        || !this->heatmap_bake_resources.diagnostic_instance_buffer->create())
    {
        this->heatmap_bake_resources.diagnostic_instance_buffer.reset();
        this->heatmap_bake_resources.diagnostic_instance_buffer_size = 0;
        return false;
    }
    this->heatmap_bake_resources.diagnostic_instance_buffer_size = required_bytes;
    return true;
}

bool MapRhiGlobeSurfaceBackend::recordVisibleHeatmapBakes(
    QRhi *rhi,
    QRhiCommandBuffer *command_buffer,
    const QVector<MapRhiGlobeHeatmapBakeJob> &jobs,
    int texture_size,
    int array_layer_count,
    int maximum_atlas_slots,
    MapRhiGlobeHeatmapBakeExecutionStats *stats)
{
    if (stats != nullptr)
        *stats = MapRhiGlobeHeatmapBakeExecutionStats();
    if (jobs.isEmpty())
        return true;
    if (rhi == nullptr || command_buffer == nullptr
        || texture_size <= 0 || array_layer_count <= 0
        || maximum_atlas_slots <= 0)
    {
        return false;
    }

    qsizetype total_instance_count = 0;
    for (const MapRhiGlobeHeatmapBakeJob &job : jobs)
    {
        if (job.destination_texture == nullptr
            || job.destination_layer < 0
            || job.destination_layer >= array_layer_count
            || job.instances == nullptr || job.instances->isEmpty()
            || total_instance_count
                > std::numeric_limits<qsizetype>::max()
                    - job.instances->size())
        {
            return false;
        }
        total_instance_count += job.instances->size();
    }

    if (total_instance_count
        > qsizetype(std::numeric_limits<int>::max())
            / qsizetype(sizeof(MapRhiGlobeHeatmapBakeInstance)))
    {
        return false;
    }

    if (!ensureHeatmapBakeResources(rhi, texture_size))
        return false;

    const int available_atlas_slots = maximumVisibleHeatmapBakeAtlasSlots(
        rhi, texture_size, maximum_atlas_slots);
    if (available_atlas_slots <= 0)
        return false;

    const int required_bytes = int(
        total_instance_count
        * qsizetype(sizeof(MapRhiGlobeHeatmapBakeInstance)));
    if (!ensureVisibleHeatmapBakeInstanceBuffer(rhi, required_bytes))
        return false;

    struct HeatmapGpuBakePage
    {
        int first_job = 0;
        int last_job = 0;
        MapRhiGlobeHeatmapBakeAtlas *atlas = nullptr;
        qsizetype first_instance = 0;
        qsizetype instance_count = 0;
    };

    const int job_count = int(jobs.size());
    QVector<HeatmapGpuBakePage> pages;
    pages.reserve(
        (job_count + available_atlas_slots - 1) / available_atlas_slots);
    QVector<MapRhiGlobeHeatmapBakeInstance> all_instances;
    all_instances.reserve(int(total_instance_count));
    for (int first_job = 0; first_job < job_count;)
    {
        HeatmapGpuBakePage page;
        page.first_job = first_job;
        page.last_job = qMin(first_job + available_atlas_slots, job_count);

        const int page_tile_count = page.last_job - page.first_job;
        int requested_slots = 1;
        while (requested_slots < page_tile_count)
            requested_slots *= 2;
        page.atlas = ensureVisibleHeatmapBakeAtlasResources(
            rhi, requested_slots, texture_size);
        if (page.atlas == nullptr)
        {
            page.atlas = ensureVisibleHeatmapBakeAtlasResources(
                rhi, available_atlas_slots, texture_size);
        }
        if (page.atlas == nullptr
            || page.atlas->slot_count < page_tile_count)
        {
            return false;
        }

        page.first_instance = all_instances.size();
        const float atlas_slot_scale = 1.0f / float(page.atlas->slot_count);
        for (int job_index = page.first_job;
             job_index < page.last_job; ++job_index)
        {
            const MapRhiGlobeHeatmapBakeJob &job = jobs.at(job_index);
            const int slot = job_index - page.first_job;
            for (const MapRhiGlobeHeatmapBakeInstance &source_instance
                 : *job.instances)
            {
                MapRhiGlobeHeatmapBakeInstance instance = source_instance;
                instance.target_scale_x = atlas_slot_scale;
                instance.target_offset_x = float(slot) * atlas_slot_scale;
                all_instances.append(instance);
            }
        }
        page.instance_count = all_instances.size() - page.first_instance;
        pages.append(page);
        first_job = page.last_job;
    }

    QRhiResourceUpdateBatch *bake_updates = rhi->nextResourceUpdateBatch();
    if (bake_updates == nullptr)
        return false;

    const bool upload_bake_vertices =
        this->heatmap_bake_resources.diagnostic_vertex_upload_pending;
    if (upload_bake_vertices)
    {
        const MapRhiGlobeHeatmapBakeVertex vertices[] = {
            {-1.0f, -1.0f}, {1.0f, -1.0f},
            {-1.0f, 1.0f}, {-1.0f, 1.0f},
            {1.0f, -1.0f}, {1.0f, 1.0f}
        };
        bake_updates->uploadStaticBuffer(
            this->heatmap_bake_resources.diagnostic_vertex_buffer.get(),
            vertices);
    }
    bake_updates->updateDynamicBuffer(
        this->heatmap_bake_resources.visible_instance_buffer.get(), 0,
        required_bytes, all_instances.constData());

    QVector<QRhiResourceUpdateBatch *> page_copy_updates;
    page_copy_updates.reserve(pages.size());
    for (const HeatmapGpuBakePage &page : pages)
    {
        QRhiResourceUpdateBatch *copy_updates = rhi->nextResourceUpdateBatch();
        if (copy_updates == nullptr)
        {
            for (QRhiResourceUpdateBatch *allocated_updates : page_copy_updates)
                allocated_updates->release();
            bake_updates->release();
            if (upload_bake_vertices)
            {
                this->heatmap_bake_resources.diagnostic_vertex_upload_pending = true;
            }
            return false;
        }

        for (int job_index = page.first_job;
             job_index < page.last_job; ++job_index)
        {
            const MapRhiGlobeHeatmapBakeJob &job = jobs.at(job_index);
            const int slot = job_index - page.first_job;
            QRhiTextureCopyDescription copy_description;
            copy_description.setSourceTopLeft(QPoint(slot * texture_size, 0));
            copy_description.setDestinationLayer(job.destination_layer);
            copy_description.setPixelSize(QSize(texture_size, texture_size));
            copy_updates->copyTexture(
                job.destination_texture, page.atlas->texture.get(),
                copy_description);
        }
        page_copy_updates.append(copy_updates);
    }
    if (upload_bake_vertices)
        this->heatmap_bake_resources.diagnostic_vertex_upload_pending = false;

    MapRhiGlobeHeatmapBakeExecutionStats execution_stats;
    execution_stats.maximum_atlas_slots = available_atlas_slots;
    for (int page_index = 0; page_index < pages.size(); ++page_index)
    {
        const HeatmapGpuBakePage &page = pages.at(page_index);
        const QSize atlas_size(
            page.atlas->slot_count * texture_size, texture_size);

        command_buffer->beginPass(
            page.atlas->target.get(), Qt::transparent, {1.0f, 0},
            page_index == 0 ? bake_updates : nullptr);
        command_buffer->setGraphicsPipeline(
            this->heatmap_bake_resources.diagnostic_pipeline.get());
        command_buffer->setViewport(QRhiViewport(
            0.0f, 0.0f, float(atlas_size.width()), float(atlas_size.height())));
        command_buffer->setShaderResources(
            this->heatmap_bake_resources.diagnostic_bindings.get());
        const QRhiCommandBuffer::VertexInput bindings[] = {
            {this->heatmap_bake_resources.diagnostic_vertex_buffer.get(), 0},
            {this->heatmap_bake_resources.visible_instance_buffer.get(),
             quint32(page.first_instance
                 * qsizetype(sizeof(MapRhiGlobeHeatmapBakeInstance)))}
        };
        command_buffer->setVertexInput(0, 2, bindings);
        command_buffer->draw(6, quint32(page.instance_count));
        command_buffer->endPass(page_copy_updates.at(page_index));

        const int page_tile_count = page.last_job - page.first_job;
        execution_stats.recorded_tiles += page_tile_count;
        execution_stats.recorded_stamps += int(page.instance_count);
        ++execution_stats.recorded_passes;
        ++execution_stats.recorded_copy_batches;
        for (int job_index = page.first_job;
             job_index < page.last_job; ++job_index)
        {
            if (jobs.at(job_index).destination_layer > 0)
                ++execution_stats.recorded_array_copies;
        }
    }

    if (stats != nullptr)
        *stats = execution_stats;
    return true;
}

bool MapRhiGlobeSurfaceBackend::recordDiagnosticHeatmapBake(
    QRhi *rhi,
    QRhiCommandBuffer *command_buffer,
    const QVector<MapRhiGlobeHeatmapBakeInstance> &instances,
    int texture_size,
    QString *failure_reason)
{
    if (failure_reason != nullptr)
        failure_reason->clear();
    if (command_buffer == nullptr)
    {
        if (failure_reason != nullptr)
            *failure_reason = QStringLiteral("missing_command_buffer");
        return false;
    }

    const qsizetype required_bytes_qsize = instances.size()
        * qsizetype(sizeof(MapRhiGlobeHeatmapBakeInstance));
    if (required_bytes_qsize <= 0
        || required_bytes_qsize
            > qsizetype(std::numeric_limits<int>::max()))
    {
        if (failure_reason != nullptr)
            *failure_reason = QStringLiteral("invalid_instance_buffer_size");
        return false;
    }
    if (!ensureHeatmapBakeResources(rhi, texture_size))
    {
        if (failure_reason != nullptr)
            *failure_reason = QStringLiteral("resource_creation_failed");
        return false;
    }

    const int required_bytes = int(required_bytes_qsize);
    if (!ensureDiagnosticHeatmapBakeInstanceBuffer(rhi, required_bytes))
    {
        if (failure_reason != nullptr)
        {
            *failure_reason = QStringLiteral(
                "instance_buffer_creation_failed");
        }
        return false;
    }

    QRhiResourceUpdateBatch *resource_updates = rhi->nextResourceUpdateBatch();
    if (resource_updates == nullptr)
    {
        if (failure_reason != nullptr)
            *failure_reason = QStringLiteral("update_batch_unavailable");
        return false;
    }

    if (this->heatmap_bake_resources.diagnostic_vertex_upload_pending)
    {
        const MapRhiGlobeHeatmapBakeVertex vertices[] = {
            {-1.0f, -1.0f}, {1.0f, -1.0f},
            {-1.0f, 1.0f}, {-1.0f, 1.0f},
            {1.0f, -1.0f}, {1.0f, 1.0f}
        };
        resource_updates->uploadStaticBuffer(
            this->heatmap_bake_resources.diagnostic_vertex_buffer.get(),
            vertices);
        this->heatmap_bake_resources.diagnostic_vertex_upload_pending = false;
    }
    resource_updates->updateDynamicBuffer(
        this->heatmap_bake_resources.diagnostic_instance_buffer.get(), 0,
        required_bytes, instances.constData());

    command_buffer->beginPass(
        this->heatmap_bake_resources.diagnostic_target.get(),
        Qt::transparent, {1.0f, 0}, resource_updates);
    command_buffer->setGraphicsPipeline(
        this->heatmap_bake_resources.diagnostic_pipeline.get());
    command_buffer->setViewport(QRhiViewport(
        0.0f, 0.0f, float(texture_size), float(texture_size)));
    command_buffer->setShaderResources(
        this->heatmap_bake_resources.diagnostic_bindings.get());
    const QRhiCommandBuffer::VertexInput bindings[] = {
        {this->heatmap_bake_resources.diagnostic_vertex_buffer.get(), 0},
        {this->heatmap_bake_resources.diagnostic_instance_buffer.get(), 0}
    };
    command_buffer->setVertexInput(0, 2, bindings);
    command_buffer->draw(6, quint32(instances.size()));
    command_buffer->endPass();
    return true;
}

bool MapRhiGlobeSurfaceBackend::queueDiagnosticHeatmapReadback(
    QRhi *rhi,
    QRhiCommandBuffer *command_buffer,
    QRhiReadbackResult *readback_result) const
{
    if (rhi == nullptr || command_buffer == nullptr
        || readback_result == nullptr
        || !this->heatmap_bake_resources.diagnostic_texture)
    {
        return false;
    }

    QRhiResourceUpdateBatch *readback_updates = rhi->nextResourceUpdateBatch();
    if (readback_updates == nullptr)
        return false;
    readback_updates->readBackTexture(
        QRhiReadbackDescription(
            this->heatmap_bake_resources.diagnostic_texture.get()),
        readback_result);
    command_buffer->resourceUpdate(readback_updates);
    return true;
}

bool MapRhiGlobeSurfaceBackend::uploadImageryArrayDrawIndices(
    QRhi *rhi,
    QRhiResourceUpdateBatch *resource_updates,
    const QVector<quint32> &indices,
    qsizetype maximum_index_count)
{
    if (rhi == nullptr || resource_updates == nullptr)
        return false;
    if (indices.isEmpty())
        return true;

    int required_bytes = 0;
    int maximum_bytes = 0;
    if (!byteCountFitsInt(
            indices.size(), qsizetype(sizeof(quint32)), &required_bytes)
        || !byteCountFitsInt(
            qMax(indices.size(), maximum_index_count),
            qsizetype(sizeof(quint32)), &maximum_bytes))
    {
        return false;
    }

    if (!this->tile_array_draw_index_buffer
        || this->tile_array_draw_index_buffer_size < required_bytes)
    {
        this->tile_array_draw_index_buffer.reset(rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, maximum_bytes));
        if (!this->tile_array_draw_index_buffer
            || !this->tile_array_draw_index_buffer->create())
        {
            return false;
        }
        this->tile_array_draw_index_buffer_size = maximum_bytes;
    }

    resource_updates->updateDynamicBuffer(
        this->tile_array_draw_index_buffer.get(), 0, required_bytes,
        indices.constData());
    return true;
}

bool MapRhiGlobeSurfaceBackend::uploadHeatmapArrayDrawIndices(
    QRhi *rhi,
    QRhiResourceUpdateBatch *resource_updates,
    const QVector<quint32> &indices,
    qsizetype maximum_index_count)
{
    if (rhi == nullptr || resource_updates == nullptr)
        return false;
    if (indices.isEmpty())
        return true;

    int required_bytes = 0;
    int maximum_bytes = 0;
    if (!byteCountFitsInt(
            indices.size(), qsizetype(sizeof(quint32)), &required_bytes)
        || !byteCountFitsInt(
            maximum_index_count, qsizetype(sizeof(quint32)), &maximum_bytes))
    {
        return false;
    }
    maximum_bytes = qMax(maximum_bytes, required_bytes);

    if (!this->heatmap_array_draw_index_buffer
        || this->heatmap_array_draw_index_buffer_size < required_bytes)
    {
        const int growth_bytes = this->heatmap_array_draw_index_buffer_size
            + qMax(this->heatmap_array_draw_index_buffer_size / 2, 65536);
        const int allocation_bytes = qMin(
            maximum_bytes, qMax(required_bytes, growth_bytes));
        this->heatmap_array_draw_index_buffer.reset(rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer,
            allocation_bytes));
        if (!this->heatmap_array_draw_index_buffer
            || !this->heatmap_array_draw_index_buffer->create())
        {
            return false;
        }
        this->heatmap_array_draw_index_buffer_size = allocation_bytes;
    }

    resource_updates->updateDynamicBuffer(
        this->heatmap_array_draw_index_buffer.get(), 0, required_bytes,
        indices.constData());
    return true;
}

bool MapRhiGlobeSurfaceBackend::uploadHeatmapArrayLayers(
    QRhi *rhi,
    QRhiResourceUpdateBatch *resource_updates,
    const QVector<float> &layers)
{
    if (rhi == nullptr || resource_updates == nullptr || layers.isEmpty())
        return false;

    int required_bytes = 0;
    if (!byteCountFitsInt(
            layers.size(), qsizetype(sizeof(float)), &required_bytes)
        || !ensureDynamicBuffer(
            rhi, &this->heatmap_array_layer_buffer,
            &this->heatmap_array_layer_buffer_size,
            QRhiBuffer::VertexBuffer, required_bytes))
    {
        return false;
    }

    resource_updates->updateDynamicBuffer(
        this->heatmap_array_layer_buffer.get(), 0, required_bytes,
        layers.constData());
    return true;
}

bool MapRhiGlobeSurfaceBackend::createImageryArrayPage(
    QRhi *rhi,
    const QSize &layer_size,
    int layer_count,
    int maximum_page_count)
{
    if (rhi == nullptr || this->camera_uniform_buffer == nullptr
        || this->sampler_resource == nullptr
        || !layer_size.isValid() || layer_count <= 1
        || maximum_page_count <= 0
        || int(this->imagery_array_pages.size()) >= maximum_page_count)
    {
        return false;
    }

    MapRhiGlobeImageryArrayPage page;
    page.texture.reset(rhi->newTextureArray(
        QRhiTexture::RGBA8, layer_count, layer_size));
    if (!page.texture || !page.texture->create())
        return false;

    page.bindings.reset(rhi->newShaderResourceBindings());
    if (!page.bindings)
        return false;
    page.bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage
                | QRhiShaderResourceBinding::FragmentStage,
            this->camera_uniform_buffer.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage,
            page.texture.get(), this->sampler_resource.get())
    });
    if (!page.bindings->create())
        return false;

    page.free_layers.reserve(layer_count - 1);
    for (int layer = layer_count - 1; layer >= 1; --layer)
        page.free_layers.append(layer);

    this->imagery_array_pages.push_back(std::move(page));
    return true;
}

bool MapRhiGlobeSurfaceBackend::createHeatmapArrayPage(
    QRhi *rhi,
    QRhiResourceUpdateBatch *resource_updates,
    const QSize &layer_size,
    int layer_count,
    int maximum_page_count)
{
    if (rhi == nullptr || resource_updates == nullptr
        || !layer_size.isValid() || layer_count <= 1
        || maximum_page_count <= 0
        || int(this->heatmap_array_pages.size()) >= maximum_page_count)
    {
        return false;
    }

    MapRhiGlobeHeatmapArrayPage page;
    page.texture.reset(rhi->newTextureArray(
        QRhiTexture::RGBA8, layer_count, layer_size));
    if (!page.texture || !page.texture->create())
        return false;

    QImage transparent_layer(
        layer_size, QImage::Format_RGBA8888_Premultiplied);
    transparent_layer.fill(Qt::transparent);
    const QRhiTextureSubresourceUploadDescription transparent_subresource(
        transparent_layer);
    const QRhiTextureUploadEntry transparent_entry(
        0, 0, transparent_subresource);
    resource_updates->uploadTexture(
        page.texture.get(), QRhiTextureUploadDescription(transparent_entry));

    page.free_layers.reserve(layer_count - 1);
    for (int layer = layer_count - 1; layer >= 1; --layer)
        page.free_layers.append(layer);

    this->heatmap_array_pages.push_back(std::move(page));
    return true;
}

bool MapRhiGlobeSurfaceBackend::createTerrainHeightArrayPage(
    QRhi *rhi,
    const QSize &layer_size,
    int layer_count,
    int maximum_page_count)
{
    if (rhi == nullptr || !layer_size.isValid() || layer_count <= 0
        || maximum_page_count <= 0
        || int(this->terrain_height_array_pages.size()) >= maximum_page_count
        || !rhi->isFeatureSupported(QRhi::TextureArrays)
        || !rhi->isTextureFormatSupported(QRhiTexture::R32F))
    {
        return false;
    }

    MapRhiGlobeTerrainHeightArrayPage page;
    page.texture.reset(rhi->newTextureArray(
        QRhiTexture::R32F, layer_count, layer_size));
    if (!page.texture || !page.texture->create())
        return false;

    page.free_layers.reserve(layer_count);
    for (int layer = layer_count - 1; layer >= 0; --layer)
        page.free_layers.append(layer);

    this->terrain_height_array_pages.push_back(std::move(page));
    return true;
}

bool MapRhiGlobeSurfaceBackend::patchWindowVertices(
    QRhiResourceUpdateBatch *resource_updates,
    int first_vertex,
    qsizetype vertex_count,
    const QVector<MapGlobeSurfaceVertex> &vertices)
{
    if (resource_updates == nullptr || first_vertex < 0 || vertex_count <= 0
        || this->window_vertex_upload_pending || !this->window_vertex_buffer
        || qsizetype(first_vertex) + vertex_count > vertices.size())
    {
        return false;
    }

    const qsizetype byte_offset_qsize =
        qsizetype(first_vertex) * qsizetype(sizeof(MapGlobeSurfaceVertex));
    const qsizetype byte_count_qsize =
        qsizetype(vertex_count) * qsizetype(sizeof(MapGlobeSurfaceVertex));
    const qsizetype byte_end_qsize = byte_offset_qsize + byte_count_qsize;
    if (byte_offset_qsize < 0 || byte_count_qsize <= 0
        || byte_end_qsize < byte_offset_qsize
        || byte_end_qsize > qsizetype(this->window_vertex_buffer_size)
        || byte_offset_qsize > qsizetype(std::numeric_limits<int>::max())
        || byte_count_qsize > qsizetype(std::numeric_limits<int>::max()))
    {
        return false;
    }

    resource_updates->updateDynamicBuffer(
        this->window_vertex_buffer.get(), int(byte_offset_qsize),
        int(byte_count_qsize), vertices.constData() + first_vertex);
    return true;
}

bool MapRhiGlobeSurfaceBackend::patchHeatmapArrayLayers(
    QRhiResourceUpdateBatch *resource_updates,
    int first_vertex,
    qsizetype vertex_count,
    const QVector<float> &layers)
{
    if (resource_updates == nullptr || first_vertex < 0 || vertex_count <= 0
        || !this->heatmap_array_layer_buffer
        || qsizetype(first_vertex) + vertex_count > layers.size())
    {
        return false;
    }

    const qsizetype byte_offset_qsize =
        qsizetype(first_vertex) * qsizetype(sizeof(float));
    const qsizetype byte_count_qsize =
        vertex_count * qsizetype(sizeof(float));
    const qsizetype byte_end_qsize = byte_offset_qsize + byte_count_qsize;
    if (byte_offset_qsize < 0 || byte_count_qsize <= 0
        || byte_end_qsize < byte_offset_qsize
        || byte_end_qsize > qsizetype(this->heatmap_array_layer_buffer_size)
        || byte_offset_qsize > qsizetype(std::numeric_limits<int>::max())
        || byte_count_qsize > qsizetype(std::numeric_limits<int>::max()))
    {
        return false;
    }

    resource_updates->updateDynamicBuffer(
        this->heatmap_array_layer_buffer.get(), int(byte_offset_qsize),
        int(byte_count_qsize), layers.constData() + first_vertex);
    return true;
}

void MapRhiGlobeSurfaceBackend::draw(
    QRhiCommandBuffer *command_buffer,
    const MapGlobeSurfaceRenderFrame &frame,
    const MapRhiGlobeSurfaceDrawResources &draw_resources) const
{
    if (command_buffer == nullptr || !frame.isValid())
        return;

    const MapGlobeSurfaceRenderResources &resources = frame.resources;
    if (frame.map_visible && draw_resources.tile_pipeline != nullptr)
    {
        const bool use_array = draw_resources.imagery_array_active
            && this->window_vertex_buffer
            && this->window_index_buffer
            && this->tile_array_draw_index_buffer != nullptr;
        const bool use_heatmap_array = use_array
            && draw_resources.heatmap_array_active
            && frame.heatmap_opacity > 0.0f
            && this->heatmap_array_layer_buffer != nullptr
            && this->heatmap_array_draw_index_buffer != nullptr
            && !draw_resources.heatmap_array_batches.empty();

        if (this->window_vertex_buffer && this->window_index_buffer)
        {
            if (use_heatmap_array)
            {
                command_buffer->setGraphicsPipeline(
                    draw_resources.heatmap_array_pipeline);
                const QRhiCommandBuffer::VertexInput bindings[] = {
                    {this->window_vertex_buffer.get(), 0},
                    {this->heatmap_array_layer_buffer.get(), 0}
                };
                command_buffer->setVertexInput(
                    0, 2, bindings,
                    this->heatmap_array_draw_index_buffer.get(), 0,
                    QRhiCommandBuffer::IndexUInt32);
                for (const MapRhiGlobeSurfaceBatch &batch :
                     draw_resources.heatmap_array_batches)
                {
                    if (batch.bindings == nullptr || batch.draw_index_count <= 0)
                        continue;
                    command_buffer->setShaderResources(batch.bindings);
                    command_buffer->drawIndexed(
                        quint32(batch.draw_index_count), 1,
                        quint32(batch.first_draw_index));
                }
            }
            else if (use_array)
            {
                command_buffer->setGraphicsPipeline(draw_resources.array_pipeline);
                const QRhiCommandBuffer::VertexInput binding(
                    this->window_vertex_buffer.get(), 0);
                command_buffer->setVertexInput(
                    0, 1, &binding,
                    this->tile_array_draw_index_buffer.get(), 0,
                    QRhiCommandBuffer::IndexUInt32);
                for (const MapRhiGlobeSurfaceBatch &batch :
                     draw_resources.imagery_array_batches)
                {
                    if (batch.bindings == nullptr || batch.draw_index_count <= 0)
                        continue;
                    command_buffer->setShaderResources(batch.bindings);
                    command_buffer->drawIndexed(
                        quint32(batch.draw_index_count), 1,
                        quint32(batch.first_draw_index));
                }
            }

            command_buffer->setGraphicsPipeline(draw_resources.tile_pipeline);
            for (const MapGlobeSurfaceTileRenderState &surface_tile :
                 resources.window_tiles)
            {
                if (surface_tile.surface_tile_index < 0
                    || surface_tile.surface_tile_index
                        >= draw_resources.window_tiles.size())
                {
                    continue;
                }

                const MapRhiGlobeSurfaceTileDrawState &tile_state =
                    draw_resources.window_tiles.at(
                        surface_tile.surface_tile_index);
                if (surface_tile.vertex_count <= 0
                    || surface_tile.index_count <= 0)
                {
                    continue;
                }
                if (use_array && tile_state.array_ready)
                    continue;

                QRhiShaderResourceBindings *bindings = tile_state.bindings;
                if (bindings == nullptr)
                    bindings = draw_resources.template_bindings;
                if (bindings == nullptr)
                    continue;

                command_buffer->setShaderResources(bindings);
                const QRhiCommandBuffer::VertexInput binding(
                    this->window_vertex_buffer.get(), 0);
                const quint32 index_byte_offset = quint32(
                    surface_tile.first_index * int(sizeof(quint32)));
                command_buffer->setVertexInput(
                    0, 1, &binding, this->window_index_buffer.get(),
                    index_byte_offset, QRhiCommandBuffer::IndexUInt32);
                command_buffer->drawIndexed(quint32(surface_tile.index_count));
            }
        }

        if (this->cap_vertex_buffer && this->cap_index_buffer)
        {
            command_buffer->setGraphicsPipeline(draw_resources.tile_pipeline);
            for (const MapGlobeSurfaceTileRenderState &surface_tile :
                 resources.cap_tiles)
            {
                if (surface_tile.surface_tile_index < 0
                    || surface_tile.surface_tile_index
                        >= draw_resources.cap_tiles.size())
                {
                    continue;
                }

                const MapRhiGlobeSurfaceTileDrawState &tile_state =
                    draw_resources.cap_tiles.at(surface_tile.surface_tile_index);
                if (tile_state.bindings == nullptr
                    || surface_tile.vertex_count <= 0
                    || surface_tile.index_count <= 0)
                {
                    continue;
                }

                command_buffer->setShaderResources(tile_state.bindings);
                const QRhiCommandBuffer::VertexInput binding(
                    this->cap_vertex_buffer.get(), 0);
                const quint32 index_byte_offset = quint32(
                    surface_tile.first_index * int(sizeof(quint32)));
                command_buffer->setVertexInput(
                    0, 1, &binding, this->cap_index_buffer.get(),
                    index_byte_offset, QRhiCommandBuffer::IndexUInt32);
                command_buffer->drawIndexed(quint32(surface_tile.index_count));
            }
        }
    }

    if (frame.wireframe_visible
        && draw_resources.wireframe_pipeline != nullptr
        && draw_resources.wireframe_bindings != nullptr
        && this->wireframe_vertex_buffer
        && !resources.wireframe_vertices->isEmpty())
    {
        command_buffer->setGraphicsPipeline(draw_resources.wireframe_pipeline);
        command_buffer->setShaderResources(draw_resources.wireframe_bindings);
        const QRhiCommandBuffer::VertexInput binding(
            this->wireframe_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(resources.wireframe_vertices->size()));
    }
}
