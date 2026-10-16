#ifndef MAP_BGFX_GLOBE_NETWORK_H
#define MAP_BGFX_GLOBE_NETWORK_H

#include "map/render/map_globe_network_scene.h"
#include "map/render/map_network_render_symbology.h"
#include "map/render/map_render_surface.h"
#include "network/network_render_snapshot.h"

#include <QHash>
#include <QSet>
#include <QSize>
#include <QUuid>
#include <QVector>

#include <bgfx/bgfx.h>

class MapGlobeCamera;

// The EPANET network on the bgfx globe (roadmap Phase 3, slice 3): pipes,
// generic node markers, junction sphere impostors, tank and reservoir 3D
// models, pump/valve/tank/reservoir icons, flow-direction chevrons,
// selection and simulation-diagnostic highlights, and the underground modes
// (Solid: the whole network through the terrain; X-Ray: buried runs dashed
// and dithered through the terrain; Hide: the terrain occludes them).
//
// All geometry is the backend-neutral MapGlobeNetworkScene the QRhi globe
// uses (WGS84 placement relative to a retained network origin, metre and
// pixel sizing, adaptive subdivision, declutter, X-Ray classification); the
// shaders are the QRhi globe shaders in bgfx form (vs/fs_globe_link, ...).
// Differences:
//
// - inputs are applied to the scene lazily, on the next globe frame
//   (synchronize()): the 2D map never pays for globe geometry;
// - junction impostors are expanded to six vertices each with their colour
//   resolved on the CPU (symbology, selection, diagnostics, visibility),
//   instead of instancing plus a style-table texture read in the vertex
//   shader (WebGL-friendly, no vertex texture fetch);
// - flow chevrons keep their 8 px pull towards the eye but have no depth
//   bias (bgfx has no depth bias state).
//
// Not yet: the globe heatmap (per-tile bake into the terrain imagery).
class MapBgfxGlobeNetwork
{
public:
    MapBgfxGlobeNetwork();
    ~MapBgfxGlobeNetwork();

    // Programs, uniforms, the tank/reservoir textures. false: no network on
    // the globe on this device.
    bool initialize();
    void release();
    bool ready() const;

    // --- Inputs (stored; applied by the next globe frame) -----------------
    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot);
    void setHiddenEntityUuids(const QSet<QUuid> &uuids);
    // Already themed by the caller (icon fill colour).
    void setSymbology(const MapNetworkRenderSymbology &symbology);
    void setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid);
    void setSimulationErrorEntities(const QHash<QUuid, InfrastructureEntity> &error_entities,
                                    const QSet<QUuid> &stale_entity_uuids);
    void setUse3dIconModels(bool enabled);
    void setNodeDeclutteringEnabled(bool enabled);
    void setGroundOffsetM(double offset_m);
    void setVerticalExaggeration(double exaggeration);
    // DEM lookup for the X-Ray classification (MapGlobeTerrainNavigation).
    void setTerrainElevationResolver(MapGlobeNetworkScene::TerrainElevationResolver resolver);
    void setUndergroundMode(MapUndergroundMode mode);
    MapUndergroundMode undergroundMode() const { return this->underground_mode; }
    // X-Ray: classifies the network against the DEM cached now (after
    // terrain arrived or the camera settled). true when it ran.
    bool refreshUndergroundXRay();

    // Applies stored inputs to the scene (geometry rebuilt as needed).
    void synchronize();
    const MapGlobeNetworkScene &scene() const { return this->network_scene; }

    struct FrameResult
    {
        int draw_calls = 0;
        quint64 upload_bytes = 0;
    };
    // Draws the network into view after the terrain surface. camera: the
    // frame's camera (viewport and origin already synchronized);
    // icon_atlas: the shared network icon atlas (MapBgfx2dGpu).
    FrameResult render(bgfx::ViewId view, const MapGlobeCamera &camera, const QSize &logical_viewport,
                       bgfx::TextureHandle icon_atlas);

