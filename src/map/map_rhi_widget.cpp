#include "map_rhi_widget.h"

#include "map_model.h"
#include "map_render_cache_math.h"
#include "map_tile_repository.h"
#include "../geo_web_mercator.h"
#include "../network_symbology_rendering.h"

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QLineF>
#include <QMatrix4x4>
#include <QPalette>
#include <QResizeEvent>
#include <QRectF>
#include <rhi/qshader.h>

#include <rhi/qrhi.h>

#include <array>
#include <cstddef>
#include <cmath>
#include <limits>

namespace
{
constexpr int CameraUniformBytes = 32 * int(sizeof(float));
constexpr int RendererMsaaSamples = 4;

QRhiWidget::Api platformGraphicsApi()
{
#if defined(Q_OS_MACOS)
    return QRhiWidget::Api::Metal;
#elif defined(Q_OS_WIN)
    return QRhiWidget::Api::Direct3D11;
#else
    return QRhiWidget::Api::OpenGL;
#endif
}

QString graphicsApiDisplayName(QRhiWidget::Api api)
{
    switch (api)
    {
        case QRhiWidget::Api::OpenGL:
            return QStringLiteral("OpenGL");
        case QRhiWidget::Api::Metal:
            return QStringLiteral("Metal");
        case QRhiWidget::Api::Vulkan:
            return QStringLiteral("Vulkan");
        case QRhiWidget::Api::Direct3D11:
            return QStringLiteral("Direct3D 11");
        case QRhiWidget::Api::Direct3D12:
            return QStringLiteral("Direct3D 12");
        case QRhiWidget::Api::Null:
        default:
            return QStringLiteral("Null");
    }
}

QShader loadShader(const QString &resource_path)
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

double pointSegmentDistance(const QPointF &point, const QPointF &start, const QPointF &end)
{
    const QPointF segment = end - start;
    const double length_squared = segment.x() * segment.x() + segment.y() * segment.y();
    if (length_squared <= 1e-12)
        return QLineF(point, start).length();

    const QPointF relative = point - start;
    const double ratio = qBound(
        0.0,
        (relative.x() * segment.x() + relative.y() * segment.y()) / length_squared,
        1.0);
    return QLineF(
        point,
        QPointF(start.x() + segment.x() * ratio, start.y() + segment.y() * ratio)).length();
}

bool finiteScreenPoint(const QPointF &point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}
}

MapRhiWidget::MapRhiWidget(MapModel *map_model, const QString &surface_name, QWidget *parent)
    : QRhiWidget(parent),
      map_model(map_model),
      surface_name(surface_name)
{
    Q_ASSERT(this->map_model != nullptr);

    setApi(platformGraphicsApi());
    setSampleCount(RendererMsaaSamples);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);

    this->fallback_origin_world = GeoWebMercator::lonLatToWorldPixel(
        GeoWebMercator::normalizeLongitude(this->map_model->centerLon()),
        this->map_model->centerLat(), MapRenderCacheMath::ReferenceZoom);
    this->basemap_renderer = std::make_unique<MapRhiBasemapRenderer>(
        this->map_model, this->tile_repository);
    syncViewState();

    connect(this->map_model, &MapModel::centerChangedWGS84, this, [this]
    {
        if (!this->scene.hasGeometry())
        {
            this->fallback_origin_world = GeoWebMercator::lonLatToWorldPixel(
                GeoWebMercator::normalizeLongitude(this->map_model->centerLon()),
                this->map_model->centerLat(), MapRenderCacheMath::ReferenceZoom);
            if (this->basemap_renderer)
                this->basemap_renderer->invalidate();
        }
        syncViewState();
        update();
    });
    connect(this->map_model, &MapModel::zoomChanged, this, [this]
    {
        syncViewState();
        if (this->basemap_renderer)
            this->basemap_renderer->invalidate();
        update();
    });
    connect(this->map_model, &MapModel::viewModeChanged, this, [this](MapViewMode)
    {
        syncViewState();
        update();
    });
    connect(this->map_model, &MapModel::view3dCameraChanged, this, [this]
    {
        syncViewState();
        update();
    });
    connect(this->map_model, &MapModel::providerChanged, this, [this](MapProvider)
    {
        if (this->basemap_renderer)
            this->basemap_renderer->invalidate();
        update();
    });
    connect(this, &QRhiWidget::renderFailed, this, [this]
    {
        reportFailure(QStringLiteral("QRhiWidget reported a rendering failure"));
    });
    connect(this, &QRhiWidget::frameSubmitted, this, [this]
    {
        if (this->ready_reported || this->failure_reported)
            return;

        this->ready_reported = true;
        qInfo().noquote()
            << QStringLiteral("Desktop map RHI surface '%1' submitted its first frame using %2 "
                              "(%3 link vertices, %4 node vertices).")
                   .arg(this->surface_name, graphicsApiName())
                   .arg(this->scene.linkVertices().size())
                   .arg(this->scene.nodeVertices().size());
        emit signalRendererReady();
    });
}

MapRhiWidget::~MapRhiWidget() = default;

QString MapRhiWidget::graphicsApiName() const
{
    return graphicsApiDisplayName(api());
}

