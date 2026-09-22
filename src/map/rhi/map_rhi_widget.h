#ifndef MAP_RHI_WIDGET_H
#define MAP_RHI_WIDGET_H

#include "map/rhi/map_rhi_basemap_renderer.h"
#include "map/rhi/map_rhi_camera.h"
#include "map/rhi/map_rhi_globe_network_backend.h"
#include "map/render/map_globe_network_scene.h"
#include "map/render/map_globe_render_frame.h"
#include "map/rhi/map_rhi_globe_renderer.h"
#include "map/rhi/map_rhi_scene.h"
#include "map/render/map_globe_junction_model.h"
#include "map/render/map_globe_reservoir_model.h"
#include "map/render/map_globe_tank_model.h"

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
class QRhiRenderTarget;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QResizeEvent;
class QEvent;
class QTimer;

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

enum class MapRhiGlobeSurfaceHitSource
{
    Invalid,
    TerrainMesh,
    EllipsoidFallback
};

struct MapRhiGlobeSurfaceHit
{
    CoordinateWGS84 coordinate;
    GeoWgs84Ellipsoid::EcefPositionD ecef_position;
    double surface_height_m = 0.0;
    double distance_m = 0.0;
    MapRhiGlobeSurfaceHitSource source =
        MapRhiGlobeSurfaceHitSource::Invalid;

    bool isValid() const
    {
        return this->source != MapRhiGlobeSurfaceHitSource::Invalid;
    }

