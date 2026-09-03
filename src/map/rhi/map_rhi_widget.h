#ifndef MAP_RHI_WIDGET_H
#define MAP_RHI_WIDGET_H

#include "map/rhi/map_rhi_basemap_renderer.h"
#include "map/rhi/map_rhi_camera.h"
#include "map/rhi/map_rhi_scene.h"
#include "map/rhi/map_rhi_junction_model.h"
#include "map/rhi/map_rhi_reservoir_model.h"
#include "map/rhi/map_rhi_tank_model.h"

#include <QRhiWidget>
#include <QElapsedTimer>
#include <QPointF>
#include <QSet>
#include <QSize>
#include <QString>

#include <memory>

class MapModel;
class MapTerrainRepository;
class MapTileRepository;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QResizeEvent;
class QEvent;

enum class MapRhiUndergroundMode
{
    XRay,
    Hide,
    Solid
};

struct MapRhiHit
{
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    QUuid uuid;

    bool isValid() const
    {
        return this->render_id != 0 && this->entity_type != InfrastructureEntity::Unknown;
    }
};

class MapRhiWidget final : public QRhiWidget
{
    Q_OBJECT

public:
    explicit MapRhiWidget(MapModel *map_model, const QString &surface_name,
                          QWidget *parent = nullptr);
    ~MapRhiWidget() override;

    QString graphicsApiName() const;
    MapRhiHit hitTest(const QPointF &screen_position) const;
    bool terrainCoordinateAtScreen(
        const QPointF &screen_position, CoordinateWGS84 *coordinate,
        bool request_missing_tile = true);
    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot);
    void setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids);
    void setNetworkScreenTranslation(const QPointF &translation_pixels);
    void setSymbology(const MapRhiSymbology &symbology);
    void setVisualControlSettings(const NetworkSymbologySettings &settings);
    void setTileRepository(MapTileRepository *tile_repository);
    void setTerrainRepository(MapTerrainRepository *terrain_repository);
    void setBackgroundOpacity(int opacity);
    void setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid);
    void setSimulationErrorEntities(
        const QHash<QUuid, InfrastructureEntity> &error_entities,
        const QSet<QUuid> &stale_entity_uuids);
    void setUndergroundMode(MapRhiUndergroundMode mode);
    MapRhiUndergroundMode undergroundMode() const;
    void setTerrainWireframeVisible(bool visible);
    void setMapTilesVisible(bool visible);

signals:
    void signalRendererReady();
    void signalRendererFailed(const QString &reason);

protected:
    void changeEvent(QEvent *event) override;
    void initialize(QRhiCommandBuffer *command_buffer) override;
    void render(QRhiCommandBuffer *command_buffer) override;
    void releaseResources() override;
    void resizeEvent(QResizeEvent *event) override;