MapRhiHit MapRhiWidget::hitTest(const QPointF &screen_position) const
{
    MapRhiHit no_hit;
    if (this->map_model == nullptr || this->map_model->viewMode() != MapViewMode::ThreeD
        || !finiteScreenPoint(screen_position)
        || screen_position.x() < 0.0 || screen_position.y() < 0.0
        || screen_position.x() > width() || screen_position.y() > height())
    {
        return no_hit;
    }

    const NetworkRenderSnapshot &snapshot = this->scene.networkSnapshot();

    // The 3D tank replaces its old SVG billboard, so hit-test the projected bounds
    // of the actual model before falling back to the generic node-center radius.
    quint32 best_tank_render_id = 0;
    double best_tank_distance = std::numeric_limits<double>::max();
    const QVector<MapRhiTankInstance> &tank_instances = this->scene.tankInstances();
    for (const MapRhiTankInstance &instance : tank_instances)
    {
        const float top_z = instance.base_center.z()
            + instance.base_height_world
            + instance.body_height_world
            + instance.roof_height_world;
        const float middle_z = (instance.base_center.z() + top_z) / 2.0f;
        const QVector3D center(
            instance.base_center.x(), instance.base_center.y(), middle_z);

        const QVector<QVector3D> bounds_points = {
            QVector3D(instance.base_center.x() - instance.radius_world,
                      instance.base_center.y(), instance.base_center.z()),
            QVector3D(instance.base_center.x() + instance.radius_world,
                      instance.base_center.y(), instance.base_center.z()),
            QVector3D(instance.base_center.x(),
                      instance.base_center.y() - instance.radius_world, instance.base_center.z()),
            QVector3D(instance.base_center.x(),
                      instance.base_center.y() + instance.radius_world, instance.base_center.z()),
            QVector3D(instance.base_center.x() - instance.radius_world,
                      instance.base_center.y(), top_z),
            QVector3D(instance.base_center.x() + instance.radius_world,
                      instance.base_center.y(), top_z),
            QVector3D(instance.base_center.x(),
                      instance.base_center.y() - instance.radius_world, top_z),
            QVector3D(instance.base_center.x(),
                      instance.base_center.y() + instance.radius_world, top_z),
            QVector3D(instance.base_center.x(), instance.base_center.y(), top_z),
            QVector3D(instance.base_center.x(), instance.base_center.y(), instance.base_center.z())
        };

        QRectF projected_bounds;
        bool have_projected_bounds = false;
        for (const QVector3D &point : bounds_points)
        {
            const QPointF projected = this->camera.projectWorldToScreen(point);
            if (!finiteScreenPoint(projected))
                continue;

            if (!have_projected_bounds)
            {
                projected_bounds = QRectF(projected, QSizeF(0.0, 0.0));
                have_projected_bounds = true;
            }
            else
            {
                projected_bounds = projected_bounds.united(
                    QRectF(projected, QSizeF(0.0, 0.0)));
            }
        }

        if (!have_projected_bounds)
            continue;

        projected_bounds.adjust(-5.0, -5.0, 5.0, 5.0);
        if (!projected_bounds.contains(screen_position))
            continue;

        const QPointF projected_center = this->camera.projectWorldToScreen(center);
        const double distance = finiteScreenPoint(projected_center)
            ? QLineF(screen_position, projected_center).length()
            : 0.0;
        if (distance >= best_tank_distance)
            continue;

        best_tank_distance = distance;
        best_tank_render_id = instance.render_id;
    }

    if (best_tank_render_id != 0)
    {
        for (const NetworkRenderNode &node : snapshot.nodes)
        {
            if (node.entity_type != InfrastructureEntity::Tank
                || node.render_id != best_tank_render_id
                || this->scene.isEntityHidden(node.uuid))
            {
                continue;
            }

            MapRhiHit tank_hit;
            tank_hit.render_id = node.render_id;
            tank_hit.entity_type = node.entity_type;
            tank_hit.uuid = node.uuid;
            return tank_hit;
        }
    }

    const double node_hit_radius = qMax(
        8.0, networkSymbologyMarkerSizeForZoom(
            this->map_model->zoom(), this->scene.nodeSizePercent()) / 2.0 + 4.0);
    double best_node_distance = node_hit_radius;
    MapRhiHit best_node_hit;
    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        if (this->scene.isEntityHidden(node.uuid))
            continue;

        const QVector3D world_position = this->scene.worldPosition(
            node.coordinate_wgs84, node.elevation_m, this->scene.originWorld().x());
        const QPointF projected = this->camera.projectWorldToScreen(world_position);
        if (!finiteScreenPoint(projected))
            continue;

        const double distance = QLineF(screen_position, projected).length();
        if (distance > best_node_distance)
            continue;

        best_node_distance = distance;
        best_node_hit.render_id = node.render_id;
        best_node_hit.entity_type = node.entity_type;
        best_node_hit.uuid = node.uuid;
    }
    if (best_node_hit.isValid())
        return best_node_hit;

    const double link_hit_radius = qMax(7.0, this->scene.linkThicknessPx() / 2.0 + 5.0);
    double best_link_distance = link_hit_radius;
    MapRhiHit best_link_hit;
    for (const NetworkRenderLink &link : snapshot.links)
    {
        if (this->scene.isEntityHidden(link.uuid) || link.vertices_wgs84.size() < 2)
            continue;

        bool have_previous = false;
        QPointF previous_screen;
        double wrap_reference_x = this->scene.originWorld().x();
        for (qsizetype vertex_index = 0; vertex_index < link.vertices_wgs84.size(); ++vertex_index)
        {
            const double elevation_m = vertex_index < link.elevations_m.size()
                ? link.elevations_m.at(vertex_index)
                : 0.0;
            double resolved_x = wrap_reference_x;
            const QVector3D world_position = this->scene.worldPosition(
                link.vertices_wgs84.at(vertex_index), elevation_m,
                wrap_reference_x, &resolved_x);
            wrap_reference_x = resolved_x;
            const QPointF current_screen = this->camera.projectWorldToScreen(world_position);
            if (!finiteScreenPoint(current_screen))
            {
                have_previous = false;
                continue;
            }

            if (have_previous)
            {
                const double distance = pointSegmentDistance(
                    screen_position, previous_screen, current_screen);
                if (distance <= best_link_distance)
                {
                    best_link_distance = distance;
                    best_link_hit.render_id = link.render_id;
                    best_link_hit.entity_type = link.entity_type;
                    best_link_hit.uuid = link.uuid;
                }
            }

            previous_screen = current_screen;
            have_previous = true;
        }
    }

    return best_link_hit;
}

void MapRhiWidget::setNetworkSnapshot(const NetworkRenderSnapshot &snapshot)
{
    if (this->scene.geometryRevision() == snapshot.geometry_revision &&
        this->scene.hasGeometry())
    {
        return;
    }

    this->scene.setNetworkSnapshot(snapshot);
    this->scene.setViewZoom(this->map_model->zoom());
    syncViewState();
    if (this->basemap_renderer)
        this->basemap_renderer->invalidate();
    this->geometry_upload_pending = true;
    this->highlight_upload_pending = true;
    this->flow_direction_upload_pending = true;
    this->icon_upload_pending = true;
    this->heatmap_upload_pending = true;
    this->tank_upload_pending = true;
    this->junction_instance_upload_pending = true;
    update();
}


void MapRhiWidget::setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids)
{
    if (!this->scene.setHiddenEntityUuids(hidden_entity_uuids))
        return;

    syncViewState();
    this->geometry_upload_pending = true;
    this->highlight_upload_pending = true;
    this->flow_direction_upload_pending = true;
    this->icon_upload_pending = true;
    this->heatmap_upload_pending = true;
    this->tank_upload_pending = true;
    this->junction_instance_upload_pending = true;
    update();
}


void MapRhiWidget::setNetworkScreenTranslation(const QPointF &translation_pixels)
{
    if (this->network_screen_translation == translation_pixels)
        return;

    this->network_screen_translation = translation_pixels;
    update();
}


