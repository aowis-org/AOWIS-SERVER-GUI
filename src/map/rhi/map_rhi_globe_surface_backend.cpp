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

bool MapRhiGlobeSurfaceBackend::hasContext() const
{
    return this->rhi != nullptr && this->render_pass_descriptor != nullptr;
}

bool MapRhiGlobeSurfaceBackend::contextMatches(QRhi *rhi) const
{
    return this->rhi == rhi;
}

bool MapRhiGlobeSurfaceBackend::renderPassMatches(
    QRhiRenderPassDescriptor *render_pass_descriptor,
    int sample_count) const
{
    return this->render_pass_descriptor == render_pass_descriptor
        && this->sample_count == sample_count;
}

void MapRhiGlobeSurfaceBackend::setContext(
    QRhi *rhi,
    QRhiRenderPassDescriptor *render_pass_descriptor,
    int sample_count)
{
    this->rhi = rhi;
    this->render_pass_descriptor = render_pass_descriptor;
    this->sample_count = sample_count;
}

bool MapRhiGlobeSurfaceBackend::ensureSharedResources()
{
    return ensureSharedResources(
        this->rhi, this->render_pass_descriptor, this->sample_count);
}

bool MapRhiGlobeSurfaceBackend::ensureImageryArrayPipeline()
{
    return ensureImageryArrayPipeline(
        this->rhi, this->render_pass_descriptor, this->sample_count);
}

bool MapRhiGlobeSurfaceBackend::ensureHeatmapArrayPipeline()
{
    return ensureHeatmapArrayPipeline(
        this->rhi, this->render_pass_descriptor, this->sample_count);
}

bool MapRhiGlobeSurfaceBackend::ensureHeatmapArrayBatchBinding(
    int imagery_page_index, int heatmap_page_index)
{
    return ensureHeatmapArrayBatchBinding(
        this->rhi, imagery_page_index, heatmap_page_index);
}

bool MapRhiGlobeSurfaceBackend::supportsTextureArrays() const
{
    return supportsTextureArrays(this->rhi);
}

bool MapRhiGlobeSurfaceBackend::supportsR32fTextures() const
{
    return supportsR32fTextures(this->rhi);
}

bool MapRhiGlobeSurfaceBackend::isYUpInNdc() const
{
    return isYUpInNdc(this->rhi);
}

bool MapRhiGlobeSurfaceBackend::recreateTileTexture(
    quint64 resource_id, const QSize &size)
{
    return recreateTileTexture(this->rhi, resource_id, size);
}

bool MapRhiGlobeSurfaceBackend::recreateTileHeatmapTexture(
    quint64 resource_id, const QSize &size)
{
    return recreateTileHeatmapTexture(this->rhi, resource_id, size);
}

bool MapRhiGlobeSurfaceBackend::rebuildTileBindings(quint64 resource_id)
{
    return rebuildTileBindings(this->rhi, resource_id);
}

bool MapRhiGlobeSurfaceBackend::ensureHeatmapBakeResources(int texture_size)
{
    return ensureHeatmapBakeResources(this->rhi, texture_size);
}

bool MapRhiGlobeSurfaceBackend::recordVisibleHeatmapBakes(
    QRhiCommandBuffer *command_buffer,
    const QVector<MapRhiGlobeHeatmapBakeJob> &jobs,
    int texture_size,
    int array_layer_count,
    int maximum_atlas_slots,
    MapRhiGlobeHeatmapBakeExecutionStats *stats)
{
    return recordVisibleHeatmapBakes(
        this->rhi, command_buffer, jobs, texture_size,
        array_layer_count, maximum_atlas_slots, stats);
}

bool MapRhiGlobeSurfaceBackend::recordDiagnosticHeatmapBake(
    QRhiCommandBuffer *command_buffer,
    const QVector<MapRhiGlobeHeatmapBakeInstance> &instances,
    int texture_size,
    QString *failure_reason)
{
    return recordDiagnosticHeatmapBake(
        this->rhi, command_buffer, instances, texture_size, failure_reason);
}

bool MapRhiGlobeSurfaceBackend::queueDiagnosticHeatmapReadback(
    QRhiCommandBuffer *command_buffer,
    QRhiReadbackResult *readback_result) const
{
    return queueDiagnosticHeatmapReadback(
        this->rhi, command_buffer, readback_result);
}

bool MapRhiGlobeSurfaceBackend::uploadGeometry(
    QRhiResourceUpdateBatch *resource_updates,
    const MapGlobeSurfaceRenderFrame &frame)
{
    return uploadGeometry(this->rhi, resource_updates, frame);
}