private:
    bool createPersistentResources();
    bool createPipelines();
    bool ensureGeometryBuffers();
    void rebuildTankModelGeometry();
    void rebuildReservoirModelGeometry();
    void rebuildUndergroundGeometry();
    void markUndergroundGeometryDirty();
    bool isUndergroundAtCoordinate(
        const CoordinateWGS84 &coordinate, double elevation_m);
    double terrainCellWorldSize() const;
    void appendUndergroundLinkSegment(
        InfrastructureEntity entity_type, quint32 render_id,
        const QVector3D &start, const QVector3D &end);
    void resetGpuResources();
    void syncViewState();
    void syncTerrainAwareCameraDistance();
    void captureView3dFocusAnchor();
    bool terrainRayHitAtScreen(
        const QPointF &screen_position, CoordinateWGS84 *coordinate,
        double *world_z, double *distance_m, bool request_missing_tile);
    void rebuildHeatmapRenderVertices();
    void syncBasemapHeatmapOverlay();
    void syncBasemapHeatmapStyle();
    double terrainWorldUnitsPerMeter() const;
    double terrainWorldZ(double elevation_m, double world_units_per_meter) const;
    bool terrainElevationAtCoordinate(
        const CoordinateWGS84 &coordinate, double *elevation_m,
        bool request_missing_tile = true);
    QPointF renderOriginWorld() const;
    float heatmapRadiusPixels() const;
    void reportFailure(const QString &reason);

    MapModel *map_model = nullptr;
    QString surface_name;
    QRhi *active_rhi = nullptr;
    QRhiRenderPassDescriptor *render_pass_descriptor = nullptr;
    QSize viewport_size;
    QPointF fallback_origin_world;
    MapRhiCamera camera;
    MapRhiScene scene;
    MapRhiSymbology applied_symbology;
    MapTileRepository *tile_repository = nullptr;
    MapTerrainRepository *terrain_repository = nullptr;
    std::unique_ptr<MapRhiBasemapRenderer> basemap_renderer;
    int background_opacity = 0;
    QPointF network_screen_translation;

    std::unique_ptr<QRhiBuffer> uniform_buffer;
    std::unique_ptr<QRhiBuffer> link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> node_vertex_buffer;
    std::unique_ptr<QRhiBuffer> selected_link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> selected_node_vertex_buffer;
    std::unique_ptr<QRhiBuffer> diagnostic_link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> diagnostic_node_vertex_buffer;
    std::unique_ptr<QRhiBuffer> flow_direction_vertex_buffer;
    std::unique_ptr<QRhiBuffer> icon_vertex_buffer;
    std::unique_ptr<QRhiBuffer> heatmap_vertex_buffer;
    std::unique_ptr<QRhiBuffer> tank_vertex_buffer;
    std::unique_ptr<QRhiBuffer> reservoir_vertex_buffer;
    std::unique_ptr<QRhiBuffer> junction_mesh_vertex_buffer;
    std::unique_ptr<QRhiBuffer> junction_instance_buffer;
    std::unique_ptr<QRhiBuffer> underground_link_vertex_buffer;
    std::unique_ptr<QRhiBuffer> underground_junction_instance_buffer;
    std::unique_ptr<QRhiTexture> icon_atlas_texture;
    std::unique_ptr<QRhiTexture> tank_texture;
    std::unique_ptr<QRhiTexture> reservoir_texture;
    std::unique_ptr<QRhiSampler> icon_sampler;
    std::unique_ptr<QRhiSampler> tank_sampler;
    std::unique_ptr<QRhiSampler> reservoir_sampler;
    std::unique_ptr<QRhiShaderResourceBindings> shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> heatmap_shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> icon_shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> tank_shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> reservoir_shader_resource_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> link_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> selected_link_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> node_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> node_overlay_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> icon_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> icon_overlay_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> heatmap_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> tank_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> reservoir_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> junction_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> link_xray_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> junction_xray_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> link_no_depth_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> junction_no_depth_pipeline;
    int link_vertex_buffer_size = 0;
    int node_vertex_buffer_size = 0;
    int selected_link_vertex_buffer_size = 0;
    int selected_node_vertex_buffer_size = 0;
    int diagnostic_link_vertex_buffer_size = 0;
    int diagnostic_node_vertex_buffer_size = 0;
    int flow_direction_vertex_buffer_size = 0;
    int icon_vertex_buffer_size = 0;
    int heatmap_vertex_buffer_size = 0;
    int tank_vertex_buffer_size = 0;
    int reservoir_vertex_buffer_size = 0;
    int junction_mesh_vertex_buffer_size = 0;
    int junction_instance_buffer_size = 0;
    int underground_link_vertex_buffer_size = 0;
    int underground_junction_instance_buffer_size = 0;
    bool geometry_upload_pending = true;
    bool highlight_upload_pending = true;
    bool flow_direction_upload_pending = true;
    bool icon_upload_pending = true;
    bool heatmap_upload_pending = true;
    bool tank_upload_pending = true;
    bool reservoir_upload_pending = true;
    bool junction_mesh_upload_pending = true;
    bool junction_instance_upload_pending = true;
    bool underground_geometry_upload_pending = true;
    bool underground_geometry_dirty = true;
    bool icon_atlas_upload_pending = true;
    bool tank_texture_upload_pending = true;
    bool reservoir_texture_upload_pending = true;
    QVector<MapRhiScene::HeatmapVertex> heatmap_render_vertices;
    QVector<MapRhiTankModelVertex> tank_model_vertices;
    QVector<MapRhiReservoirModelVertex> reservoir_model_vertices;
    QVector<MapRhiScene::LinkVertex> underground_link_vertices;
    QVector<MapRhiJunctionInstance> underground_junction_instances;
    MapRhiUndergroundMode underground_mode = MapRhiUndergroundMode::XRay;
    bool symbology_initialized = false;
    bool ready_reported = false;
    bool failure_reported = false;
    bool terrain_camera_distance_sync_active = false;
    QElapsedTimer terrain_pan_smoothing_clock;
};

#endif // MAP_RHI_WIDGET_H