void MapRhiWidget::setSymbology(const MapRhiSymbology &symbology)
{
    const bool base_symbology_changed =
        !this->symbology_initialized
        || this->applied_symbology.node_size_percent != symbology.node_size_percent
        || this->applied_symbology.link_thickness_px != symbology.link_thickness_px
        || this->applied_symbology.node_colors != symbology.node_colors
        || this->applied_symbology.link_colors != symbology.link_colors;
    const bool icon_changed =
        !this->symbology_initialized
        || this->applied_symbology.icon_size_percent != symbology.icon_size_percent
        || this->applied_symbology.show_icons != symbology.show_icons
        || this->applied_symbology.node_colors != symbology.node_colors
        || this->applied_symbology.link_colors != symbology.link_colors;
    const bool heatmap_changed =
        !this->symbology_initialized
        || this->applied_symbology.visual_heatmap != symbology.visual_heatmap
        || this->applied_symbology.heatmap_fractions != symbology.heatmap_fractions;
    const bool flow_direction_changed =
        !this->symbology_initialized
        || this->applied_symbology.show_flow_direction != symbology.show_flow_direction
        || this->applied_symbology.flow_direction_size_px != symbology.flow_direction_size_px
        || this->applied_symbology.flow_directions != symbology.flow_directions
        || this->applied_symbology.link_thickness_px != symbology.link_thickness_px
        || this->applied_symbology.link_colors != symbology.link_colors;

    this->scene.setViewZoom(this->map_model->zoom());
    this->scene.setSymbology(symbology);
    this->applied_symbology = symbology;
    this->symbology_initialized = true;

    if (base_symbology_changed)
    {
        this->geometry_upload_pending = true;
        this->highlight_upload_pending = true;
        this->junction_instance_upload_pending = true;
    }
    if (flow_direction_changed)
        this->flow_direction_upload_pending = true;
    if (icon_changed)
    {
        this->icon_upload_pending = true;
        this->tank_upload_pending = true;
    }
    if (heatmap_changed)
        this->heatmap_upload_pending = true;

    update();
}

void MapRhiWidget::setTileRepository(MapTileRepository *tile_repository)
{
    if (this->tile_repository == tile_repository)
        return;

    if (this->tile_repository != nullptr)
        disconnect(this->tile_repository, nullptr, this, nullptr);

    this->tile_repository = tile_repository;
    if (this->basemap_renderer)
        this->basemap_renderer->setTileRepository(this->tile_repository);

    if (this->tile_repository != nullptr)
    {
        connect(this->tile_repository, &MapTileRepository::signalTileAvailable,
                this, [this](const QString &)
        {
            update();
        });
        connect(this->tile_repository, &MapTileRepository::signalTileRetryReady,
                this, [this](const QString &)
        {
            update();
        });
        connect(this->tile_repository, &MapTileRepository::signalTilesDeleted,
                this, [this]
        {
            if (this->basemap_renderer)
                this->basemap_renderer->invalidate();
            update();
        });
    }

    update();
}

void MapRhiWidget::setBackgroundOpacity(int opacity)
{
    const int bounded_opacity = qBound(0, opacity, 100);
    if (this->background_opacity == bounded_opacity)
        return;

    this->background_opacity = bounded_opacity;
    update();
}

void MapRhiWidget::setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid)
{
    this->scene.setSelectedEntity(entity_type, uuid);
    this->highlight_upload_pending = true;
    update();
}

void MapRhiWidget::setSimulationErrorEntities(
    const QHash<QUuid, InfrastructureEntity> &error_entities,
    const QSet<QUuid> &stale_entity_uuids)
{
    this->scene.setSimulationErrorEntities(error_entities, stale_entity_uuids);
    this->highlight_upload_pending = true;
    update();
}

void MapRhiWidget::initialize(QRhiCommandBuffer *command_buffer)
{
    Q_UNUSED(command_buffer);

    QRhi *current_rhi = rhi();
    if (current_rhi == nullptr)
    {
        reportFailure(QStringLiteral("QRhiWidget did not provide a QRhi instance"));
        return;
    }

    QRhiRenderTarget *target = renderTarget();
    if (target == nullptr || target->renderPassDescriptor() == nullptr)
    {
        reportFailure(QStringLiteral("QRhiWidget did not provide a render target"));
        return;
    }

    const bool rhi_changed = this->active_rhi != current_rhi;
    const bool render_pass_changed =
        this->render_pass_descriptor != target->renderPassDescriptor();
    if (rhi_changed)
    {
        resetGpuResources();
        this->active_rhi = current_rhi;
        this->ready_reported = false;
        this->failure_reported = false;
    }
    else if (render_pass_changed)
    {
        this->link_pipeline.reset();
        this->selected_link_pipeline.reset();
        this->node_pipeline.reset();
        this->icon_pipeline.reset();
        this->heatmap_pipeline.reset();
        this->tank_pipeline.reset();
        this->junction_pipeline.reset();
    }

    this->render_pass_descriptor = target->renderPassDescriptor();

    syncViewState();
    if (this->scene.setViewZoom(this->map_model->zoom()))
    {
        this->flow_direction_upload_pending = true;
        this->icon_upload_pending = true;
        this->tank_upload_pending = true;
        this->junction_instance_upload_pending = true;
    }

    if (!createPersistentResources() || !ensureGeometryBuffers() || !createPipelines())
        return;
    if (this->basemap_renderer
        && !this->basemap_renderer->initialize(
            this->active_rhi, this->render_pass_descriptor,
            this->uniform_buffer.get(), sampleCount()))
    {
        reportFailure(QStringLiteral("Failed to initialize RHI basemap renderer"));
        return;
    }

    qInfo().noquote()
        << QStringLiteral("Desktop map RHI surface '%1' initialized using %2 with %3x MSAA.")
               .arg(this->surface_name, graphicsApiName())
               .arg(sampleCount());
}

