#include "map/rhi/map_rhi_globe_renderer.h"

#include "map/core/map_model.h"
#include "map/data/map_tile_repository.h"
#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QSet>
#include <QtMath>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace
{
// Highest imagery zoom the globe will ever request. Matches MapModel::MaxZoom
// (19) exactly, since MapModel::MinViewGlobeDistanceM is itself pinned to
// zoom 19 via viewGlobeDistanceMForZoomLevel() -- the globe's maximum zoom-in
// should reach exactly as much detail as 2D/3D ever do, no more, no less.
constexpr int GlobeImageryMaxZoom = MapModel::MaxZoom;
// Hard cap on the (square) tile window's radius around the center tile, in
// tiles -- bounds the worst-case request/mesh burden regardless of zoom or
// viewing angle (see foregroundTileRadius() below).
constexpr int GlobeForegroundMaxTileRadius = 5;
// Extra ring of tiles kept beyond the strictly-visible foreground, so an
// ordinary pan/orbit does not immediately fall outside the built window and
// force a rebuild on every frame -- the same role
// MapRhiBasemapRenderer's own retention margin plays for 2D/3D.
constexpr int GlobeWindowRetentionMarginTiles = 1;
// Dead zone (in zoom levels) around the current zoom before
// computeDesiredZoom() will actually switch -- without this, distance
// values that hover near an integer boundary would flicker the window
// between two zoom levels every frame.
constexpr double GlobeZoomHysteresis = 0.6;
// Safety multiplier applied to the horizon-angle visible-radius estimate in
// foregroundTileRadius(), so the requested window comfortably covers what's
// on screen even though the estimate itself (see that function) is a rough
// one.
constexpr double GlobeForegroundAngleMarginFactor = 1.3;
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

constexpr int GlobeCameraUniformBytes = 16 * int(sizeof(float));

// Vertex grid subdivisions per tile edge, by zoom level. Low zoom tiles
// span a huge angular area (a zoom-0 tile is the entire planet, a zoom-1
// tile is a full hemisphere) and need heavy subdivision for the ellipsoid
// curvature to look smooth -- 8 subdivisions across an entire 360-degree
// tile is only 45 degrees per facet, which renders as a visibly faceted
// polyhedron rather than a sphere. By the time tiles are a few degrees
// across or smaller, the curvature within a single tile is negligible and
// a coarse grid is indistinguishable from a fine one while costing far
// less geometry across a whole tile window. In practice MapModel's own
// Min/MaxViewGlobeDistanceM keep the picked zoom at 4 or above, so zoom
// 0-3 should rarely if ever be hit -- these are still handled properly in
// case that ever changes.
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

// Picks the imagery zoom level from camera distance, using the exact same
// distance<->zoom relationship the footer zoom control and MapModel's own
// Min/MaxViewGlobeDistanceM are pinned to (see
// MapModel::viewGlobeZoomLevelForDistanceM()), so the imagery resolution
// shown always matches what that same zoom level would show in 2D/3D.
// Hysteresis-gated against the previously chosen zoom to avoid flicker at
// exact boundaries.
int computeDesiredZoom(
    double continuous_zoom_level, int current_zoom)
{
    if (current_zoom >= 0
        && std::abs(continuous_zoom_level - double(current_zoom)) < GlobeZoomHysteresis)
    {
        return current_zoom;
    }
    return qBound(0, int(std::lround(continuous_zoom_level)), GlobeImageryMaxZoom);
}

// Rough visible-radius estimate, in tiles at the given zoom, around the
// camera's target. Treats camera distance as if it were altitude directly
// above the target (distance is actually eye-to-target, not eye-to-center,
// so this over-estimates the true horizon angle at low pitch) -- a
// deliberate simplification that errs toward requesting a bit more
// coverage than strictly visible rather than leaving gaps, since exact
// per-tile view-frustum culling (as MapRhiBasemapRenderer does for the flat
// view) is not worth the complexity at whole-globe scale.
int foregroundTileRadius(double distance_m, int zoom)
{
    const double eye_radius = GeoWgs84Ellipsoid::EquatorialRadiusM + qMax(0.0, distance_m);
    const double ratio = qBound(0.0, GeoWgs84Ellipsoid::EquatorialRadiusM / eye_radius, 1.0);
    const double half_angle_deg =
        qRadiansToDegrees(std::acos(ratio)) * GlobeForegroundAngleMarginFactor;
    const double tiles_per_degree = double(1 << zoom) / 360.0;
    const int radius = int(std::ceil(half_angle_deg * tiles_per_degree));
    return qBound(1, radius, GlobeForegroundMaxTileRadius);
}

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
    cap.first_vertex = this->cap_vertices.size();

    const TileVertex pole_vertex = makeTileVertex(0.0, pole_lat, 0.5f, 0.5f);
    for (int segment = 0; segment < GlobePolarCapSegments; ++segment)
    {
        const double lon0 = -180.0 + 360.0 * double(segment) / double(GlobePolarCapSegments);
        const double lon1 = -180.0 + 360.0 * double(segment + 1) / double(GlobePolarCapSegments);
        const TileVertex ring0 = makeTileVertex(lon0, ring_lat, 0.5f, 0.5f);
        const TileVertex ring1 = makeTileVertex(lon1, ring_lat, 0.5f, 0.5f);

        this->cap_vertices.append(pole_vertex);
        if (north)
        {
            this->cap_vertices.append(ring0);
            this->cap_vertices.append(ring1);
        }
        else
        {
            this->cap_vertices.append(ring1);
            this->cap_vertices.append(ring0);
        }
    }

    cap.vertex_count = this->cap_vertices.size() - cap.first_vertex;
    this->cap_tiles.append(cap);
}

