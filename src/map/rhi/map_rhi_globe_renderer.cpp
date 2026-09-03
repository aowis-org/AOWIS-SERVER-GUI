#include "map/rhi/map_rhi_globe_renderer.h"

#include "map/core/map_model.h"
#include "map/data/map_tile_repository.h"
#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <QFile>
#include <QImage>
#include <QPixmap>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cstddef>

namespace
{
// Fixed whole-planet imagery zoom level: 2^GlobeImageryZoom tiles per axis.
// Deliberately low/conservative for a first version -- it keeps the globe's
// initial tile request burst small (16 tiles at zoom 2) while still giving
// a recognizable, reasonably sharp view of the whole Earth from space. Easy
// to raise once this is proven out.
constexpr int GlobeImageryZoom = 2;
// Vertex grid subdivisions per tile edge. Tiles at zoom 2 span 90 degrees of
// longitude each, so a fair amount of subdivision is needed for the curved
// ellipsoid surface (and Web Mercator's own per-tile latitude compression
// near the poles) to look smooth rather than faceted.
constexpr int GlobeTileGridSubdivisions = 10;
// Longitude segments used for each polar cap fan. Independent of the tile
// grid above -- a small seam between the imagery tiles' edge at +-85.05
// degrees and the cap fan's ring is not visually significant at whole-globe
// viewing distance.
constexpr int GlobePolarCapSegments = 48;
// Flat fallback color for the polar caps (area above/below Web Mercator's
// +-85.05 degree limit, which basemap tiles never cover). A light,
// ice/cloud-like color reads reasonably for both poles without pretending
// to be real imagery.
const QColor GlobePolarCapColor(235, 240, 245);

constexpr int GlobeCameraUniformBytes = 16 * int(sizeof(float));

QShader loadGlobeShader(const QString &resource_path)
{
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly))
        return QShader();
    return QShader::fromSerialized(file.readAll());
}
}

MapRhiGlobeRenderer::MapRhiGlobeRenderer(MapModel *map_model, MapTileRepository *tile_repository)
    : map_model(map_model), tile_repository(tile_repository)
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
    cap.first_vertex = this->tile_vertices.size();

    const TileVertex pole_vertex = makeTileVertex(0.0, pole_lat, 0.5f, 0.5f);
    for (int segment = 0; segment < GlobePolarCapSegments; ++segment)
    {
        const double lon0 = -180.0 + 360.0 * double(segment) / double(GlobePolarCapSegments);
        const double lon1 = -180.0 + 360.0 * double(segment + 1) / double(GlobePolarCapSegments);
        const TileVertex ring0 = makeTileVertex(lon0, ring_lat, 0.5f, 0.5f);
        const TileVertex ring1 = makeTileVertex(lon1, ring_lat, 0.5f, 0.5f);

        this->tile_vertices.append(pole_vertex);
        if (north)
        {
            this->tile_vertices.append(ring0);
            this->tile_vertices.append(ring1);
        }
        else
        {
            this->tile_vertices.append(ring1);
            this->tile_vertices.append(ring0);
        }
    }

    cap.vertex_count = this->tile_vertices.size() - cap.first_vertex;
    this->tiles.append(cap);
}