bool MapRhiGlobeSurfaceBackend::uploadImageryArrayDrawIndices(
    QRhiResourceUpdateBatch *resource_updates,
    const QVector<quint32> &indices,
    qsizetype maximum_index_count)
{
    return uploadImageryArrayDrawIndices(
        this->rhi, resource_updates, indices, maximum_index_count);
}

bool MapRhiGlobeSurfaceBackend::uploadHeatmapArrayDrawIndices(
    QRhiResourceUpdateBatch *resource_updates,
    const QVector<quint32> &indices,
    qsizetype maximum_index_count)
{
    return uploadHeatmapArrayDrawIndices(
        this->rhi, resource_updates, indices, maximum_index_count);
}

bool MapRhiGlobeSurfaceBackend::uploadImageryArrayLayers(
    QRhiResourceUpdateBatch *resource_updates,
    const QVector<float> &layers)
{
    return uploadImageryArrayLayers(this->rhi, resource_updates, layers);
}

bool MapRhiGlobeSurfaceBackend::uploadHeatmapArrayLayers(
    QRhiResourceUpdateBatch *resource_updates,
    const QVector<float> &layers)
{
    return uploadHeatmapArrayLayers(this->rhi, resource_updates, layers);
}

bool MapRhiGlobeSurfaceBackend::createImageryArrayPage(
    const QSize &layer_size,
    int layer_count,
    int maximum_page_count)
{
    return createImageryArrayPage(
        this->rhi, layer_size, layer_count, maximum_page_count);
}

bool MapRhiGlobeSurfaceBackend::createHeatmapArrayPage(
    QRhiResourceUpdateBatch *resource_updates,
    const QSize &layer_size,
    int layer_count,
    int maximum_page_count)
{
    return createHeatmapArrayPage(
        this->rhi, resource_updates, layer_size, layer_count,
        maximum_page_count);
}

bool MapRhiGlobeSurfaceBackend::createTerrainHeightArrayPage(
    const QSize &layer_size,
    int layer_count,
    int maximum_page_count)
{
    return createTerrainHeightArrayPage(
        this->rhi, layer_size, layer_count, maximum_page_count);
}

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
        || this->imagery_array_pages.empty()
        || this->imagery_array_page_gpu_resources.empty())
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
        {quint32(sizeof(MapGlobeSurfaceVertex))},
        {quint32(sizeof(float))}
    });
    input_layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3,
         quint32(offsetof(MapGlobeSurfaceVertex, x))},
        {0, 1, QRhiVertexInputAttribute::Float2,
         quint32(offsetof(MapGlobeSurfaceVertex, u))},
        {1, 2, QRhiVertexInputAttribute::Float, 0}
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
        this->imagery_array_page_gpu_resources.front().bindings.get());
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
        || this->imagery_array_page_gpu_resources.empty()
        || this->heatmap_array_page_gpu_resources.empty()
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
                this->imagery_array_page_gpu_resources.front().texture.get(),
                this->sampler_resource.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage,
                this->heatmap_array_page_gpu_resources.front().texture.get(),
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
        {quint32(sizeof(float))},
        {quint32(sizeof(float))}
    });
    input_layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3,
         quint32(offsetof(MapGlobeSurfaceVertex, x))},
        {0, 1, QRhiVertexInputAttribute::Float2,
         quint32(offsetof(MapGlobeSurfaceVertex, u))},
        {1, 2, QRhiVertexInputAttribute::Float, 0},
        {2, 3, QRhiVertexInputAttribute::Float, 0}
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