void MapRhiWidget::render(QRhiCommandBuffer *command_buffer)
{
    if (this->active_rhi == nullptr || command_buffer == nullptr)
    {
        reportFailure(QStringLiteral("QRhi render called without an initialized QRhi context"));
        return;
    }

    QRhiRenderTarget *target = renderTarget();
    if (target == nullptr)
    {
        reportFailure(QStringLiteral("QRhiWidget did not provide a render target"));
        return;
    }

    syncViewState();
    if (this->scene.setViewZoom(this->map_model->zoom()))
    {
        this->flow_direction_upload_pending = true;
        this->icon_upload_pending = true;
        this->tank_upload_pending = true;
        this->junction_instance_upload_pending = true;
    }

    if (!createPersistentResources() || !ensureGeometryBuffers() || !createPipelines())
        return;
    if (this->basemap_renderer
        && !this->basemap_renderer->initialize(
            this->active_rhi, this->render_pass_descriptor,
            this->uniform_buffer.get(), sampleCount()))
    {
        reportFailure(QStringLiteral("Failed to initialize RHI basemap renderer"));
        return;
    }

    const QMatrix4x4 view_projection = this->camera.viewProjectionMatrix(*this->active_rhi);
    std::array<float, 32> uniform_data{};
    const float *matrix_data = view_projection.constData();
    for (int index = 0; index < 16; ++index)
        uniform_data[size_t(index)] = matrix_data[index];
    uniform_data[16] = float(qMax(1, this->viewport_size.width()));
    uniform_data[17] = float(qMax(1, this->viewport_size.height()));
    uniform_data[18] = float(this->scene.linkThicknessPx()) / 2.0f;
    uniform_data[19] = float(networkSymbologyJunctionDotDiameterForZoom(
        this->map_model->zoom(), this->scene.nodeSizePercent()) / 2.0);
    uniform_data[20] = heatmapRadiusPixels();
    uniform_data[21] = qBound(0.0f, this->applied_symbology.heatmap_opacity / 100.0f, 1.0f);
    uniform_data[22] = qBound(0.0f,
        this->applied_symbology.heatmap_solid_center_percent / 100.0f, 0.9f);
    const double heatmap_scale = GeoWebMercator::zoomScale(
        this->map_model->zoom(), MapRenderCacheMath::ReferenceZoom);
    uniform_data[23] = float(heatmapRadiusPixels()
        / qMax(heatmap_scale, 0.000001));

    const QColor background_color = palette().color(QPalette::Window);
    uniform_data[24] = background_color.redF();
    uniform_data[25] = background_color.greenF();
    uniform_data[26] = background_color.blueF();
    uniform_data[27] = qBound(0.0f, this->background_opacity / 100.0f, 1.0f);
    uniform_data[28] = float(this->network_screen_translation.x());
    uniform_data[29] = -float(this->network_screen_translation.y());

    QRhiResourceUpdateBatch *resource_updates = this->active_rhi->nextResourceUpdateBatch();
    resource_updates->updateDynamicBuffer(
        this->uniform_buffer.get(), 0, CameraUniformBytes, uniform_data.data());

    if (this->icon_atlas_upload_pending)
    {
        resource_updates->uploadTexture(
            this->icon_atlas_texture.get(), mapRhiIconAtlasImage());
        this->icon_atlas_upload_pending = false;
    }

    if (this->tank_texture_upload_pending)
    {
        resource_updates->uploadTexture(
            this->tank_texture.get(), mapRhiTankAlbedoImage());
        resource_updates->generateMips(this->tank_texture.get());
        this->tank_texture_upload_pending = false;
    }

    if (this->geometry_upload_pending)
    {
        const QVector<MapRhiScene::LinkVertex> &link_vertices = this->scene.linkVertices();
        const QVector<MapRhiScene::NodeVertex> &node_vertices = this->scene.nodeVertices();
        if (!link_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->link_vertex_buffer.get(), 0,
                int(link_vertices.size() * qsizetype(sizeof(MapRhiScene::LinkVertex))),
                link_vertices.constData());
        }
        if (!node_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->node_vertex_buffer.get(), 0,
                int(node_vertices.size() * qsizetype(sizeof(MapRhiScene::NodeVertex))),
                node_vertices.constData());
        }
        this->geometry_upload_pending = false;
    }

    if (this->highlight_upload_pending)
    {
        const QVector<MapRhiScene::LinkVertex> &selected_link_vertices =
            this->scene.selectedLinkVertices();
        const QVector<MapRhiScene::NodeVertex> &selected_node_vertices =
            this->scene.selectedNodeVertices();
        const QVector<MapRhiScene::LinkVertex> &diagnostic_link_vertices =
            this->scene.diagnosticLinkVertices();
        const QVector<MapRhiScene::NodeVertex> &diagnostic_node_vertices =
            this->scene.diagnosticNodeVertices();

        if (!selected_link_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->selected_link_vertex_buffer.get(), 0,
                int(selected_link_vertices.size() * qsizetype(sizeof(MapRhiScene::LinkVertex))),
                selected_link_vertices.constData());
        }
        if (!selected_node_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->selected_node_vertex_buffer.get(), 0,
                int(selected_node_vertices.size() * qsizetype(sizeof(MapRhiScene::NodeVertex))),
                selected_node_vertices.constData());
        }
        if (!diagnostic_link_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->diagnostic_link_vertex_buffer.get(), 0,
                int(diagnostic_link_vertices.size() * qsizetype(sizeof(MapRhiScene::LinkVertex))),
                diagnostic_link_vertices.constData());
        }
        if (!diagnostic_node_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->diagnostic_node_vertex_buffer.get(), 0,
                int(diagnostic_node_vertices.size() * qsizetype(sizeof(MapRhiScene::NodeVertex))),
                diagnostic_node_vertices.constData());
        }
        this->highlight_upload_pending = false;
    }

    if (this->flow_direction_upload_pending)
    {
        const QVector<MapRhiScene::LinkVertex> &flow_direction_vertices =
            this->scene.flowDirectionVertices();
        if (!flow_direction_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->flow_direction_vertex_buffer.get(), 0,
                int(flow_direction_vertices.size() * qsizetype(sizeof(MapRhiScene::LinkVertex))),
                flow_direction_vertices.constData());
        }
        this->flow_direction_upload_pending = false;
    }

    if (this->icon_upload_pending)
    {
        const QVector<MapRhiScene::IconVertex> &icon_vertices = this->scene.iconVertices();
        if (!icon_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->icon_vertex_buffer.get(), 0,
                int(icon_vertices.size() * qsizetype(sizeof(MapRhiScene::IconVertex))),
                icon_vertices.constData());
        }
        this->icon_upload_pending = false;
    }

    if (this->heatmap_upload_pending)
    {
        const QVector<MapRhiScene::HeatmapVertex> &heatmap_vertices =
            this->scene.heatmapVertices();
        if (!heatmap_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->heatmap_vertex_buffer.get(), 0,
                int(heatmap_vertices.size() * qsizetype(sizeof(MapRhiScene::HeatmapVertex))),
                heatmap_vertices.constData());
        }
        this->heatmap_upload_pending = false;
    }

    if (this->tank_upload_pending)
    {
        if (!this->tank_model_vertices.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->tank_vertex_buffer.get(), 0,
                int(this->tank_model_vertices.size() * qsizetype(sizeof(MapRhiTankModelVertex))),
                this->tank_model_vertices.constData());
        }
        this->tank_upload_pending = false;
    }

    if (this->junction_mesh_upload_pending)
    {
        const QVector<MapRhiJunctionMeshVertex> &junction_mesh =
            mapRhiJunctionSphereMeshVertices();
        if (!junction_mesh.isEmpty())
            resource_updates->uploadStaticBuffer(
                this->junction_mesh_vertex_buffer.get(), junction_mesh.constData());
        this->junction_mesh_upload_pending = false;
    }

    if (this->junction_instance_upload_pending)
    {
        const QVector<MapRhiJunctionInstance> &junction_instances =
            this->scene.junctionInstances();
        if (!junction_instances.isEmpty())
        {
            resource_updates->updateDynamicBuffer(
                this->junction_instance_buffer.get(), 0,
                int(junction_instances.size() * qsizetype(sizeof(MapRhiJunctionInstance))),
                junction_instances.constData());
        }
        this->junction_instance_upload_pending = false;
    }

    if (this->basemap_renderer
        && !this->basemap_renderer->prepare(
            resource_updates, renderOriginWorld(), this->viewport_size))
    {
        resource_updates->release();
        reportFailure(QStringLiteral("Failed to prepare RHI basemap tiles"));
        return;
    }

    command_buffer->beginPass(target, background_color, {1.0f, 0}, resource_updates);

    const QSize output_size = target->pixelSize();
    command_buffer->setViewport(QRhiViewport(
        0.0f, 0.0f, float(output_size.width()), float(output_size.height())));

    if (this->basemap_renderer)
        this->basemap_renderer->draw(command_buffer);

    const QVector<MapRhiScene::HeatmapVertex> &heatmap_vertices =
        this->scene.heatmapVertices();
    if (!heatmap_vertices.isEmpty() && this->applied_symbology.heatmap_opacity > 0)
    {
        command_buffer->setGraphicsPipeline(this->heatmap_pipeline.get());
        command_buffer->setShaderResources(this->heatmap_shader_resource_bindings.get());
        const QRhiCommandBuffer::VertexInput heatmap_binding(
            this->heatmap_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &heatmap_binding);
        command_buffer->draw(quint32(heatmap_vertices.size()));
    }

    const QVector<MapRhiScene::LinkVertex> &link_vertices = this->scene.linkVertices();
    if (!link_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->link_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput link_binding(this->link_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &link_binding);
        command_buffer->draw(quint32(link_vertices.size()));
    }

    const QVector<MapRhiScene::LinkVertex> &flow_direction_vertices =
        this->scene.flowDirectionVertices();
    if (!flow_direction_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->link_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(
            this->flow_direction_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(flow_direction_vertices.size()));
    }

    const QVector<MapRhiScene::NodeVertex> &node_vertices = this->scene.nodeVertices();
    if (!node_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->node_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput node_binding(this->node_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &node_binding);
        command_buffer->draw(quint32(node_vertices.size()));
    }

    const QVector<MapRhiJunctionInstance> &junction_instances =
        this->scene.junctionInstances();
    const QVector<MapRhiJunctionMeshVertex> &junction_mesh =
        mapRhiJunctionSphereMeshVertices();
    if (!junction_instances.isEmpty() && !junction_mesh.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->junction_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput junction_bindings[] = {
            {this->junction_mesh_vertex_buffer.get(), 0},
            {this->junction_instance_buffer.get(), 0}
        };
        command_buffer->setVertexInput(0, 2, junction_bindings);
        command_buffer->draw(
            quint32(junction_mesh.size()), quint32(junction_instances.size()));
    }

    const QVector<MapRhiScene::IconVertex> &icon_vertices = this->scene.iconVertices();
    if (!icon_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->icon_pipeline.get());
        command_buffer->setShaderResources(this->icon_shader_resource_bindings.get());
        const QRhiCommandBuffer::VertexInput icon_binding(this->icon_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &icon_binding);
        command_buffer->draw(quint32(icon_vertices.size()));
    }

    if (!this->tank_model_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->tank_pipeline.get());
        command_buffer->setShaderResources(this->tank_shader_resource_bindings.get());
        const QRhiCommandBuffer::VertexInput tank_binding(this->tank_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &tank_binding);
        command_buffer->draw(quint32(this->tank_model_vertices.size()));
    }

    const QVector<MapRhiScene::LinkVertex> &selected_link_vertices =
        this->scene.selectedLinkVertices();
    if (!selected_link_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->selected_link_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(this->selected_link_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(selected_link_vertices.size()));
    }

    const QVector<MapRhiScene::NodeVertex> &selected_node_vertices =
        this->scene.selectedNodeVertices();
    if (!selected_node_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->node_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(this->selected_node_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(selected_node_vertices.size()));
    }

    const QVector<MapRhiScene::LinkVertex> &diagnostic_link_vertices =
        this->scene.diagnosticLinkVertices();
    if (!diagnostic_link_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->link_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(this->diagnostic_link_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(diagnostic_link_vertices.size()));
    }

    const QVector<MapRhiScene::NodeVertex> &diagnostic_node_vertices =
        this->scene.diagnosticNodeVertices();
    if (!diagnostic_node_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->node_pipeline.get());
        command_buffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput binding(this->diagnostic_node_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(diagnostic_node_vertices.size()));
    }

    command_buffer->endPass();
}