void MapRhiGlobeRenderer::buildCaps()
{
    if (this->caps_built)
        return;

    this->cap_vertices.clear();
    this->cap_tiles.clear();
    buildPolarCap(true);
    buildPolarCap(false);
    this->caps_built = true;
    this->cap_vertex_upload_pending = true;
}

void MapRhiGlobeRenderer::rebuildWindow(
    int zoom, int x_min, int x_max, int y_min, int y_max, int tile_span)
{
    const int clamped_y_min = qBound(0, y_min, tile_span - 1);
    const int clamped_y_max = qBound(0, y_max, tile_span - 1);
    const int subdivisions = subdivisionsForZoom(zoom);

    this->window_vertices.clear();
    this->window_tiles.clear();

    // At low zoom (small tile_span) the requested window, expanded by
    // radius + retention margin, can wrap around the whole planet more
    // than once in X; dedupe by (wrapped_x, y) so that just produces a few
    // redundant loop iterations rather than duplicate geometry.
    QSet<quint64> seen_positions;
    for (int x = x_min; x <= x_max; ++x)
    {
        const int wrapped_x = GeoWebMercator::wrapTileX(x, zoom);
        for (int y = clamped_y_min; y <= clamped_y_max; ++y)
        {
            const quint64 position_key =
                (quint64(quint32(wrapped_x)) << 32) | quint64(quint32(y));
            if (seen_positions.contains(position_key))
                continue;
            seen_positions.insert(position_key);

            GlobeTile tile;
            tile.tile_x = wrapped_x;
            tile.tile_y = y;
            tile.zoom = zoom;
            tile.imagery_key = this->map_model->tileCacheKeyAtZoom(wrapped_x, y, zoom);
            tile.first_vertex = this->window_vertices.size();

            for (int row = 0; row < subdivisions; ++row)
            {
                const double v0 = double(row) / double(subdivisions);
                const double v1 = double(row + 1) / double(subdivisions);
                const double lat0 = GeoWebMercator::tileYToLat(y + v0, zoom);
                const double lat1 = GeoWebMercator::tileYToLat(y + v1, zoom);

                for (int col = 0; col < subdivisions; ++col)
                {
                    const double u0 = double(col) / double(subdivisions);
                    const double u1 = double(col + 1) / double(subdivisions);
                    const double lon0 = GeoWebMercator::tileXToLon(x + u0, zoom);
                    const double lon1 = GeoWebMercator::tileXToLon(x + u1, zoom);

                    const TileVertex p00 = makeTileVertex(lon0, lat0, float(u0), float(v0));
                    const TileVertex p10 = makeTileVertex(lon1, lat0, float(u1), float(v0));
                    const TileVertex p01 = makeTileVertex(lon0, lat1, float(u0), float(v1));
                    const TileVertex p11 = makeTileVertex(lon1, lat1, float(u1), float(v1));

                    this->window_vertices.append(p00);
                    this->window_vertices.append(p01);
                    this->window_vertices.append(p10);
                    this->window_vertices.append(p10);
                    this->window_vertices.append(p01);
                    this->window_vertices.append(p11);
                }
            }

            tile.vertex_count = this->window_vertices.size() - tile.first_vertex;
            this->window_tiles.append(tile);
        }
    }

    this->window_zoom = zoom;
    this->window_tile_x_min = x_min;
    this->window_tile_x_max = x_max;
    this->window_tile_y_min = clamped_y_min;
    this->window_tile_y_max = clamped_y_max;
    this->window_dirty = false;
    this->window_tiles_requested = false;
    this->window_vertex_upload_pending = true;

    pruneUnusedTileResources();
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

    for (auto it = this->tile_resources.begin(); it != this->tile_resources.end();)
    {
        if (keys_in_use.contains(it->first))
            ++it;
        else
            it = this->tile_resources.erase(it);
    }
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
    if (this->tile_repository != nullptr && !this->window_tiles_requested)
    {
        const quint64 batch = this->tile_repository->beginTileRequestBatch(
            this, QStringLiteral("globe"));
        for (const GlobeTile &tile : this->window_tiles)
        {
            if (this->tile_repository->tile(tile.imagery_key) != nullptr)
                continue;

            this->tile_repository->requestTile(
                this->map_model->tileEndpointAtZoom(tile.tile_x, tile.tile_y, tile.zoom),
                tile.imagery_key, tile.tile_x, tile.tile_y, 0, batch, true);
        }
        this->window_tiles_requested = true;
    }

    for (GlobeTile &tile : this->window_tiles)
        ensureTileResource(tile, resource_updates);
    for (GlobeTile &tile : this->cap_tiles)
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

    buildCaps();
    return ensureSharedResources();
}