bool MapRhiGlobeSurfaceBackend::ensureHeatmapArrayBatchBinding(
    QRhi *rhi, int imagery_page_index, int heatmap_page_index)
{
    if (rhi == nullptr || imagery_page_index < 0 || heatmap_page_index < 0
        || imagery_page_index
            >= int(this->imagery_array_page_gpu_resources.size())
        || heatmap_page_index
            >= int(this->heatmap_array_page_gpu_resources.size())
        || this->camera_uniform_buffer == nullptr
        || this->sampler_resource == nullptr)
    {
        return false;
    }

    const quint64 key = heatmapArrayBatchBindingKey(
        imagery_page_index, heatmap_page_index);
    if (this->heatmap_array_batch_bindings.find(key)
        != this->heatmap_array_batch_bindings.end())
    {
        return true;
    }

    QRhiTexture *imagery_texture =
        this->imagery_array_page_gpu_resources[size_t(imagery_page_index)]
            .texture.get();
    QRhiTexture *heatmap_texture =
        this->heatmap_array_page_gpu_resources[size_t(heatmap_page_index)]
            .texture.get();
    if (imagery_texture == nullptr || heatmap_texture == nullptr)
        return false;

    std::unique_ptr<QRhiShaderResourceBindings> bindings(
        rhi->newShaderResourceBindings());
    if (!bindings)
        return false;
    bindings->setBindings({
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
    if (!bindings->create())
        return false;

    this->heatmap_array_batch_bindings.emplace(key, std::move(bindings));
    return true;
}

void MapRhiGlobeSurfaceBackend::clearHeatmapArrayBatchBindings()
{
    this->heatmap_array_batch_bindings.clear();
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
    this->heatmap_array_batch_bindings.clear();
    this->terrain_height_array_page_gpu_resources.clear();
    this->terrain_height_array_pages.clear();
    this->heatmap_array_page_gpu_resources.clear();
    this->heatmap_array_pages.clear();
    this->imagery_array_page_gpu_resources.clear();
    this->imagery_array_pages.clear();

    this->heatmap_array_layer_buffer.reset();
    this->imagery_array_layer_buffer.reset();
    this->heatmap_array_draw_index_buffer.reset();
    this->tile_array_draw_index_buffer.reset();
    this->wireframe_vertex_buffer.reset();
    this->cap_index_buffer.reset();
    this->cap_vertex_buffer.reset();
    this->window_index_buffer.reset();
    this->window_vertex_buffer.reset();

    this->heatmap_array_layer_buffer_size = 0;
    this->imagery_array_layer_buffer_size = 0;
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

    this->rhi = nullptr;
    this->render_pass_descriptor = nullptr;
    this->sample_count = 1;
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

bool MapRhiGlobeSurfaceBackend::uploadTileTextureImage(
    QRhiResourceUpdateBatch *resource_updates,
    quint64 resource_id,
    const QImage &image) const
{
    const TileGpuResource *resource = tileGpuResource(resource_id);
    if (resource_updates == nullptr || resource == nullptr
        || resource->texture == nullptr || image.isNull())
    {
        return false;
    }
    resource_updates->uploadTexture(resource->texture.get(), image);
    return true;
}

bool MapRhiGlobeSurfaceBackend::uploadTileHeatmapTextureImage(
    QRhiResourceUpdateBatch *resource_updates,
    quint64 resource_id,
    const QImage &image) const
{
    const TileGpuResource *resource = tileGpuResource(resource_id);
    if (resource_updates == nullptr || resource == nullptr
        || resource->heatmap_texture == nullptr || image.isNull())
    {
        return false;
    }
    resource_updates->uploadTexture(resource->heatmap_texture.get(), image);
    return true;
}

bool MapRhiGlobeSurfaceBackend::imageryArrayPageReady(int page_index) const
{
    return page_index >= 0
        && page_index < int(this->imagery_array_page_gpu_resources.size())
        && this->imagery_array_page_gpu_resources[size_t(page_index)].texture
            != nullptr
        && this->imagery_array_page_gpu_resources[size_t(page_index)].bindings
            != nullptr;
}

bool MapRhiGlobeSurfaceBackend::heatmapArrayPageReady(int page_index) const
{
    return page_index >= 0
        && page_index < int(this->heatmap_array_page_gpu_resources.size())
        && this->heatmap_array_page_gpu_resources[size_t(page_index)].texture
            != nullptr;
}

bool MapRhiGlobeSurfaceBackend::terrainHeightArrayPageReady(
    int page_index) const
{
    return page_index >= 0
        && page_index < int(this->terrain_height_array_page_gpu_resources.size())
        && this->terrain_height_array_page_gpu_resources[size_t(page_index)]
            .texture != nullptr;
}

bool MapRhiGlobeSurfaceBackend::uploadImageryArrayPageLayer(
    QRhiResourceUpdateBatch *resource_updates,
    int page_index, int layer, const QImage &image) const
{
    if (!imageryArrayPageReady(page_index) || resource_updates == nullptr
        || layer < 0 || image.isNull())
    {
        return false;
    }
    const QRhiTextureSubresourceUploadDescription subresource(image);
    const QRhiTextureUploadEntry entry(layer, 0, subresource);
    resource_updates->uploadTexture(
        this->imagery_array_page_gpu_resources[size_t(page_index)].texture.get(),
        QRhiTextureUploadDescription(entry));
    return true;
}

bool MapRhiGlobeSurfaceBackend::uploadHeatmapArrayPageLayer(
    QRhiResourceUpdateBatch *resource_updates,
    int page_index, int layer, const QImage &image) const
{
    if (!heatmapArrayPageReady(page_index) || resource_updates == nullptr
        || layer < 0 || image.isNull())
    {
        return false;
    }
    const QRhiTextureSubresourceUploadDescription subresource(image);
    const QRhiTextureUploadEntry entry(layer, 0, subresource);
    resource_updates->uploadTexture(
        this->heatmap_array_page_gpu_resources[size_t(page_index)].texture.get(),
        QRhiTextureUploadDescription(entry));
    return true;
}

bool MapRhiGlobeSurfaceBackend::uploadTerrainHeightArrayPageLayerRaw(
    QRhiResourceUpdateBatch *resource_updates,
    int page_index, int layer, const QByteArray &data) const
{
    if (!terrainHeightArrayPageReady(page_index)
        || resource_updates == nullptr || layer < 0 || data.isEmpty())
    {
        return false;
    }
    const QRhiTextureSubresourceUploadDescription subresource(data);
    const QRhiTextureUploadEntry entry(layer, 0, subresource);
    resource_updates->uploadTexture(
        this->terrain_height_array_page_gpu_resources[size_t(page_index)]
            .texture.get(),
        QRhiTextureUploadDescription(entry));
    return true;
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

QRhiShaderResourceBindings *MapRhiGlobeSurfaceBackend::tileBindings(
    quint64 resource_id) const
{
    const TileGpuResource *resource = tileGpuResource(resource_id);
    return resource != nullptr ? resource->bindings.get() : nullptr;
}

quint64 MapRhiGlobeSurfaceBackend::heatmapArrayBatchBindingKey(
    int imagery_page_index, int heatmap_page_index)
{
    return (quint64(quint32(imagery_page_index)) << 32)
        | quint64(quint32(heatmap_page_index));
}

QRhiShaderResourceBindings *
MapRhiGlobeSurfaceBackend::heatmapArrayBatchBinding(
    int imagery_page_index, int heatmap_page_index) const
{
    const quint64 key = heatmapArrayBatchBindingKey(
        imagery_page_index, heatmap_page_index);
    std::map<quint64, std::unique_ptr<QRhiShaderResourceBindings>>::const_iterator
        iterator = this->heatmap_array_batch_bindings.find(key);
    return iterator != this->heatmap_array_batch_bindings.end()
        ? iterator->second.get() : nullptr;
}

QRhiTexture *MapRhiGlobeSurfaceBackend::heatmapBakeDestinationTexture(
    const MapRhiGlobeHeatmapBakeJob &job) const
{
    if (job.destination == MapRhiGlobeHeatmapBakeDestination::TileHeatmapTexture)
    {
        const TileGpuResource *resource = tileGpuResource(
            job.tile_gpu_resource_id);
        return resource != nullptr ? resource->heatmap_texture.get() : nullptr;
    }
    if (job.destination == MapRhiGlobeHeatmapBakeDestination::HeatmapArrayLayer
        && heatmapArrayPageReady(job.heatmap_array_page))
    {
        return this->heatmap_array_page_gpu_resources[
            size_t(job.heatmap_array_page)].texture.get();
    }
    return nullptr;
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
        if (heatmapBakeDestinationTexture(job) == nullptr
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
            QRhiTexture *destination_texture =
                heatmapBakeDestinationTexture(job);
            if (destination_texture == nullptr)
            {
                copy_updates->release();
                for (QRhiResourceUpdateBatch *allocated_updates
                     : page_copy_updates)
                {
                    allocated_updates->release();
                }
                bake_updates->release();
                if (upload_bake_vertices)
                {
                    this->heatmap_bake_resources
                        .diagnostic_vertex_upload_pending = true;
                }
                return false;
            }
            copy_updates->copyTexture(
                destination_texture, page.atlas->texture.get(),
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

bool MapRhiGlobeSurfaceBackend::uploadImageryArrayLayers(
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
            rhi, &this->imagery_array_layer_buffer,
            &this->imagery_array_layer_buffer_size,
            QRhiBuffer::VertexBuffer, required_bytes))
    {
        return false;
    }

    resource_updates->updateDynamicBuffer(
        this->imagery_array_layer_buffer.get(), 0, required_bytes,
        layers.constData());
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
        || int(this->imagery_array_pages.size()) >= maximum_page_count
        || this->imagery_array_pages.size()
            != this->imagery_array_page_gpu_resources.size())
    {
        return false;
    }

    ImageryArrayPageGpuResource gpu_page;
    gpu_page.texture.reset(rhi->newTextureArray(
        QRhiTexture::RGBA8, layer_count, layer_size));
    if (!gpu_page.texture || !gpu_page.texture->create())
        return false;

    gpu_page.bindings.reset(rhi->newShaderResourceBindings());
    if (!gpu_page.bindings)
        return false;
    gpu_page.bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage
                | QRhiShaderResourceBinding::FragmentStage,
            this->camera_uniform_buffer.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage,
            gpu_page.texture.get(), this->sampler_resource.get())
    });
    if (!gpu_page.bindings->create())
        return false;

    MapRhiGlobeImageryArrayPage page;
    page.free_layers.reserve(layer_count - 1);
    for (int layer = layer_count - 1; layer >= 1; --layer)
        page.free_layers.append(layer);

    this->imagery_array_pages.push_back(std::move(page));
    this->imagery_array_page_gpu_resources.push_back(std::move(gpu_page));
    clearHeatmapArrayBatchBindings();
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
        || int(this->heatmap_array_pages.size()) >= maximum_page_count
        || this->heatmap_array_pages.size()
            != this->heatmap_array_page_gpu_resources.size())
    {
        return false;
    }

    HeatmapArrayPageGpuResource gpu_page;
    gpu_page.texture.reset(rhi->newTextureArray(
        QRhiTexture::RGBA8, layer_count, layer_size));
    if (!gpu_page.texture || !gpu_page.texture->create())
        return false;

    QImage transparent_layer(
        layer_size, QImage::Format_RGBA8888_Premultiplied);
    transparent_layer.fill(Qt::transparent);
    const QRhiTextureSubresourceUploadDescription transparent_subresource(
        transparent_layer);
    const QRhiTextureUploadEntry transparent_entry(
        0, 0, transparent_subresource);
    resource_updates->uploadTexture(
        gpu_page.texture.get(), QRhiTextureUploadDescription(transparent_entry));

    MapRhiGlobeHeatmapArrayPage page;
    page.free_layers.reserve(layer_count - 1);
    for (int layer = layer_count - 1; layer >= 1; --layer)
        page.free_layers.append(layer);

    this->heatmap_array_pages.push_back(std::move(page));
    this->heatmap_array_page_gpu_resources.push_back(std::move(gpu_page));
    clearHeatmapArrayBatchBindings();
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
        || this->terrain_height_array_pages.size()
            != this->terrain_height_array_page_gpu_resources.size()
        || !rhi->isFeatureSupported(QRhi::TextureArrays)
        || !rhi->isTextureFormatSupported(QRhiTexture::R32F))
    {
        return false;
    }

    TerrainHeightArrayPageGpuResource gpu_page;
    gpu_page.texture.reset(rhi->newTextureArray(
        QRhiTexture::R32F, layer_count, layer_size));
    if (!gpu_page.texture || !gpu_page.texture->create())
        return false;

    MapRhiGlobeTerrainHeightArrayPage page;
    page.free_layers.reserve(layer_count);
    for (int layer = layer_count - 1; layer >= 0; --layer)
        page.free_layers.append(layer);

    this->terrain_height_array_pages.push_back(std::move(page));
    this->terrain_height_array_page_gpu_resources.push_back(std::move(gpu_page));
    return true;
}