void MapRhiWidget::releaseResources()
{
    resetGpuResources();
    this->active_rhi = nullptr;
    this->render_pass_descriptor = nullptr;
    this->ready_reported = false;
}

void MapRhiWidget::resizeEvent(QResizeEvent *event)
{
    this->viewport_size = event->size();
    this->camera.setViewportSize(this->viewport_size);
    QRhiWidget::resizeEvent(event);
}

bool MapRhiWidget::createPersistentResources()
{
    if (this->active_rhi == nullptr)
        return false;

    if (!this->uniform_buffer)
    {
        this->uniform_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, CameraUniformBytes));
        if (!this->uniform_buffer || !this->uniform_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI camera uniform buffer"));
            return false;
        }
    }

    if (!this->shader_resource_bindings)
    {
        this->shader_resource_bindings.reset(this->active_rhi->newShaderResourceBindings());
        if (!this->shader_resource_bindings)
        {
            reportFailure(QStringLiteral("Failed to allocate RHI shader resource bindings"));
            return false;
        }
        this->shader_resource_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, this->uniform_buffer.get())
        });
        if (!this->shader_resource_bindings->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI shader resource bindings"));
            return false;
        }
    }

    if (!this->heatmap_shader_resource_bindings)
    {
        this->heatmap_shader_resource_bindings.reset(
            this->active_rhi->newShaderResourceBindings());
        if (!this->heatmap_shader_resource_bindings)
        {
            reportFailure(QStringLiteral("Failed to allocate RHI heatmap shader bindings"));
            return false;
        }
        this->heatmap_shader_resource_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                this->uniform_buffer.get())
        });
        if (!this->heatmap_shader_resource_bindings->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI heatmap shader bindings"));
            return false;
        }
    }

    if (!this->icon_atlas_texture)
    {
        const QImage atlas_image = mapRhiIconAtlasImage();
        this->icon_atlas_texture.reset(this->active_rhi->newTexture(
            QRhiTexture::RGBA8, atlas_image.size()));
        if (!this->icon_atlas_texture || !this->icon_atlas_texture->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI icon atlas texture"));
            return false;
        }
        this->icon_atlas_upload_pending = true;
    }

    if (!this->icon_sampler)
    {
        this->icon_sampler.reset(this->active_rhi->newSampler(
            QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
            QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!this->icon_sampler || !this->icon_sampler->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI icon atlas sampler"));
            return false;
        }
    }

    if (!this->icon_shader_resource_bindings)
    {
        this->icon_shader_resource_bindings.reset(
            this->active_rhi->newShaderResourceBindings());
        if (!this->icon_shader_resource_bindings)
        {
            reportFailure(QStringLiteral("Failed to allocate RHI icon shader bindings"));
            return false;
        }
        this->icon_shader_resource_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, this->uniform_buffer.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                this->icon_atlas_texture.get(), this->icon_sampler.get())
        });
        if (!this->icon_shader_resource_bindings->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI icon shader bindings"));
            return false;
        }
    }

    if (!this->tank_texture)
    {
        const QImage tank_image = mapRhiTankAlbedoImage();
        if (tank_image.isNull())
        {
            reportFailure(QStringLiteral("Failed to load RHI tank model texture"));
            return false;
        }
        this->tank_texture.reset(this->active_rhi->newTexture(
            QRhiTexture::RGBA8, tank_image.size(), 1,
            QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
        if (!this->tank_texture || !this->tank_texture->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI tank model texture"));
            return false;
        }
        this->tank_texture_upload_pending = true;
    }

    if (!this->tank_sampler)
    {
        this->tank_sampler.reset(this->active_rhi->newSampler(
            QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
            QRhiSampler::Repeat, QRhiSampler::ClampToEdge));
        if (!this->tank_sampler || !this->tank_sampler->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI tank model sampler"));
            return false;
        }
    }

    if (!this->tank_shader_resource_bindings)
    {
        this->tank_shader_resource_bindings.reset(
            this->active_rhi->newShaderResourceBindings());
        if (!this->tank_shader_resource_bindings)
        {
            reportFailure(QStringLiteral("Failed to allocate RHI tank shader bindings"));
            return false;
        }
        this->tank_shader_resource_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, this->uniform_buffer.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                this->tank_texture.get(), this->tank_sampler.get())
        });
        if (!this->tank_shader_resource_bindings->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI tank shader bindings"));
            return false;
        }
    }

    return true;
}

