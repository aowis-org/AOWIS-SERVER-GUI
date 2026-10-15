#ifndef MAP_BGFX_SURFACE_H
#define MAP_BGFX_SURFACE_H

#include "map/bgfx/map_bgfx_2d_scene.h"
#include "map/bgfx/map_bgfx_2d_gpu.h"
#include "map/bgfx/map_bgfx_2d_imagery_gpu.h"
#include "map/bgfx/map_bgfx_hud.h"
#include "map/bgfx/map_bgfx_hud_canvas.h"
#include "map/bgfx/map_bgfx_hud_gpu.h"
#include "map/bgfx/map_bgfx_hud_text_cache.h"
#include "map/core/map_frame_pacer.h"
#include "map/render/map_camera_view_state.h"
#include "map/render/map_network_render_symbology.h"
#include "map/render/map_render_surface.h"
#include "map/render/map_retained_network_scene.h"

#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QSize>
#include <QSizeF>
#include <QTimer>
#include <QWidget>

#include <functional>

#ifdef Q_OS_WASM
#include <emscripten/html5.h>
#endif

class MapModel;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QWindow;

// Experimental production integration with continuous 2D projection and
// zoom-independent CPU staging: GPU network, imagery, heatmap, 2D picking
// (click selection and hover), selection/error highlights and the native HUD
// (crosshair, GPS, scale, downloads, view switch, fullscreen). The map legend
// HUD (slice 6b) and the globe are NOT yet implemented. The MapRenderSurface contract is preserved
// without making the application depend on the probe executable.
class MapBgfxSurface final : public QWidget, public MapRenderSurface
{
    Q_OBJECT
public:
    explicit MapBgfxSurface(MapModel *model, QWidget *parent = nullptr);
    ~MapBgfxSurface() override;

    QString graphicsApiName() const override;
    // 2D picking with the QRhi rules (map_bgfx_2d_picking.h) at the current
    // continuous camera. Globe picking arrives with the bgfx globe.
    MapRenderHit hitTest(const QPointF &screen_position) const override;
    bool terrainCoordinateAtScreen(const QPointF &, CoordinateWGS84 *, bool = true) override { return false; }
    bool cachedTerrainElevationAtCoordinate(const CoordinateWGS84 &, double *) const override { return false; }
    bool globeSurfaceRayHitAtScreen(const QPointF &, MapGlobeSurfaceHit *) const override { return false; }
    bool panGlobeByTerrainPixels(const QPoint &) override { return false; }

    // Keyboard events originate from the embedded QWindow on native desktop.
    // Forward them to the shared map-navigation owner; bgfx must not implement
    // a second camera controller or independently quantize zoom input.
    using NavigationKeyHandler = std::function<bool(QKeyEvent *)>;
    using NavigationWheelHandler = std::function<void(QWheelEvent *)>;
    using NavigationFrameHandler = std::function<void(double)>;
    void setNavigationHandlers(NavigationKeyHandler press,
                               NavigationKeyHandler release,
                               std::function<void()> cancel);
    // Wheel events on the embedded native window (desktop) go to the same
    // shared navigation owner as keys; they share one zoom motion.
    void setNavigationWheelHandler(NavigationWheelHandler wheel);
    // Called once per presented frame with the paced elapsed time, *before*
    // the camera is sampled. This surface is then the only navigation clock.
    void setNavigationFrameHandler(NavigationFrameHandler advance);
    // Mouse press/release/move on the embedded native window (desktop).
    // Returns true when consumed, like MapWidget::handleMouse*Event().
    using NavigationMouseHandler = std::function<bool(QMouseEvent *)>;
    void setNavigationMouseHandlers(NavigationMouseHandler press,
                                    NavigationMouseHandler release,
                                    NavigationMouseHandler move);
    // The window that actually has pointer focus over the map (desktop), so
    // a Wayland pointer lock can target its own wl_surface. nullptr on WASM.
    QWindow *nativeInputWindow() const;
    void focusNavigationInput();

    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot) override;
    void setHiddenEntityUuids(const QSet<QUuid> &) override {}
    void setNodeDeclutteringEnabled(bool enabled) override;
    void setNetworkScreenTranslation(const QPointF &) override {}
    void setSymbology(const MapNetworkRenderSymbology &symbology) override;
    void setVisualControlSettings(const NetworkSymbologySettings &settings) override;
    void setTileRepository(MapTileRepository *tile_repository) override;
    void setTerrainRepository(MapTerrainRepository *) override {}
    void setBackgroundOpacity(int opacity) override;
    void setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid) override;
    void setSimulationErrorEntities(const QHash<QUuid, InfrastructureEntity> &error_entities,
                                    const QSet<QUuid> &stale_entity_uuids) override;
    void setGlobe3dIconsEnabled(bool) override {}
    void setUndergroundMode(MapUndergroundMode mode) override { this->underground_mode = mode; }
    MapUndergroundMode undergroundMode() const override { return this->underground_mode; }
    void setTerrainWireframeVisible(bool) override {}
    void setMapTilesVisible(bool) override {}
    void globeTerrainMeshProgress(int *completed, int *total, bool *active) const override;

    // --- Native HUD (slice 6a) --------------------------------------------------
    // Called (queued, outside the native window's event dispatch) when a HUD
    // button is clicked.
    void setHudActionHandler(std::function<void(MapBgfxHud::Action)> handler);
    void setHudGpsPosition(bool valid, double longitude_deg, double latitude_deg);
    void setHudDownloadActivity(int tiles_active, int tiles_queued,
                                int terrain_active, int terrain_queued);
    void setHudFullscreen(bool available, bool active);
    // Routes a mouse event (logical px of this widget) to the HUD first.
    // consumed: the event belongs to the HUD and must not reach navigation or
    // picking. over_button: pointing-hand cursor. over_panel: hovering a
    // blocking HUD panel (no map hover there).
    struct HudPointer
    {
        bool consumed = false;
        bool over_button = false;
        bool over_panel = false;
    };
    HudPointer handleHudMouseEvent(QMouseEvent *event);
    // Pointing-hand cursor over pickable entities and HUD buttons. On desktop
    // the cursor must be set on the embedded native window; the widget's own
    // cursor is never visible there.
    void setHoverCursor(bool hovered);