private:
    // GPU streams that are out of date, as the QRhi backend's invalidations.
    enum Stream : quint32
    {
        GeometryStream = 1u << 0,     // links, nodes
        HighlightStream = 1u << 1,    // selected / diagnostic links and nodes
        FlowStream = 1u << 2,         // chevrons
        IconStream = 1u << 3,         // icons, tank and reservoir models
        UndergroundStream = 1u << 4,  // X-Ray links and junctions
        JunctionStream = 1u << 5,     // junction impostors (instances, colours)
        AllStreams = 0x3fu
    };
    // One dynamic vertex buffer and the vertex count uploaded into it.
    struct Buffer
    {
        bgfx::DynamicVertexBufferHandle handle = BGFX_INVALID_HANDLE;
        quint32 count = 0;
    };
    // A junction impostor corner: centre, corner, resolved colour, radius.
    struct JunctionVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float corner_x = 0.0f;
        float corner_y = 0.0f;
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 0.0f;
        float radius = 0.0f;
    };

    // Uniform values of one frame (camera, viewport, sizes, depth range).
    struct FrameUniforms
    {
        float view_projection[16];
        float viewport[4];
        float params[4];
        float camera_right[4];
        float camera_up[4];
        float camera_eye[4];
    };

    void uploadStreams(FrameResult *result);
    // One draw of a stream (nothing when it is empty).
    void submitStream(bgfx::ViewId view, const Buffer &buffer, bgfx::ProgramHandle program, uint64_t state,
                      const FrameUniforms &uniforms, bgfx::TextureHandle texture,
                      bgfx::UniformHandle sampler, FrameResult *result) const;
    QVector<JunctionVertex> junctionVertices(const QVector<MapGlobeJunctionInstance> &instances) const;
    // Resolved junction colour (0 alpha: not drawn), as the QRhi junction
    // shader with the style table: error red, stale grey, selection cyan,
    // else the symbology colour.
    bool junctionColor(quint32 render_id, float (&rgba)[4]) const;
    void destroyBuffers();

    MapGlobeNetworkScene network_scene;

    // Stored inputs and what still has to reach the scene.
    NetworkRenderSnapshot snapshot;
    bool snapshot_pending = false;
    QSet<QUuid> hidden_entity_uuids;
    bool hidden_pending = false;
    MapNetworkRenderSymbology symbology;
    bool symbology_pending = false;
    // What the scene has: the change detection of the next symbology.
    MapNetworkRenderSymbology applied_symbology;
    bool symbology_applied = false;
    InfrastructureEntity selected_entity_type = InfrastructureEntity::Unknown;
    QUuid selected_entity_uuid;
    bool selection_pending = false;
    QHash<QUuid, InfrastructureEntity> simulation_error_entities;
    QSet<QUuid> simulation_stale_entity_uuids;
    bool errors_pending = false;
    bool use_3d_icon_models = false;
    bool icon_models_pending = false;
    bool node_decluttering_enabled = true;
    bool declutter_pending = false;
    double ground_offset_m = 0.0;
    bool ground_offset_pending = false;
    double vertical_exaggeration = 1.0;
    bool exaggeration_pending = false;
    MapUndergroundMode underground_mode = MapUndergroundMode::XRay;
    bool underground_pending = true;
    // Junction render id -> uuid (selection, diagnostics, hidden entities).
    QHash<quint32, QUuid> junction_uuids;
    // A link thickness change rebuilds the scene's geometry when the network
    // has demand point attachments.
    bool has_attachments = false;
    quint32 dirty_streams = AllStreams;

    bgfx::VertexLayout link_layout;
    bgfx::VertexLayout node_layout;
    bgfx::VertexLayout icon_layout;
    bgfx::VertexLayout junction_layout;
    bgfx::VertexLayout model_layout;
    bgfx::ProgramHandle link_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle link_xray_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle flow_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle node_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle junction_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle junction_xray_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle icon_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle model_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle view_projection_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle viewport_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle params_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle camera_right_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle camera_up_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle camera_eye_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle icon_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle albedo_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle tank_texture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle reservoir_texture = BGFX_INVALID_HANDLE;

    Buffer link_buffer;
    Buffer node_buffer;
    Buffer selected_link_buffer;
    Buffer selected_node_buffer;
    Buffer diagnostic_link_buffer;
    Buffer diagnostic_node_buffer;
    Buffer flow_buffer;
    Buffer icon_buffer;
    Buffer underground_link_buffer;
    Buffer junction_buffer;
    Buffer underground_junction_buffer;
    Buffer tank_buffer;
    Buffer reservoir_buffer;
};

#endif // MAP_BGFX_GLOBE_NETWORK_H