bool MapRhiWidget::createPipelines()
{
    if (this->active_rhi == nullptr || this->render_pass_descriptor == nullptr
        || !this->shader_resource_bindings || !this->heatmap_shader_resource_bindings
        || !this->icon_shader_resource_bindings || !this->tank_shader_resource_bindings)
    {
        return false;
    }

    if (!this->link_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_link.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_link.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI link shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiScene::LinkVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::LinkVertex, start_x))},
            {0, 1, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::LinkVertex, end_x))},
            {0, 2, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::LinkVertex, along))},
            {0, 3, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiScene::LinkVertex, red))},
            {0, 4, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiScene::LinkVertex, size_adjust_px))}
        });

        this->link_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->link_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->link_pipeline->setVertexInputLayout(input_layout);
        this->link_pipeline->setShaderResourceBindings(this->shader_resource_bindings.get());
        this->link_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->link_pipeline->setSampleCount(sampleCount());
        this->link_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->link_pipeline->setDepthTest(true);
        this->link_pipeline->setDepthWrite(true);
        this->link_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        QRhiGraphicsPipeline::TargetBlend link_blend;
        link_blend.enable = true;
        this->link_pipeline->setTargetBlends({link_blend});
        if (!this->link_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI link graphics pipeline"));
            return false;
        }
    }

    if (!this->selected_link_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_link.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_link.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI selected-link shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiScene::LinkVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::LinkVertex, start_x))},
            {0, 1, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::LinkVertex, end_x))},
            {0, 2, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::LinkVertex, along))},
            {0, 3, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiScene::LinkVertex, red))},
            {0, 4, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiScene::LinkVertex, size_adjust_px))}
        });

        this->selected_link_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->selected_link_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->selected_link_pipeline->setVertexInputLayout(input_layout);
        this->selected_link_pipeline->setShaderResourceBindings(
            this->shader_resource_bindings.get());
        this->selected_link_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->selected_link_pipeline->setSampleCount(sampleCount());
        this->selected_link_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);

        // Selection is a UI overlay. Do not depth-test it against the pipe it is
        // highlighting, otherwise equal-depth segment joins can make the cyan stroke
        // appear to weave in and out of the selected pipe in perspective view.
        this->selected_link_pipeline->setDepthTest(false);
        this->selected_link_pipeline->setDepthWrite(false);
        QRhiGraphicsPipeline::TargetBlend selected_blend;
        selected_blend.enable = true;
        this->selected_link_pipeline->setTargetBlends({selected_blend});
        if (!this->selected_link_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI selected-link pipeline"));
            return false;
        }
    }

    if (!this->node_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_node.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_node.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI node shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiScene::NodeVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::NodeVertex, center_x))},
            {0, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::NodeVertex, corner_x))},
            {0, 2, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiScene::NodeVertex, red))},
            {0, 3, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiScene::NodeVertex, size_adjust_px))}
        });

        this->node_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->node_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->node_pipeline->setVertexInputLayout(input_layout);
        this->node_pipeline->setShaderResourceBindings(this->shader_resource_bindings.get());
        this->node_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->node_pipeline->setSampleCount(sampleCount());
        this->node_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->node_pipeline->setDepthTest(true);
        this->node_pipeline->setDepthWrite(true);
        this->node_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        QRhiGraphicsPipeline::TargetBlend node_blend;
        node_blend.enable = true;
        this->node_pipeline->setTargetBlends({node_blend});
        if (!this->node_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI node graphics pipeline"));
            return false;
        }
    }

    if (!this->icon_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_icon.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_icon.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI icon shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiScene::IconVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::IconVertex, center_x))},
            {0, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::IconVertex, offset_x_px))},
            {0, 2, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::IconVertex, u))},
            {0, 3, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiScene::IconVertex, red))}
        });

        this->icon_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->icon_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->icon_pipeline->setVertexInputLayout(input_layout);
        this->icon_pipeline->setShaderResourceBindings(
            this->icon_shader_resource_bindings.get());
        this->icon_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->icon_pipeline->setSampleCount(sampleCount());
        this->icon_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->icon_pipeline->setDepthTest(true);
        this->icon_pipeline->setDepthWrite(false);
        this->icon_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        this->icon_pipeline->setTargetBlends({blend});
        if (!this->icon_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI icon graphics pipeline"));
            return false;
        }
    }

    if (!this->heatmap_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_heatmap.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_heatmap.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI heatmap shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiScene::HeatmapVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::HeatmapVertex, center_x))},
            {0, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiScene::HeatmapVertex, corner_x))},
            {0, 2, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiScene::HeatmapVertex, red))}
        });

        this->heatmap_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->heatmap_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->heatmap_pipeline->setVertexInputLayout(input_layout);
        this->heatmap_pipeline->setShaderResourceBindings(
            this->heatmap_shader_resource_bindings.get());
        this->heatmap_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->heatmap_pipeline->setSampleCount(sampleCount());
        this->heatmap_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->heatmap_pipeline->setDepthTest(true);
        this->heatmap_pipeline->setDepthWrite(false);
        this->heatmap_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        QRhiGraphicsPipeline::TargetBlend heatmap_blend;
        heatmap_blend.enable = true;
        this->heatmap_pipeline->setTargetBlends({heatmap_blend});
        if (!this->heatmap_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI heatmap graphics pipeline"));
            return false;
        }
    }

    if (!this->tank_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_tank.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_tank.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI tank model shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiTankModelVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiTankModelVertex, position_x))},
            {0, 1, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiTankModelVertex, normal_x))},
            {0, 2, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(MapRhiTankModelVertex, u))}
        });

        this->tank_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->tank_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->tank_pipeline->setVertexInputLayout(input_layout);
        this->tank_pipeline->setShaderResourceBindings(
            this->tank_shader_resource_bindings.get());
        this->tank_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->tank_pipeline->setSampleCount(sampleCount());
        this->tank_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->tank_pipeline->setDepthTest(true);
        this->tank_pipeline->setDepthWrite(true);
        this->tank_pipeline->setCullMode(QRhiGraphicsPipeline::Back);
        this->tank_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        QRhiGraphicsPipeline::TargetBlend tank_blend;
        tank_blend.enable = true;
        this->tank_pipeline->setTargetBlends({tank_blend});
        if (!this->tank_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI tank graphics pipeline"));
            return false;
        }
    }


    if (!this->junction_pipeline)
    {
        const QShader vertex_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_junction.vert.qsb"));
        const QShader fragment_shader = loadShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_junction.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            reportFailure(QStringLiteral("Failed to load RHI junction sphere shaders"));
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(MapRhiJunctionMeshVertex))},
            {quint32(sizeof(MapRhiJunctionInstance)), QRhiVertexInputBinding::PerInstance}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiJunctionMeshVertex, position_x))},
            {0, 1, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiJunctionMeshVertex, normal_x))},
            {1, 2, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(MapRhiJunctionInstance, center_x))},
            {1, 3, QRhiVertexInputAttribute::Float,
             quint32(offsetof(MapRhiJunctionInstance, radius_world))},
            {1, 4, QRhiVertexInputAttribute::Float4,
             quint32(offsetof(MapRhiJunctionInstance, red))}
        });

        this->junction_pipeline.reset(this->active_rhi->newGraphicsPipeline());
        this->junction_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->junction_pipeline->setVertexInputLayout(input_layout);
        this->junction_pipeline->setShaderResourceBindings(this->shader_resource_bindings.get());
        this->junction_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->junction_pipeline->setSampleCount(sampleCount());
        this->junction_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->junction_pipeline->setDepthTest(true);
        this->junction_pipeline->setDepthWrite(true);
        this->junction_pipeline->setCullMode(QRhiGraphicsPipeline::Back);
        this->junction_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if (!this->junction_pipeline->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI junction sphere pipeline"));
            return false;
        }
    }


    return true;
}