signals:
    void signalRendererReady();
    void signalRendererFailed(const QString &reason);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    bool initializeBackend();
    void startFrameLoop();
    void stopFrameLoop();
    double refreshPeriodSeconds() const;
    void renderFrame();
    void renderFrameAt(double elapsed_seconds);
    // 2D imagery: layout from the continuous camera, budgeted uploads,
    // repository requests on tile-set changes, then draw.
    void renderImagery(const QSizeF &logical);
    void synchronizeGeometry();
    // Rebuilds the selection/error highlight meshes when the selection, the
    // errors, the prepared geometry or the junction visibility changed.
    void synchronizeHighlights();
    // Builds the HUD, uploads its atlas changes and submits it on top.
    void renderHud(const QSizeF &logical, const MapBgfx2d::Camera &camera);
    // bgfx's text statistics overlay (AOWIS_BGFX_DEBUG=1).
    void renderDebugOverlay();
    // Releases every bgfx object owned by this surface (before
    // bgfx::shutdown()) and marks all GPU state for re-creation.
    void releaseGpuResources();
    // bgfx::shutdown() for a native window that is about to be destroyed;
    // the backend restarts once the platform surface exists again.
    void shutdownBackend();
    void scheduleBackendRestart();
    QSize physicalSize() const;
    QSizeF logicalSize() const;
    void fail(const QString &reason);
#ifdef Q_OS_WASM
    static EM_BOOL animationFrameCallback(double time_ms, void *user_data);
#endif

    NavigationKeyHandler navigation_key_press;
    NavigationKeyHandler navigation_key_release;
    std::function<void()> navigation_cancel;
    NavigationWheelHandler navigation_wheel;
    NavigationFrameHandler navigation_frame;
    NavigationMouseHandler navigation_mouse_press;
    NavigationMouseHandler navigation_mouse_release;
    NavigationMouseHandler navigation_mouse_move;
    MapModel *map_model = nullptr;
    MapRetainedNetworkScene retained_scene;
    MapBgfx2dScene prepared_2d_scene;
    MapBgfx2dGpu gpu_2d_scene;
    MapBgfx2dGpu::FrameStats network_frame_stats;
    // Resolved colours/sizes; colours reach the GPU only when this changes.
    MapNetworkRenderSymbology symbology;
    bool symbology_colors_dirty = true;
    // Entity identity for picking and highlights (render ids are only unique
    // per entity type, see MapBgfx2dScene::entityKey()).
    QHash<quint64, QUuid> uuid_by_entity_key;
    QHash<QUuid, quint64> entity_key_by_uuid;
    InfrastructureEntity selected_entity_type = InfrastructureEntity::Unknown;
    QUuid selected_entity_uuid;
    QHash<QUuid, InfrastructureEntity> simulation_error_entities;
    QSet<QUuid> simulation_stale_entity_uuids;
    bool highlights_dirty = true;
    quint64 highlight_preparation_id = 0;
    bool highlight_show_junctions = true;
    MapBgfx2dImageryGpu imagery_gpu;
    MapTileRepository *tile_repository = nullptr;
    quint64 imagery_layout_key = 0;
    quint64 imagery_request_batch = 0;
    bool imagery_requests_dirty = true;
    int imagery_zoom = 0;
    int imagery_quad_count = 0;
    int background_opacity = 0;
    MapCameraViewState camera_state;
    // Native HUD: state/layout (CPU), its text/image atlas, the per-frame
    // draw list and the bgfx objects.
    MapBgfxHud hud;
    MapBgfxHudTextCache hud_text_cache;
    MapBgfxHudCanvas hud_canvas;
    MapBgfxHudGpu hud_gpu;
    std::function<void(MapBgfxHud::Action)> hud_action_handler;
    QElapsedTimer hud_clock;
    bool debug_overlay_enabled = false;
    // Desktop: one PreciseTimer slightly faster than the display; the
    // vsync-blocking bgfx::frame() then locks presentation to the refresh.
    // WASM: requestAnimationFrame (browser vsync), never a setTimeout timer.
    QTimer frame_timer;
    QElapsedTimer frame_clock;
    MapFramePacer frame_pacer;
    bool frame_loop_running = false;
    double last_frame_time_ms = -1.0;
    QSize last_physical_size;
    MapUndergroundMode underground_mode = MapUndergroundMode::XRay;
    bool initialized = false;
    bool initialization_attempted = false;
    bool failed = false;
    bool ready_reported = false;
    // The native window lost its platform surface while bgfx was running.
    bool reinitialize_on_surface = false;
    quint64 frame_count = 0;
#ifndef Q_OS_WASM
    QWindow *native_window = nullptr;
#endif
};

#endif // MAP_BGFX_SURFACE_H