void MapRhiGlobeRenderer::buildMesh()
{
    if (this->mesh_built)
        return;

    this->tile_vertices.clear();
    this->tiles.clear();

    const int tile_span = 1 << GlobeImageryZoom;
    for (int tile_y = 0; tile_y < tile_span; ++tile_y)
    {
        for (int tile_x = 0; tile_x < tile_span; ++tile_x)
        {
            GlobeTile tile;
            tile.tile_x = tile_x;
            tile.tile_y = tile_y;
            tile.zoom = GlobeImageryZoom;
            tile.first_vertex = this->tile_vertices.size();

            for (int row = 0; row < GlobeTileGridSubdivisions; ++row)
            {
                const double v0 = double(row) / double(GlobeTileGridSubdivisions);
                const double v1 = double(row + 1) / double(GlobeTileGridSubdivisions);
                const double lat0 = GeoWebMercator::tileYToLat(tile_y + v0, GlobeImageryZoom);
                const double lat1 = GeoWebMercator::tileYToLat(tile_y + v1, GlobeImageryZoom);

                for (int col = 0; col < GlobeTileGridSubdivisions; ++col)
                {
                    const double u0 = double(col) / double(GlobeTileGridSubdivisions);
                    const double u1 = double(col + 1) / double(GlobeTileGridSubdivisions);
                    const double lon0 = GeoWebMercator::tileXToLon(tile_x + u0, GlobeImageryZoom);
                    const double lon1 = GeoWebMercator::tileXToLon(tile_x + u1, GlobeImageryZoom);

                    const TileVertex p00 = makeTileVertex(lon0, lat0, float(u0), float(v0));
                    const TileVertex p10 = makeTileVertex(lon1, lat0, float(u1), float(v0));
                    const TileVertex p01 = makeTileVertex(lon0, lat1, float(u0), float(v1));
                    const TileVertex p11 = makeTileVertex(lon1, lat1, float(u1), float(v1));

                    this->tile_vertices.append(p00);
                    this->tile_vertices.append(p01);
                    this->tile_vertices.append(p10);
                    this->tile_vertices.append(p10);
                    this->tile_vertices.append(p01);
                    this->tile_vertices.append(p11);
                }
            }

            tile.vertex_count = this->tile_vertices.size() - tile.first_vertex;
            this->tiles.append(tile);
        }
    }

    buildPolarCap(true);
    buildPolarCap(false);

    this->mesh_built = true;
    this->tile_vertex_upload_pending = true;
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
    if (this->tile_repository != nullptr && !this->tiles_requested)
    {
        const quint64 batch = this->tile_repository->beginTileRequestBatch(
            this, QStringLiteral("globe"));
        for (GlobeTile &tile : this->tiles)
        {
            if (tile.is_cap)
                continue;

            tile.imagery_key = this->map_model->tileCacheKeyAtZoom(
                tile.tile_x, tile.tile_y, tile.zoom);
            if (this->tile_repository->tile(tile.imagery_key) != nullptr)
                continue;

            this->tile_repository->requestTile(
                this->map_model->tileEndpointAtZoom(tile.tile_x, tile.tile_y, tile.zoom),
                tile.imagery_key, tile.tile_x, tile.tile_y, 0, batch, true);
        }
        this->tiles_requested = true;
    }

    for (GlobeTile &tile : this->tiles)
        ensureTileResource(tile, resource_updates);
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

    buildMesh();
    return ensureSharedResources();
}

bool MapRhiGlobeRenderer::prepare(
    QRhiResourceUpdateBatch *resource_updates, const QMatrix4x4 &view_projection,
    const QSize &viewport_size)
{
    Q_UNUSED(viewport_size);

    if (this->rhi == nullptr || resource_updates == nullptr)
        return false;
    if (!ensureSharedResources())
        return false;

    if (this->tile_vertex_upload_pending && !this->tile_vertices.isEmpty())
    {
        const int required_bytes =
            int(this->tile_vertices.size() * qsizetype(sizeof(TileVertex)));
        if (!this->tile_vertex_buffer || this->tile_vertex_buffer_size != required_bytes)
        {
            this->tile_vertex_buffer.reset(this->rhi->newBuffer(
                QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_bytes));
            if (!this->tile_vertex_buffer || !this->tile_vertex_buffer->create())
                return false;
            this->tile_vertex_buffer_size = required_bytes;
        }
        resource_updates->updateDynamicBuffer(
            this->tile_vertex_buffer.get(), 0, required_bytes,
            this->tile_vertices.constData());
        this->tile_vertex_upload_pending = false;
    }

    requestMissingTiles(resource_updates);

    float matrix_data[16];
    std::copy(view_projection.constData(), view_projection.constData() + 16, matrix_data);
    resource_updates->updateDynamicBuffer(
        this->camera_uniform_buffer.get(), 0, GlobeCameraUniformBytes, matrix_data);

    return true;
}

void MapRhiGlobeRenderer::draw(QRhiCommandBuffer *command_buffer)
{
    if (command_buffer == nullptr || !this->tile_vertex_buffer || !this->pipeline)
        return;

    command_buffer->setGraphicsPipeline(this->pipeline.get());
    for (const GlobeTile &tile : this->tiles)
    {
        if (tile.resource == nullptr || !tile.resource->bindings || tile.vertex_count <= 0)
            continue;

        command_buffer->setShaderResources(tile.resource->bindings.get());
        const quint32 byte_offset = quint32(tile.first_vertex * int(sizeof(TileVertex)));
        const QRhiCommandBuffer::VertexInput binding(this->tile_vertex_buffer.get(), byte_offset);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(tile.vertex_count));
    }
}

void MapRhiGlobeRenderer::invalidateImagery()
{
    this->tile_resources.clear();
    this->cap_resource = TileResource();
    for (GlobeTile &tile : this->tiles)
    {
        tile.imagery_key.clear();
        tile.resource = nullptr;
    }
    this->tiles_requested = false;
}

void MapRhiGlobeRenderer::releaseResources()
{
    this->pipeline.reset();
    this->template_bindings.reset();
    this->dummy_texture.reset();
    this->sampler.reset();
    this->camera_uniform_buffer.reset();
    this->tile_vertex_buffer.reset();
    this->tile_vertex_buffer_size = 0;
    this->tile_vertex_upload_pending = true;
    invalidateImagery();
}