bool MapRhiWidget::ensureGeometryBuffers()
{
    if (this->active_rhi == nullptr)
        return false;

    if (this->tank_upload_pending)
        rebuildTankModelGeometry();

    const int required_link_bytes = boundedBufferSize(
        this->scene.linkVertices().size(), qsizetype(sizeof(MapRhiScene::LinkVertex)));
    const int required_node_bytes = boundedBufferSize(
        this->scene.nodeVertices().size(), qsizetype(sizeof(MapRhiScene::NodeVertex)));
    const int required_selected_link_bytes = boundedBufferSize(
        this->scene.selectedLinkVertices().size(), qsizetype(sizeof(MapRhiScene::LinkVertex)));
    const int required_selected_node_bytes = boundedBufferSize(
        this->scene.selectedNodeVertices().size(), qsizetype(sizeof(MapRhiScene::NodeVertex)));
    const int required_diagnostic_link_bytes = boundedBufferSize(
        this->scene.diagnosticLinkVertices().size(), qsizetype(sizeof(MapRhiScene::LinkVertex)));
    const int required_diagnostic_node_bytes = boundedBufferSize(
        this->scene.diagnosticNodeVertices().size(), qsizetype(sizeof(MapRhiScene::NodeVertex)));
    const int required_flow_direction_bytes = boundedBufferSize(
        this->scene.flowDirectionVertices().size(), qsizetype(sizeof(MapRhiScene::LinkVertex)));
    const int required_icon_bytes = boundedBufferSize(
        this->scene.iconVertices().size(), qsizetype(sizeof(MapRhiScene::IconVertex)));
    const int required_heatmap_bytes = boundedBufferSize(
        this->scene.heatmapVertices().size(), qsizetype(sizeof(MapRhiScene::HeatmapVertex)));
    const int required_tank_bytes = boundedBufferSize(
        this->tank_model_vertices.size(), qsizetype(sizeof(MapRhiTankModelVertex)));
    const QVector<MapRhiJunctionMeshVertex> &junction_mesh =
        mapRhiJunctionSphereMeshVertices();
    const int required_junction_mesh_bytes = boundedBufferSize(
        junction_mesh.size(), qsizetype(sizeof(MapRhiJunctionMeshVertex)));
    const int required_junction_instance_bytes = boundedBufferSize(
        this->scene.junctionInstances().size(), qsizetype(sizeof(MapRhiJunctionInstance)));
    if (required_link_bytes == 0 || required_node_bytes == 0
        || required_selected_link_bytes == 0 || required_selected_node_bytes == 0
        || required_diagnostic_link_bytes == 0 || required_diagnostic_node_bytes == 0
        || required_flow_direction_bytes == 0 || required_icon_bytes == 0
        || required_heatmap_bytes == 0 || required_tank_bytes == 0
        || required_junction_mesh_bytes == 0 || required_junction_instance_bytes == 0)
    {
        reportFailure(QStringLiteral("RHI network geometry exceeds supported buffer size"));
        return false;
    }

    if (!this->link_vertex_buffer || this->link_vertex_buffer_size != required_link_bytes)
    {
        this->link_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_link_bytes));
        if (!this->link_vertex_buffer || !this->link_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI link vertex buffer"));
            return false;
        }
        this->link_vertex_buffer_size = required_link_bytes;
        this->geometry_upload_pending = true;
    }

    if (!this->node_vertex_buffer || this->node_vertex_buffer_size != required_node_bytes)
    {
        this->node_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_node_bytes));
        if (!this->node_vertex_buffer || !this->node_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI node vertex buffer"));
            return false;
        }
        this->node_vertex_buffer_size = required_node_bytes;
        this->geometry_upload_pending = true;
    }

    if (!this->selected_link_vertex_buffer
        || this->selected_link_vertex_buffer_size != required_selected_link_bytes)
    {
        this->selected_link_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_selected_link_bytes));
        if (!this->selected_link_vertex_buffer || !this->selected_link_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI selected-link vertex buffer"));
            return false;
        }
        this->selected_link_vertex_buffer_size = required_selected_link_bytes;
        this->highlight_upload_pending = true;
    }

    if (!this->selected_node_vertex_buffer
        || this->selected_node_vertex_buffer_size != required_selected_node_bytes)
    {
        this->selected_node_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_selected_node_bytes));
        if (!this->selected_node_vertex_buffer || !this->selected_node_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI selected-node vertex buffer"));
            return false;
        }
        this->selected_node_vertex_buffer_size = required_selected_node_bytes;
        this->highlight_upload_pending = true;
    }

    if (!this->diagnostic_link_vertex_buffer
        || this->diagnostic_link_vertex_buffer_size != required_diagnostic_link_bytes)
    {
        this->diagnostic_link_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_diagnostic_link_bytes));
        if (!this->diagnostic_link_vertex_buffer || !this->diagnostic_link_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI diagnostic-link vertex buffer"));
            return false;
        }
        this->diagnostic_link_vertex_buffer_size = required_diagnostic_link_bytes;
        this->highlight_upload_pending = true;
    }

    if (!this->diagnostic_node_vertex_buffer
        || this->diagnostic_node_vertex_buffer_size != required_diagnostic_node_bytes)
    {
        this->diagnostic_node_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_diagnostic_node_bytes));
        if (!this->diagnostic_node_vertex_buffer || !this->diagnostic_node_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI diagnostic-node vertex buffer"));
            return false;
        }
        this->diagnostic_node_vertex_buffer_size = required_diagnostic_node_bytes;
        this->highlight_upload_pending = true;
    }

    if (!this->flow_direction_vertex_buffer
        || this->flow_direction_vertex_buffer_size != required_flow_direction_bytes)
    {
        this->flow_direction_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_flow_direction_bytes));
        if (!this->flow_direction_vertex_buffer || !this->flow_direction_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI flow-direction vertex buffer"));
            return false;
        }
        this->flow_direction_vertex_buffer_size = required_flow_direction_bytes;
        this->flow_direction_upload_pending = true;
    }

    if (!this->icon_vertex_buffer || this->icon_vertex_buffer_size != required_icon_bytes)
    {
        this->icon_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_icon_bytes));
        if (!this->icon_vertex_buffer || !this->icon_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI icon vertex buffer"));
            return false;
        }
        this->icon_vertex_buffer_size = required_icon_bytes;
        this->icon_upload_pending = true;
    }

    if (!this->heatmap_vertex_buffer
        || this->heatmap_vertex_buffer_size != required_heatmap_bytes)
    {
        this->heatmap_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_heatmap_bytes));
        if (!this->heatmap_vertex_buffer || !this->heatmap_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI heatmap vertex buffer"));
            return false;
        }
        this->heatmap_vertex_buffer_size = required_heatmap_bytes;
        this->heatmap_upload_pending = true;
    }

    if (!this->tank_vertex_buffer || this->tank_vertex_buffer_size != required_tank_bytes)
    {
        this->tank_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_tank_bytes));
        if (!this->tank_vertex_buffer || !this->tank_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI tank vertex buffer"));
            return false;
        }
        this->tank_vertex_buffer_size = required_tank_bytes;
        this->tank_upload_pending = true;
    }

    if (!this->junction_mesh_vertex_buffer
        || this->junction_mesh_vertex_buffer_size != required_junction_mesh_bytes)
    {
        this->junction_mesh_vertex_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, required_junction_mesh_bytes));
        if (!this->junction_mesh_vertex_buffer || !this->junction_mesh_vertex_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI junction sphere mesh buffer"));
            return false;
        }
        this->junction_mesh_vertex_buffer_size = required_junction_mesh_bytes;
        this->junction_mesh_upload_pending = true;
    }

    if (!this->junction_instance_buffer
        || this->junction_instance_buffer_size != required_junction_instance_bytes)
    {
        this->junction_instance_buffer.reset(this->active_rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_junction_instance_bytes));
        if (!this->junction_instance_buffer || !this->junction_instance_buffer->create())
        {
            reportFailure(QStringLiteral("Failed to create RHI junction sphere instance buffer"));
            return false;
        }
        this->junction_instance_buffer_size = required_junction_instance_bytes;
        this->junction_instance_upload_pending = true;
    }

    return true;
}