void MapRhiGlobeSurfaceBackend::popImageryArrayPage()
{
    if (!this->imagery_array_pages.empty())
        this->imagery_array_pages.pop_back();
    if (!this->imagery_array_page_gpu_resources.empty())
        this->imagery_array_page_gpu_resources.pop_back();
    clearHeatmapArrayBatchBindings();
}

void MapRhiGlobeSurfaceBackend::popHeatmapArrayPage()
{
    if (!this->heatmap_array_pages.empty())
        this->heatmap_array_pages.pop_back();
    if (!this->heatmap_array_page_gpu_resources.empty())
        this->heatmap_array_page_gpu_resources.pop_back();
    clearHeatmapArrayBatchBindings();
}

void MapRhiGlobeSurfaceBackend::clearTerrainHeightArrayPages()
{
    this->terrain_height_array_pages.clear();
    this->terrain_height_array_page_gpu_resources.clear();
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

bool MapRhiGlobeSurfaceBackend::patchImageryArrayLayers(
    QRhiResourceUpdateBatch *resource_updates,
    int first_vertex,
    qsizetype vertex_count,
    const QVector<float> &layers)
{
    if (resource_updates == nullptr || first_vertex < 0 || vertex_count <= 0
        || !this->imagery_array_layer_buffer
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
        || byte_end_qsize > qsizetype(this->imagery_array_layer_buffer_size)
        || byte_offset_qsize > qsizetype(std::numeric_limits<int>::max())
        || byte_count_qsize > qsizetype(std::numeric_limits<int>::max()))
    {
        return false;
    }

    resource_updates->updateDynamicBuffer(
        this->imagery_array_layer_buffer.get(), int(byte_offset_qsize),
        int(byte_count_qsize), layers.constData() + first_vertex);
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
    if (frame.map_visible && this->tile_pipeline != nullptr)
    {
        const bool use_array = draw_resources.imagery_array_active
            && this->window_vertex_buffer
            && this->window_index_buffer
            && this->imagery_array_layer_buffer != nullptr
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
                    this->heatmap_array_pipeline.get());
                const QRhiCommandBuffer::VertexInput bindings[] = {
                    {this->window_vertex_buffer.get(), 0},
                    {this->imagery_array_layer_buffer.get(), 0},
                    {this->heatmap_array_layer_buffer.get(), 0}
                };
                command_buffer->setVertexInput(
                    0, 3, bindings,
                    this->heatmap_array_draw_index_buffer.get(), 0,
                    QRhiCommandBuffer::IndexUInt32);
                for (const MapRhiGlobeSurfaceBatch &batch :
                     draw_resources.heatmap_array_batches)
                {
                    QRhiShaderResourceBindings *batch_bindings =
                        heatmapArrayBatchBinding(
                            batch.imagery_page_index,
                            batch.heatmap_page_index);
                    if (batch_bindings == nullptr
                        || batch.draw_index_count <= 0)
                    {
                        continue;
                    }
                    command_buffer->setShaderResources(batch_bindings);
                    command_buffer->drawIndexed(
                        quint32(batch.draw_index_count), 1,
                        quint32(batch.first_draw_index));
                }
            }
            else if (use_array)
            {
                command_buffer->setGraphicsPipeline(
                    this->imagery_array_pipeline.get());
                const QRhiCommandBuffer::VertexInput bindings[] = {
                    {this->window_vertex_buffer.get(), 0},
                    {this->imagery_array_layer_buffer.get(), 0}
                };
                command_buffer->setVertexInput(
                    0, 2, bindings,
                    this->tile_array_draw_index_buffer.get(), 0,
                    QRhiCommandBuffer::IndexUInt32);
                for (const MapRhiGlobeSurfaceBatch &batch :
                     draw_resources.imagery_array_batches)
                {
                    if (batch.imagery_page_index < 0
                        || batch.imagery_page_index
                            >= int(this->imagery_array_page_gpu_resources.size())
                        || batch.draw_index_count <= 0)
                    {
                        continue;
                    }
                    QRhiShaderResourceBindings *batch_bindings =
                        this->imagery_array_page_gpu_resources[
                            size_t(batch.imagery_page_index)].bindings.get();
                    if (batch_bindings == nullptr)
                        continue;
                    command_buffer->setShaderResources(batch_bindings);
                    command_buffer->drawIndexed(
                        quint32(batch.draw_index_count), 1,
                        quint32(batch.first_draw_index));
                }
            }

            command_buffer->setGraphicsPipeline(this->tile_pipeline.get());
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

                QRhiShaderResourceBindings *bindings =
                    tileBindings(tile_state.gpu_resource_id);
                if (bindings == nullptr)
                    bindings = this->template_bindings.get();
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
            command_buffer->setGraphicsPipeline(this->tile_pipeline.get());
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
                QRhiShaderResourceBindings *tile_bindings =
                    tileBindings(tile_state.gpu_resource_id);
                if (tile_bindings == nullptr
                    || surface_tile.vertex_count <= 0
                    || surface_tile.index_count <= 0)
                {
                    continue;
                }

                command_buffer->setShaderResources(tile_bindings);
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
        && this->wireframe_pipeline != nullptr
        && this->wireframe_bindings != nullptr
        && this->wireframe_vertex_buffer
        && !resources.wireframe_vertices->isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->wireframe_pipeline.get());
        command_buffer->setShaderResources(this->wireframe_bindings.get());
        const QRhiCommandBuffer::VertexInput binding(
            this->wireframe_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(resources.wireframe_vertices->size()));
    }
}
