#ifndef MAP_BGFX_SURFACE_H
#define MAP_BGFX_SURFACE_H

#include "map/bgfx/map_bgfx_2d_scene.h"
#include "map/bgfx/map_bgfx_2d_gpu.h"
#include "map/bgfx/map_bgfx_2d_imagery_gpu.h"
#include "map/bgfx/map_bgfx_2d_symbology.h"
#include "map/bgfx/map_bgfx_hud.h"
#include "map/bgfx/map_bgfx_hud_canvas.h"
#include "map/bgfx/map_bgfx_hud_gpu.h"
#include "map/bgfx/map_bgfx_hud_text_cache.h"
#include "map/bgfx/map_bgfx_labels.h"
#include "map/bgfx/map_bgfx_pan_reuse.h"
#include "map/core/map_frame_pacer.h"
#include "map/render/map_camera_view_state.h"
#include "map/render/map_network_render_symbology.h"
#include "map/render/map_render_surface.h"
#include "map/render/map_retained_network_scene.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QSizeF>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>

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
// (crosshair, GPS, scale, downloads, view switch, fullscreen, map legend) and
// map labels (entity IDs / values, map_bgfx_labels.h).
// The map editor uses a second instance (Phase 2): hidden entities and the
// network screen translation for its move preview, and an overlay pass
// (setOverlayBuilder()) for its icons and interaction feedback. The globe is
// NOT yet implemented.
//
// One bgfx device per process: it belongs to the visible surface. Hiding a
// surface (tab switch) shuts bgfx down; showing one starts it again, so the
// Map Monitor and the Map Editor take turns on the same device (and, in the
// browser, the same canvas).
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
    // One line on where startup stands (initialization, frame loop, frames),
    // for the startup watchdog and failure messages.
    QString startupDiagnostics() const;
    void focusNavigationInput();

    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot) override;
    // Editor move preview: these entities are drawn transparent (colour
    // stream only, no geometry upload) while the overlay draws them moving.
    void setHiddenEntityUuids(const QSet<QUuid> &uuids) override;
    void setNodeDeclutteringEnabled(bool enabled) override;
    // Editor full-network move: the network (not the imagery) is drawn
    // shifted by this many logical pixels (camera offset, no upload).
    void setNetworkScreenTranslation(const QPointF &translation) override;
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

    // Planned native-window changes (fullscreen reparenting, desktop): shut
    // bgfx down while the old native surface still exists, restart once the
    // window is exposed again. Qt Wayland destroys the wl_surface on reparent
    // without a QPlatformSurfaceEvent, so this must not rely on events.
    void suspendForNativeWindowChange();
    void resumeAfterNativeWindowChange();

    // --- Native HUD (slices 6a, 6b) ---------------------------------------------
    // Called (queued, outside the native window's event dispatch) when a HUD
    // button or legend entry is clicked.
    void setHudActionHandler(std::function<void(const MapBgfxHud::Command &)> handler);
    void setHudGpsPosition(bool valid, double longitude_deg, double latitude_deg);
    void setHudDownloadActivity(int tiles_active, int tiles_queued,
                                int terrain_active, int terrain_queued);
    void setHudFullscreen(bool available, bool active);
    // Map legend content (map_bgfx_legend_content.h); visible=false hides it.
    void setHudLegend(const MapBgfxHud::Legend &legend);
    // Closes an open HUD popup (combo list, palette menu).
    void closeHudPopup();
    // The 2D/3D switch (shown by default; 3D is offered once the bgfx globe
    // exists).
    void setHudViewSwitchVisible(bool visible);

    // --- Map labels (map_label_settings.h) ----------------------------------------
    // Which labels are drawn; all off (default) costs nothing.
    void setLabelSettings(const MapLabelSettings &settings);
    // Canonical -> displayed value of the node / link colouring metric
    // (mapBgfxLabelValueFormat()); the values come with setSymbology().
    void setLabelValueFormats(const MapBgfxLabelValueFormat &node_format,
                              const MapBgfxLabelValueFormat &link_format);
    // Symbol boxes the labels are placed around and keep clear of, relative
    // to the anchor (logical px). Unset: the GPU network's own symbols (dots,
    // icons at the symbology's sizes). The editor sets its icon boxes (its
    // icons are drawn by the overlay, the GPU network only has dots).
    void setLabelSymbolRects(MapBgfxLabels::SymbolRect node_symbol,
                             MapBgfxLabels::SymbolRect link_symbol);

    // --- On-demand rendering ------------------------------------------------------
    // The surface presents a frame only when its camera or size changed, one
    // of its setters was called, or something animates (HUD busy bars or
    // tooltip delay, budgeted imagery uploads, deferred labels). Anything
    // else that changes what it draws - the editor overlay's content - calls
    // this.
    void requestRedraw();

    // --- Map editor (Phase 2) ---------------------------------------------------
    // Called every 2D frame after the network and before the HUD with the
    // frame's continuous camera; draws into its own canvas and atlas, which
    // the surface uploads and submits with the HUD program. Returns the
    // translation (logical px) to draw the canvas with: a builder may keep
    // the canvas of an earlier frame and only move it (pure pans,
    // map_bgfx_pan_reuse.h); QPointF() for a canvas built for this camera.
    using OverlayBuilder = std::function<QPointF(MapBgfxHudCanvas *canvas,
                                              MapBgfxHudTextCache *cache,
                                              const MapBgfx2d::Camera &camera,
                                              const QSizeF &viewport)>;
    void setOverlayBuilder(OverlayBuilder builder);
    // Desktop: keyboard focus stays on this widget instead of moving to the
    // embedded native window on a click (the editor canvas handles keys and
    // owns the editor's window shortcuts). nullptr: the native window takes
    // focus (Map Monitor).
    void setKeyboardFocusWidget(QWidget *widget);
    // Routes a mouse event (logical px of this widget) to the HUD first.
    // consumed: the event belongs to the HUD and must not reach navigation or
    // picking. cursor: the HUD element's pointer shape (Qt::ArrowCursor when
    // the HUD has no opinion). over_panel: hovering a blocking HUD panel or
    // popup (no map hover there).
    struct HudPointer
    {
        bool consumed = false;
        Qt::CursorShape cursor = Qt::ArrowCursor;
        bool over_panel = false;
    };
    HudPointer handleHudMouseEvent(QMouseEvent *event);
    // Wheel over a HUD panel scrolls an open combo list and never zooms the
    // map underneath; true when consumed. On desktop the native window's
    // wheel events are routed here internally; WASM routes MapWidget's.
    bool handleHudWheelEvent(QWheelEvent *event);
    // Escape closes an open HUD popup; true when consumed (same routing).
    bool handleHudKeyPress(QKeyEvent *event);
    // Map pointer shape: pointing hand over pickable entities and HUD
    // buttons, cross over legend ramps, Qt::ArrowCursor resets it. On
    // desktop the cursor must be set on the embedded native window; the
    // widget's own cursor is never visible there.
    void setMapCursor(Qt::CursorShape shape);

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
    // Palette / font changes restyle the HUD: present a frame.
    void changeEvent(QEvent *event) override;

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
    // First initialization after the first show (retried while another
    // visible surface still holds the device).
    void beginInitialization();
    // Draws the editor overlay (when a builder is set) for this frame.
    void renderOverlay(const QSizeF &logical, const MapBgfx2d::Camera &camera);
    // Map labels with the network's camera (the editor's full-network move
    // included); rebuilt only when the camera or the content changed.
    void renderLabels(const QSizeF &logical, const MapBgfx2d::Camera &network_camera,
                      const MapBgfx2dSymbology::Sizes &sizes);
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
    std::function<void(const MapBgfxHud::Command &)> hud_action_handler;
    QElapsedTimer hud_clock;
    // Editor overlay: builder, its atlas (icons with glows; larger than the
    // HUD's), draw list and bgfx objects.
    OverlayBuilder overlay_builder;
    MapBgfxHudTextCache overlay_text_cache{2048};
    MapBgfxHudCanvas overlay_canvas;
    MapBgfxHudGpu overlay_gpu;
    // Map labels: settings, value formats, optional symbol boxes, the
    // builder, its atlas (created on first use: 16 MiB) and bgfx objects.
    // The canvas is rebuilt only when label_build_key changes, labels were
    // deferred (rasterization budget) or a pan left label_pan's margin.
    MapLabelSettings label_settings;
    MapBgfxLabelValueFormat label_node_format;
    MapBgfxLabelValueFormat label_link_format;
    MapBgfxLabels::SymbolRect label_node_symbol;
    MapBgfxLabels::SymbolRect label_link_symbol;
    MapBgfxLabels labels;
    std::unique_ptr<MapBgfxHudTextCache> label_text_cache;
    MapBgfxHudCanvas label_canvas;
    MapBgfxHudGpu label_gpu;
    // Everything but the camera centre: pans reuse the canvas translated
    // (label_pan) while the key is unchanged.
    struct LabelBuildKey
    {
        double world_pixels = 0.0;
        double viewport_width = 0.0;
        double viewport_height = 0.0;
        double node_half_px = 0.0;
        double icon_size_px = 0.0;
        double device_pixel_ratio = 0.0;
        quint64 preparation_id = 0;
        quint64 atlas_generation = 0;
        bool operator==(const LabelBuildKey &other) const
        {
            return this->world_pixels == other.world_pixels
                && this->viewport_width == other.viewport_width
                && this->viewport_height == other.viewport_height
                && this->node_half_px == other.node_half_px
                && this->icon_size_px == other.icon_size_px
                && this->device_pixel_ratio == other.device_pixel_ratio
                && this->preparation_id == other.preparation_id
                && this->atlas_generation == other.atlas_generation;
        }
    };
    LabelBuildKey label_build_key;
    // Built camera / margin of label_canvas and its draw offset this frame.
    MapBgfxPanReuse label_pan;
    // Content changed (settings, values, symbols, hidden entities, font):
    // rebuild on the next frame.
    bool labels_dirty = true;
    // On-demand rendering (requestRedraw()): what the last presented frame
    // showed, and whether a frame is wanted regardless.
    bool redraw_requested = true;
    bool presented_valid = false;
    MapCameraViewState presented_camera;
    QSize presented_pixels;
    QSizeF presented_logical;
    // Tiles in the repository that did not fit into this frame's upload
    // budget: keep presenting until all are resident.
    bool imagery_uploads_pending = false;
    // The last pointer move involved the HUD (hover feedback to redraw).
    bool hud_pointer_involved = false;
    // Last setHudDownloadActivity() numbers.
    int hud_download_activity[4] = {0, 0, 0, 0};
    QPointer<QWidget> keyboard_focus_widget;
    // Editor move preview.
    QSet<QUuid> hidden_entity_uuids;
    QPointF network_screen_translation;
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
    // initializeBackend() found the device held by another visible surface.
    bool device_busy = false;
    quint64 frame_count = 0;
    // renderFrameAt() calls, including skipped ones (startupDiagnostics()).
    quint64 frame_callback_count = 0;
    // Current initialization step, reported when startup stalls.
    const char *startup_step = "not started";
#ifndef Q_OS_WASM
    QWindow *native_window = nullptr;
#endif
};

#endif // MAP_BGFX_SURFACE_H