bool MapRhiGlobeRenderer::prepare(
    QRhiResourceUpdateBatch *resource_updates, const QMatrix4x4 &view_projection,
    const QSize &viewport_size)
{
    if (this->rhi == nullptr || resource_updates == nullptr || this->map_model == nullptr)
        return false;
    if (!ensureSharedResources())
        return false;

    const double distance_m = qMax(1.0, this->map_model->viewGlobeDistanceM());
    const double continuous_zoom_level = MapModel::viewGlobeZoomLevelForDistanceM(
        distance_m, this->map_model->centerLat(),
        viewport_size.isValid()
            ? viewport_size.height() : MapModel::GlobeZoomReferenceViewportHeightPx);
    const int desired_zoom = computeDesiredZoom(continuous_zoom_level, this->window_zoom);
    const int tile_span = 1 << desired_zoom;
    const int foreground_radius = foregroundTileRadius(distance_m, desired_zoom);
    const int center_tile_x = int(std::floor(
        GeoWebMercator::lonToTileX(this->map_model->centerLon(), desired_zoom)));
    const int center_tile_y = qBound(0, int(std::floor(
        GeoWebMercator::latToTileY(this->map_model->centerLat(), desired_zoom))), tile_span - 1);

    const int foreground_x_min = center_tile_x - foreground_radius;
    const int foreground_x_max = center_tile_x + foreground_radius;
    const int foreground_y_min = qMax(0, center_tile_y - foreground_radius);
    const int foreground_y_max = qMin(tile_span - 1, center_tile_y + foreground_radius);

    // Mirrors MapRhiBasemapRenderer's currentLayoutCoversForeground() short
    // circuit: only rebuild the window (new mesh, new tile requests) when
    // the zoom level changed or the foreground has moved outside the
    // already-built (foreground + retention margin) window, not on every
    // frame a pan/orbit is in progress.
    const bool window_covers_foreground =
        !this->window_dirty
        && this->window_zoom == desired_zoom
        && foreground_x_min >= this->window_tile_x_min
        && foreground_x_max <= this->window_tile_x_max
        && foreground_y_min >= this->window_tile_y_min
        && foreground_y_max <= this->window_tile_y_max;

    if (!window_covers_foreground)
    {
        rebuildWindow(
            desired_zoom,
            foreground_x_min - GlobeWindowRetentionMarginTiles,
            foreground_x_max + GlobeWindowRetentionMarginTiles,
            foreground_y_min - GlobeWindowRetentionMarginTiles,
            foreground_y_max + GlobeWindowRetentionMarginTiles,
            tile_span);
    }

    if (this->window_vertex_upload_pending && !this->window_vertices.isEmpty())
    {
        const int required_bytes =
            int(this->window_vertices.size() * qsizetype(sizeof(TileVertex)));
        if (!this->window_vertex_buffer || this->window_vertex_buffer_size != required_bytes)
        {
            this->window_vertex_buffer.reset(this->rhi->newBuffer(
                QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_bytes));
            if (!this->window_vertex_buffer || !this->window_vertex_buffer->create())
                return false;
            this->window_vertex_buffer_size = required_bytes;
        }
        resource_updates->updateDynamicBuffer(
            this->window_vertex_buffer.get(), 0, required_bytes,
            this->window_vertices.constData());
        this->window_vertex_upload_pending = false;
    }

    if (this->cap_vertex_upload_pending && !this->cap_vertices.isEmpty())
    {
        const int required_bytes =
            int(this->cap_vertices.size() * qsizetype(sizeof(TileVertex)));
        if (!this->cap_vertex_buffer)
        {
            this->cap_vertex_buffer.reset(this->rhi->newBuffer(
                QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_bytes));
            if (!this->cap_vertex_buffer || !this->cap_vertex_buffer->create())
                return false;
        }
        resource_updates->updateDynamicBuffer(
            this->cap_vertex_buffer.get(), 0, required_bytes, this->cap_vertices.constData());
        this->cap_vertex_upload_pending = false;
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
    if (command_buffer == nullptr || !this->pipeline)
        return;

    command_buffer->setGraphicsPipeline(this->pipeline.get());

    if (this->window_vertex_buffer)
    {
        for (const GlobeTile &tile : this->window_tiles)
        {
            if (tile.resource == nullptr || !tile.resource->bindings || tile.vertex_count <= 0)
                continue;

            command_buffer->setShaderResources(tile.resource->bindings.get());
            const quint32 byte_offset = quint32(tile.first_vertex * int(sizeof(TileVertex)));
            const QRhiCommandBuffer::VertexInput binding(
                this->window_vertex_buffer.get(), byte_offset);
            command_buffer->setVertexInput(0, 1, &binding);
            command_buffer->draw(quint32(tile.vertex_count));
        }
    }

    if (this->cap_vertex_buffer)
    {
        for (const GlobeTile &tile : this->cap_tiles)
        {
            if (tile.resource == nullptr || !tile.resource->bindings || tile.vertex_count <= 0)
                continue;

            command_buffer->setShaderResources(tile.resource->bindings.get());
            const quint32 byte_offset = quint32(tile.first_vertex * int(sizeof(TileVertex)));
            const QRhiCommandBuffer::VertexInput binding(
                this->cap_vertex_buffer.get(), byte_offset);
            command_buffer->setVertexInput(0, 1, &binding);
            command_buffer->draw(quint32(tile.vertex_count));
        }
    }
}

void MapRhiGlobeRenderer::invalidateImagery()
{
    this->tile_resources.clear();
    this->cap_resource = TileResource();
    for (GlobeTile &tile : this->window_tiles)
        tile.resource = nullptr;
    for (GlobeTile &tile : this->cap_tiles)
        tile.resource = nullptr;
    this->window_tiles_requested = false;
}

void MapRhiGlobeRenderer::releaseResources()
{
    this->pipeline.reset();
    this->template_bindings.reset();
    this->dummy_texture.reset();
    this->sampler.reset();
    this->camera_uniform_buffer.reset();
    this->window_vertex_buffer.reset();
    this->window_vertex_buffer_size = 0;
    this->window_vertex_upload_pending = true;
    this->window_dirty = true;
    this->window_zoom = -1;
    this->window_tile_x_min = 0;
    this->window_tile_x_max = -1;
    this->window_tile_y_min = 0;
    this->window_tile_y_max = -1;
    this->cap_vertex_buffer.reset();
    this->cap_vertex_upload_pending = true;
    invalidateImagery();
}