    bool isTerrainMesh() const
    {
        return this->source == MapRhiGlobeSurfaceHitSource::TerrainMesh;
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
    // Globe surface picking against the exact DEM triangles currently
    // retained by MapRhiGlobeRenderer, with a WGS84 ellipsoid fallback when
    // no rendered DEM triangle is hit. Cursor-coordinate lookup, orbit
    // focus capture and the guarded terrain-aware pan path use this.
    bool globeSurfaceRayHitAtScreen(
        const QPointF &screen_position, MapRhiGlobeSurfaceHit *hit) const;
    bool panGlobeByTerrainPixels(const QPoint &delta_pixels);
    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot);
    void setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids);
    void setNodeDeclutteringEnabled(bool enabled);
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
    void setGlobe3dIconsEnabled(bool enabled);
    void setUndergroundMode(MapRhiUndergroundMode mode);
    MapRhiUndergroundMode undergroundMode() const;
    void setTerrainWireframeVisible(bool visible);
    void setMapTilesVisible(bool visible);
    void globeTerrainMeshProgress(int *completed, int *total, bool *active) const;

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
    void resetGpuResources();
    void renderGlobe(QRhiCommandBuffer *command_buffer, QRhiRenderTarget *target);
    void uploadNetworkStyleTable(QRhiResourceUpdateBatch *resource_updates);
    // Globe-mode counterpart of hitTest() -- see that function's header
    // comment for why it can't just be folded into the same function body.
    MapRhiHit globeHitTest(const QPointF &screen_position) const;
    void syncViewState();
    void syncGlobeTerrainAwareCameraHeight(bool request_missing_tile);
    void scheduleGlobeUndergroundXRayRefresh();
    void captureViewGlobeFocusAnchor();
    void rebuildHeatmapRenderVertices();
    void syncBasemapHeatmapOverlay();
    void syncBasemapHeatmapStyle();
    // Shared by both of the above -- see its own definition's comment for
    // why Globe doesn't need (or benefit from) the same data/style split
    // the flat basemap renderer has.
    void syncGlobeHeatmapOverlay(double solid_fraction);
    bool globeTerrainElevationAtCoordinate(
        const CoordinateWGS84 &coordinate, double *elevation_m,
        double *cell_size_m) const;
    bool globeCameraTerrainElevationAtCoordinate(
        const CoordinateWGS84 &coordinate, double *elevation_m,
        bool request_missing_tile) const;
    QPointF renderOriginWorld() const;
    float heatmapRadiusPixels() const;
    // Globe counterpart of heatmapRadiusPixels() -- returns a real-world
    // meters radius directly, rather than a screen-pixel one, since
    // MapRhiGlobeRenderer::renderHeatmapTile() needs meters (it works in
    // Web Mercator tile-fraction space at whatever zoom a given tile
    // happens to be, not screen space). See its own definition for how a
    // pixel-configured radius gets converted.
    double globeHeatmapRadiusMeters() const;
    void reportFailure(const QString &reason);

    MapModel *map_model = nullptr;
    QString surface_name;
    QRhi *active_rhi = nullptr;
    QRhiRenderPassDescriptor *render_pass_descriptor = nullptr;
    QSize viewport_size;
    QPointF fallback_origin_world;
    MapRhiCamera camera;
    MapRhiScene scene;
    MapGlobeNetworkScene globe_network_scene;
    MapRhiSymbology applied_symbology;
    MapTileRepository *tile_repository = nullptr;
    MapTerrainRepository *terrain_repository = nullptr;
    std::unique_ptr<MapRhiBasemapRenderer> basemap_renderer;
    std::unique_ptr<MapRhiGlobeRenderer> globe_renderer;
    int background_opacity = 0;
    QPointF network_screen_translation;
    QSize network_style_texture_size;

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
    MapRhiGlobeNetworkBackend globe_network_backend;
    std::unique_ptr<QRhiTexture> icon_atlas_texture;
    std::unique_ptr<QRhiTexture> tank_texture;
    std::unique_ptr<QRhiTexture> reservoir_texture;
    std::unique_ptr<QRhiTexture> network_style_texture;
    std::unique_ptr<QRhiSampler> icon_sampler;
    std::unique_ptr<QRhiSampler> tank_sampler;
    std::unique_ptr<QRhiSampler> reservoir_sampler;
    std::unique_ptr<QRhiSampler> network_style_sampler;
    std::unique_ptr<QRhiShaderResourceBindings> shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> junction_shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> heatmap_shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> icon_shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> tank_shader_resource_bindings;
    std::unique_ptr<QRhiShaderResourceBindings> reservoir_shader_resource_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> link_pipeline;
    // Globe chevrons retain their surface-aligned 3D geometry, but use a
    // depth-stable raster path independent of the ordinary depth-tested
    // network pass.
    std::unique_ptr<QRhiGraphicsPipeline> globe_flow_direction_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> selected_link_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> node_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> node_overlay_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> icon_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> icon_overlay_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> heatmap_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> tank_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> reservoir_pipeline;
    // Globe-owned junction pipelines use the same shader, quad, instance
    // layout and no-culling state used by the Globe, but keep their own pipeline
    // objects so Globe-specific render-state changes stay isolated.
    std::unique_ptr<QRhiGraphicsPipeline> globe_junction_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> globe_junction_no_depth_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> link_xray_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> junction_xray_pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> link_no_depth_pipeline;
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
    bool geometry_upload_pending = true;
    bool highlight_upload_pending = true;
    bool flow_direction_upload_pending = true;
    bool icon_upload_pending = true;
    bool heatmap_upload_pending = true;
    bool tank_upload_pending = true;
    bool reservoir_upload_pending = true;
    bool junction_mesh_upload_pending = true;
    bool network_style_upload_pending = true;
    bool icon_atlas_upload_pending = true;
    bool tank_texture_upload_pending = true;
    bool reservoir_texture_upload_pending = true;
    QVector<MapRhiScene::HeatmapVertex> heatmap_render_vertices;
    QVector<MapGlobeTankModelVertex> tank_model_vertices;
    QVector<MapGlobeReservoirModelVertex> reservoir_model_vertices;
    MapRhiUndergroundMode underground_mode = MapRhiUndergroundMode::XRay;
    bool symbology_initialized = false;
    bool ready_reported = false;
    bool failure_reported = false;
    bool globe_terrain_camera_sync_active = false;
    bool globe_terrain_prime_after_network_pending = false;
    bool globe_underground_refresh_requested = false;
    quint64 globe_underground_refresh_generation = 0;
    QElapsedTimer globe_terrain_follow_clock;
    QTimer *globe_terrain_follow_timer = nullptr;
};

#endif // MAP_RHI_WIDGET_H