void MapRhiWidget::resetGpuResources()
{
    if (this->basemap_renderer)
        this->basemap_renderer->releaseResources();
    this->junction_pipeline.reset();
    this->tank_pipeline.reset();
    this->heatmap_pipeline.reset();
    this->icon_pipeline.reset();
    this->node_pipeline.reset();
    this->selected_link_pipeline.reset();
    this->link_pipeline.reset();
    this->tank_shader_resource_bindings.reset();
    this->icon_shader_resource_bindings.reset();
    this->heatmap_shader_resource_bindings.reset();
    this->shader_resource_bindings.reset();
    this->tank_sampler.reset();
    this->icon_sampler.reset();
    this->tank_texture.reset();
    this->icon_atlas_texture.reset();
    this->junction_instance_buffer.reset();
    this->junction_mesh_vertex_buffer.reset();
    this->tank_vertex_buffer.reset();
    this->heatmap_vertex_buffer.reset();
    this->icon_vertex_buffer.reset();
    this->flow_direction_vertex_buffer.reset();
    this->diagnostic_node_vertex_buffer.reset();
    this->diagnostic_link_vertex_buffer.reset();
    this->selected_node_vertex_buffer.reset();
    this->selected_link_vertex_buffer.reset();
    this->node_vertex_buffer.reset();
    this->link_vertex_buffer.reset();
    this->uniform_buffer.reset();
    this->junction_instance_buffer_size = 0;
    this->junction_mesh_vertex_buffer_size = 0;
    this->tank_vertex_buffer_size = 0;
    this->heatmap_vertex_buffer_size = 0;
    this->icon_vertex_buffer_size = 0;
    this->flow_direction_vertex_buffer_size = 0;
    this->diagnostic_node_vertex_buffer_size = 0;
    this->diagnostic_link_vertex_buffer_size = 0;
    this->selected_node_vertex_buffer_size = 0;
    this->selected_link_vertex_buffer_size = 0;
    this->node_vertex_buffer_size = 0;
    this->link_vertex_buffer_size = 0;
    this->geometry_upload_pending = true;
    this->highlight_upload_pending = true;
    this->flow_direction_upload_pending = true;
    this->icon_upload_pending = true;
    this->heatmap_upload_pending = true;
    this->tank_upload_pending = true;
    this->junction_mesh_upload_pending = true;
    this->junction_instance_upload_pending = true;
    this->icon_atlas_upload_pending = true;
    this->tank_texture_upload_pending = true;
    this->tank_model_vertices.clear();
}

void MapRhiWidget::rebuildTankModelGeometry()
{
    this->tank_model_vertices = mapRhiBuildTankModelVertices(this->scene.tankInstances());
}

void MapRhiWidget::syncViewState()
{
    this->viewport_size = size();
    this->camera.setViewportSize(this->viewport_size);
    this->camera.setSceneOriginWorld(renderOriginWorld());
    this->camera.syncFromMapModel(*this->map_model);

    const bool use_3d_models = this->map_model->viewMode() == MapViewMode::ThreeD;
    if (this->scene.setUse3dTankModels(use_3d_models))
    {
        this->icon_upload_pending = true;
        this->tank_upload_pending = true;
    }
    if (this->scene.setUse3dJunctionModels(use_3d_models))
    {
        this->geometry_upload_pending = true;
        this->junction_instance_upload_pending = true;
    }
}

QPointF MapRhiWidget::renderOriginWorld() const
{
    return this->scene.hasGeometry()
        ? this->scene.originWorld()
        : this->fallback_origin_world;
}

float MapRhiWidget::heatmapRadiusPixels() const
{
    if (this->applied_symbology.visual_heatmap == VisualHeatmap::None)
        return 1.0f;

    if (this->applied_symbology.heatmap_radius_unit == HeatmapRadiusUnit::Pixels)
        return float(qMax(1, this->applied_symbology.heatmap_radius_px));

    const double meters_per_pixel = GeoWebMercator::metersPerPixel(
        this->map_model->centerLat(), this->map_model->zoom());
    if (!std::isfinite(meters_per_pixel) || meters_per_pixel <= 0.0)
        return 1.0f;

    return float(qMax(1.0,
        this->applied_symbology.heatmap_radius_m / meters_per_pixel));
}

void MapRhiWidget::reportFailure(const QString &reason)
{
    if (this->failure_reported)
        return;

    this->failure_reported = true;
    qWarning().noquote()
        << QStringLiteral("Desktop map RHI surface '%1' failed: %2.")
               .arg(this->surface_name, reason);
    emit signalRendererFailed(reason);
}
