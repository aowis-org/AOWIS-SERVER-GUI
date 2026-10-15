#include "map/bgfx/map_bgfx_surface.h"

#include "map/bgfx/map_bgfx_backend.h"
#include "map/core/map_model.h"
#include "map/data/map_tile_repository.h"
#include "map/bgfx/map_bgfx_2d_picking.h"
#include "map/core/map_scale_distance.h"
#include "map/core/map_scale_renderer.h"
#include "config/unit_profile_manager.h"
#include "map/bgfx/map_bgfx_2d_symbology.h"
#include "map/render/map_icon_atlas.h"

#include <QDebug>
#include <QGuiApplication>
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QPlatformSurfaceEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#ifdef Q_OS_WASM
#include <emscripten/emscripten.h>

// Resolve the independent canvas within Qt's shadow-root screen, as tested
// by the existing bgfx probe. Emscripten also needs this alias for reset().
EM_JS(int, aowisBgfxMapPrepare, (), {
    const bridge = window.aowisBgfxMap;
    if (!bridge || !bridge.prepare() || !bridge.attach()
        || typeof specialHTMLTargets === 'undefined')
        return 0;
    specialHTMLTargets['!aowis-bgfx-map'] = bridge.canvas();
    return 1;
});
EM_JS(void, aowisBgfxMapGeometry, (int x, int y, int w, int h, int visible), {
    window.aowisBgfxMap?.geometry(x, y, w, h, Boolean(visible));
});
EM_JS(void, aowisBgfxMapRemove, (), {
    if (typeof specialHTMLTargets !== 'undefined')
        delete specialHTMLTargets['!aowis-bgfx-map'];
    window.aowisBgfxMap?.remove();
});
#else
#if defined(Q_OS_LINUX) && defined(AOWIS_BGFX_NATIVE_WAYLAND_PRIVATE)
#include <qpa/qplatformnativeinterface.h>
#endif
#endif

namespace {
constexpr uint16_t kMapBgfxView = 0;
// bgfx::init/bgfx::shutdown own a process-global renderer. Until multi-view
// ownership is introduced, only one experimental surface may initialize it.
bool g_bgfx_map_initialized = false;
// Tile texture uploads per frame (~256 KiB each). Enough to fill a screen
// within a few frames, small enough to never cost a frame while streaming.
constexpr int kImageryUploadsPerFrame = 8;
// VSYNC: presentation pacing (see MapFramePacer). MSAA x4: antialiased thick
// line and marker edges; bgfx clamps it to what the renderer supports.
constexpr uint32_t kMapBgfxResetFlags = BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X4;
// Fallback when the platform reports no usable refresh rate.
constexpr double kFallbackRefreshHz = 60.0;
#ifdef Q_OS_WASM
// requestAnimationFrame loops cannot be cancelled from C++. A loop is only
// valid while its generation matches; stop/restart/destruction bump it, so a
// stale callback returns EM_FALSE instead of touching a dead surface or
// running a second loop in parallel.
MapBgfxSurface *g_bgfx_frame_loop_surface = nullptr;
quintptr g_bgfx_frame_loop_generation = 0;
#endif
}

MapBgfxSurface::MapBgfxSurface(MapModel *model, QWidget *parent)
    : QWidget(parent), map_model(model)
{
    Q_ASSERT(this->map_model != nullptr);
    setObjectName(QStringLiteral("aowis-bgfx-monitor-surface"));
    this->camera_state = this->map_model->cameraViewState();
    // A provider switch reuses the same tile ids for different pixels.
    connect(this->map_model, &MapModel::providerChanged, this, [this](MapProvider)
    {
        if (this->initialized)
            this->imagery_gpu.clearTextures();
        this->imagery_layout_key = 0;
        this->imagery_requests_dirty = true;
    });
    // Native HUD (slice 6a): app font, the QWidget HUD's images, a scale bar in
    // the user's distance unit from the continuous camera.
    this->hud.setFont(font());
    this->hud.setImages(QImage(QStringLiteral(":/icon/crosshair.png")),
                        QImage(QStringLiteral(":/icon/fullscreen.png")),
                        QImage(QStringLiteral(":/icon/fullscreen_undo.png")));
    this->hud.setScaleProvider([](double lon0, double lat0, double lon1, double lat1,
                                  double bar_px)
    {
        MapBgfxHud::ScaleBar result;
        CoordinateWGS84 start;
        start.longitude_deg = lon0;
        start.latitude_deg = lat0;
        CoordinateWGS84 end;
        end.longitude_deg = lon1;
        end.latitude_deg = lat1;
        const double maximum_distance_m = MapScaleRenderer::Detail::distanceMeters(start, end);
        const MapScaleDistance::Scale scale = MapScaleDistance::forMaximumDistance(
            maximum_distance_m, UnitProfileManager::instance().presentation());
        if (!(maximum_distance_m > 0.0) || !(scale.distance_m > 0.0)
            || !std::isfinite(maximum_distance_m))
            return result;
        result.valid = true;
        result.width_px = bar_px * scale.distance_m / maximum_distance_m;
        result.label = scale.label;
        return result;
    });
    this->hud_clock.start();
    this->debug_overlay_enabled = qEnvironmentVariableIntValue("AOWIS_BGFX_DEBUG") != 0;
    this->frame_timer.setTimerType(Qt::PreciseTimer);
    connect(&this->frame_timer, &QTimer::timeout, this, &MapBgfxSurface::renderFrame);

#ifndef Q_OS_WASM
    setFocusPolicy(Qt::StrongFocus);
    this->native_window = new QWindow();
    // createWindowContainer embeds a separate native QWindow. Its key events
    // are not delivered to MapWidget once keyboard focus moves to bgfx.
    this->native_window->installEventFilter(this);
    // The surface type only decides what Qt prepares for the native view.
    // macOS: bgfx selects Metal there, which needs a CAMetalLayer-backed
    // NSView. Elsewhere one HWND / X11 window / wl_surface serves OpenGL,
    // Vulkan and Direct3D alike.
#ifdef Q_OS_MACOS
    this->native_window->setSurfaceType(QSurface::MetalSurface);
#else
    this->native_window->setSurfaceType(QSurface::OpenGLSurface);
#endif
    QWidget *container = QWidget::createWindowContainer(this->native_window, this);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(container);
#endif
    // The frame timer samples the continuous camera, NOT integer tile zoom.
    // Signal connections are deliberately unnecessary: a camera move causes
    // no scene changes or uploads in this early backend.
}

MapBgfxSurface::~MapBgfxSurface()
{
    stopFrameLoop();
#ifdef Q_OS_WASM
    if (g_bgfx_frame_loop_surface == this)
        g_bgfx_frame_loop_surface = nullptr;
#endif
    if (this->initialized) {
        releaseGpuResources();
        bgfx::shutdown();
        g_bgfx_map_initialized = false;
    }
#ifdef Q_OS_WASM
    aowisBgfxMapRemove();
#endif
}

void MapBgfxSurface::setNavigationHandlers(NavigationKeyHandler press,
                                           NavigationKeyHandler release,
                                           std::function<void()> cancel)
{
    this->navigation_key_press = std::move(press);
    this->navigation_key_release = std::move(release);
    this->navigation_cancel = std::move(cancel);
}

void MapBgfxSurface::setNavigationWheelHandler(NavigationWheelHandler wheel)
{
    this->navigation_wheel = std::move(wheel);
}

void MapBgfxSurface::setNavigationFrameHandler(NavigationFrameHandler advance)
{
    this->navigation_frame = std::move(advance);
}

void MapBgfxSurface::focusNavigationInput()
{
#ifndef Q_OS_WASM
    // The embedded native window owns key focus on desktop. QWidget focus on
    // the hidden MapWidget cannot be used as a substitute.
    setFocus(Qt::ShortcutFocusReason);
    if (this->native_window != nullptr)
        this->native_window->requestActivate();
#endif
}

bool MapBgfxSurface::eventFilter(QObject *watched, QEvent *event)
{
#ifndef Q_OS_WASM
    // Reparenting the map (fullscreen) can destroy and recreate the native
    // window: bgfx must let go of the old surface first and start again on
    // the new one, or it would present into a dead window handle.
    if (watched == this->native_window && event->type() == QEvent::PlatformSurface)
    {
        const QPlatformSurfaceEvent *surface_event = static_cast<QPlatformSurfaceEvent *>(event);
        if (surface_event->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
        {
            shutdownBackend();
        }
        else if (surface_event->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated
                 && this->reinitialize_on_surface)
        {
            scheduleBackendRestart();
        }
    }
    if (watched == this->native_window && this->initialized)
    {
        if (event->type() == QEvent::KeyPress && this->navigation_key_press)
            return this->navigation_key_press(static_cast<QKeyEvent *>(event));
        if (event->type() == QEvent::KeyRelease && this->navigation_key_release)
            return this->navigation_key_release(static_cast<QKeyEvent *>(event));
        // The native window fills this surface, which has the same geometry
        // as MapWidget in the stack, so the position is a valid anchor as-is.
        if (event->type() == QEvent::Wheel && this->navigation_wheel)
        {
            this->navigation_wheel(static_cast<QWheelEvent *>(event));
            return true;
        }
        // Mouse: same coordinates as MapWidget for the same reason. A QWindow
        // receives every move (no mouse-tracking flag), and it receives the
        // moves/release of a drag that started on it (implicit grab).
        // Double clicks are forwarded unchanged (type DblClick) through the
        // press handler, so the receiver sees exactly the press + double-click
        // sequence Qt delivers to an ordinary QWidget.
        if ((event->type() == QEvent::MouseButtonPress
             || event->type() == QEvent::MouseButtonDblClick)
            && this->navigation_mouse_press)
        {
            // A click must also move keyboard focus to the embedded window,
            // otherwise zoom/pan keys keep going to the previous widget.
            this->native_window->requestActivate();
            return this->navigation_mouse_press(static_cast<QMouseEvent *>(event));
        }
        if (event->type() == QEvent::MouseButtonRelease && this->navigation_mouse_release)
            return this->navigation_mouse_release(static_cast<QMouseEvent *>(event));
        if (event->type() == QEvent::MouseMove && this->navigation_mouse_move)
            return this->navigation_mouse_move(static_cast<QMouseEvent *>(event));
        if (event->type() == QEvent::FocusOut && this->navigation_cancel)
            this->navigation_cancel();
        // The pointer left the map: no HUD button stays hover-highlighted.
        if (event->type() == QEvent::Leave)
            this->hud.clearHover();
    }
#else
    Q_UNUSED(watched)
    Q_UNUSED(event)
#endif
    return QWidget::eventFilter(watched, event);
}

void MapBgfxSurface::keyPressEvent(QKeyEvent *event)
{
    if (this->initialized && this->navigation_key_press
        && this->navigation_key_press(event))
        return;
    QWidget::keyPressEvent(event);
}

void MapBgfxSurface::keyReleaseEvent(QKeyEvent *event)
{
    if (this->initialized && this->navigation_key_release
        && this->navigation_key_release(event))
        return;
    QWidget::keyReleaseEvent(event);
}

void MapBgfxSurface::focusOutEvent(QFocusEvent *event)
{
    if (this->navigation_cancel)
        this->navigation_cancel();
    QWidget::focusOutEvent(event);
}

QString MapBgfxSurface::graphicsApiName() const
{
    if (!this->initialized)
        return QStringLiteral("bgfx (not initialized)");
#ifdef Q_OS_WASM
    return QStringLiteral("bgfx / WebGL 2 (experimental)");
#else
    return QStringLiteral("bgfx / %1 (experimental)")
        .arg(QString::fromLatin1(bgfx::getRendererName(bgfx::getRendererType())));
#endif
}

void MapBgfxSurface::setNavigationMouseHandlers(NavigationMouseHandler press,
                                                NavigationMouseHandler release,
                                                NavigationMouseHandler move)
{
    this->navigation_mouse_press = std::move(press);
    this->navigation_mouse_release = std::move(release);
    this->navigation_mouse_move = std::move(move);
}

QWindow *MapBgfxSurface::nativeInputWindow() const
{
#ifdef Q_OS_WASM
    return nullptr;
#else
    return this->native_window;
#endif
}

void MapBgfxSurface::globeTerrainMeshProgress(int *completed, int *total,
                                             bool *active) const
{
    if (completed != nullptr)
        *completed = 0;
    if (total != nullptr)
        *total = 0;
    if (active != nullptr)
        *active = false;
}

void MapBgfxSurface::setNetworkSnapshot(const NetworkRenderSnapshot &snapshot)
{
    // Retained geometry revisions change only with model changes, not zoom.
    const MapRetainedNetworkUpdate update = this->retained_scene.setSnapshot(snapshot);
    if (update.geometry_changed)
    {
        // Identity for picking (hit -> uuid) and highlights (uuid -> entity).
        this->uuid_by_entity_key.clear();
        this->entity_key_by_uuid.clear();
        const NetworkRenderSnapshot &retained = this->retained_scene.snapshot();
        for (const NetworkRenderNode &node : retained.nodes)
        {
            const quint64 key = MapBgfx2dScene::entityKey(node.entity_type, node.render_id);
            this->uuid_by_entity_key.insert(key, node.uuid);
            this->entity_key_by_uuid.insert(node.uuid, key);
        }
        for (const NetworkRenderLink &link : retained.links)
        {
            const quint64 key = MapBgfx2dScene::entityKey(link.entity_type, link.render_id);
            this->uuid_by_entity_key.insert(key, link.uuid);
            this->entity_key_by_uuid.insert(link.uuid, key);
        }
        this->highlights_dirty = true;
    }
    if (update.geometry_changed && this->prepared_2d_scene.prepare(this->retained_scene))
    {
        qInfo() << "bgfx retained geometry prepared" << snapshot.geometry_revision
                << "links" << this->prepared_2d_scene.linkPolylines().size()
                << "link segments" << this->prepared_2d_scene.linkSegmentCount()
                // Demand points are snapshot nodes (entity DemandPoint).
                << "nodes incl. demand points" << this->prepared_2d_scene.networkNodes().size()
                << "attachments" << this->prepared_2d_scene.attachments().size();
    }
}

void MapBgfxSurface::setNodeDeclutteringEnabled(bool enabled)
{
    // Positions change without a geometry revision: re-prepare now; the GPU
    // mesh and the highlights follow MapBgfx2dScene::preparationId().
    this->prepared_2d_scene.setDeclutteringEnabled(enabled);
    if (this->retained_scene.hasSnapshot())
        this->prepared_2d_scene.prepare(this->retained_scene);
}

void MapBgfxSurface::setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (this->selected_entity_type == entity_type && this->selected_entity_uuid == uuid)
        return;
    this->selected_entity_type = entity_type;
    this->selected_entity_uuid = uuid;
    this->highlights_dirty = true;
}

void MapBgfxSurface::setSimulationErrorEntities(
    const QHash<QUuid, InfrastructureEntity> &error_entities,
    const QSet<QUuid> &stale_entity_uuids)
{
    if (this->simulation_error_entities == error_entities
        && this->simulation_stale_entity_uuids == stale_entity_uuids)
        return;
    this->simulation_error_entities = error_entities;
    this->simulation_stale_entity_uuids = stale_entity_uuids;
    this->highlights_dirty = true;
}

MapRenderHit MapBgfxSurface::hitTest(const QPointF &screen_position) const
{
    MapRenderHit no_hit;
    if (this->map_model == nullptr || !this->prepared_2d_scene.isPrepared())
        return no_hit;
    const QSizeF logical = logicalSize();
    if (!std::isfinite(screen_position.x()) || !std::isfinite(screen_position.y())
        || screen_position.x() < 0.0 || screen_position.y() < 0.0
        || screen_position.x() > logical.width() || screen_position.y() > logical.height())
        return no_hit;
    // The model's current camera, not the last presented frame: a click
    // right after a zoom step must hit what the next frame shows.
    const MapCameraViewState state = this->map_model->cameraViewState();
    if (state.mode != MapViewMode::TwoD)
        return no_hit;
    const MapBgfx2d::Camera camera = MapBgfx2d::cameraFor(
        state.center_lon_deg, state.center_lat_deg, state.continuous_2d_zoom,
        logical.width(), logical.height());
    const MapBgfx2dSymbology::Sizes sizes = MapBgfx2dSymbology::sizes(
        this->symbology, state.center_lat_deg, state.continuous_2d_zoom);
    MapBgfx2dPicking::Options options;
    options.icon_size_px = sizes.icon_size_px;
    options.node_half_px = sizes.node_half_px;
    options.link_width_px = sizes.link_width_px;
    options.show_icons = this->symbology.show_icons;
    options.show_junctions = this->symbology.show_junctions;
    options.has_icon = [](InfrastructureEntity entity_type) { return mapHasIcon(entity_type); };
    const MapBgfx2dPicking::Hit hit = MapBgfx2dPicking::pick(
        this->prepared_2d_scene, camera, {screen_position.x(), screen_position.y()}, options);
    if (!hit.isValid())
        return no_hit;
    MapRenderHit result;
    result.render_id = hit.render_id;
    result.entity_type = hit.entity_type;
    result.uuid = this->uuid_by_entity_key.value(
        MapBgfx2dScene::entityKey(hit.entity_type, hit.render_id));
    return result;
}

void MapBgfxSurface::synchronizeHighlights()
{
    if (!this->prepared_2d_scene.isPrepared())
        return;
    if (!this->highlights_dirty
        && this->highlight_preparation_id == this->prepared_2d_scene.preparationId()
        && this->highlight_show_junctions == this->symbology.show_junctions)
        return;

    // A uuid only counts when it still resolves to an entity of that type.
    const std::function<bool(const QUuid &, InfrastructureEntity, quint64 *)> resolve =
        [this](const QUuid &uuid, InfrastructureEntity entity_type, quint64 *key)
    {
        if (uuid.isNull() || entity_type == InfrastructureEntity::Unknown)
            return false;
        const QHash<QUuid, quint64>::const_iterator found = this->entity_key_by_uuid.constFind(uuid);
        if (found == this->entity_key_by_uuid.cend()
            || found.value() != MapBgfx2dScene::entityKey(entity_type, quint32(found.value())))
            return false;
        *key = found.value();
        return true;
    };

    // QRhi colours: selection (0, 190, 255); errors red, stale results grey.
    const quint32 selected_abgr = mapBgfx2dAbgrFromArgb(quint32(qRgb(0, 190, 255)));
    const quint32 error_abgr = mapBgfx2dAbgrFromArgb(quint32(qRgb(255, 0, 0)));
    const quint32 stale_abgr = mapBgfx2dAbgrFromArgb(quint32(qRgb(128, 128, 128)));

    QSet<quint64> selected_keys;
    quint64 selected_key = 0;
    const bool selected_has_error =
        resolve(this->selected_entity_uuid, this->selected_entity_type, &selected_key)
        && this->simulation_error_entities.value(this->selected_entity_uuid,
                                                 InfrastructureEntity::Unknown)
            == this->selected_entity_type;
    if (selected_key != 0)
        selected_keys.insert(selected_key);

    QSet<quint64> diagnostic_keys;
    // Links reach the colour lookup without their entity type (see
    // MapBgfx2dMesh::buildColors()), nodes with it: keep both maps.
    QHash<quint64, quint32> diagnostic_node_colors;
    QHash<quint32, quint32> diagnostic_link_colors;
    for (QHash<QUuid, InfrastructureEntity>::const_iterator error =
             this->simulation_error_entities.cbegin();
         error != this->simulation_error_entities.cend(); ++error)
    {
        quint64 key = 0;
        if (!resolve(error.key(), error.value(), &key))
            continue;
        const quint32 color = this->simulation_stale_entity_uuids.contains(error.key())
            ? stale_abgr : error_abgr;
        diagnostic_keys.insert(key);
        diagnostic_node_colors.insert(key, color);
        diagnostic_link_colors.insert(quint32(key), color);
    }

    const bool show_junctions = this->symbology.show_junctions;
    // Hidden junctions get no node highlight (QRhi appendEntityHighlight()).
    const MapBgfx2dMesh::ColorLookup selected_lookup =
        [selected_abgr, show_junctions](MapBgfx2dPrimitive primitive, quint32,
                                        InfrastructureEntity entity_type)
    {
        if (primitive == MapBgfx2dPrimitive::Node
            && entity_type == InfrastructureEntity::Junction && !show_junctions)
            return 0u;
        return selected_abgr;
    };
    const MapBgfx2dMesh::ColorLookup diagnostic_lookup =
        [&diagnostic_node_colors, &diagnostic_link_colors, show_junctions](
            MapBgfx2dPrimitive primitive, quint32 render_id, InfrastructureEntity entity_type)
    {
        if (primitive == MapBgfx2dPrimitive::Link)
            return diagnostic_link_colors.value(render_id, 0u);
        if (entity_type == InfrastructureEntity::Junction && !show_junctions)
            return 0u;
        return diagnostic_node_colors.value(MapBgfx2dScene::entityKey(entity_type, render_id), 0u);
    };

    if (!this->gpu_2d_scene.setHighlights(
            this->prepared_2d_scene.filtered(selected_keys), selected_lookup, selected_has_error,
            this->prepared_2d_scene.filtered(diagnostic_keys), diagnostic_lookup))
        qWarning() << "bgfx selection/error highlight upload failed";
    this->highlights_dirty = false;
    this->highlight_preparation_id = this->prepared_2d_scene.preparationId();
    this->highlight_show_junctions = show_junctions;
}

void MapBgfxSurface::releaseGpuResources()
{
    this->imagery_gpu.release();
    this->gpu_2d_scene.release();
    this->hud_gpu.release();
    // Everything staged on the GPU is gone with the device.
    this->highlights_dirty = true;
    this->imagery_layout_key = 0;
    this->imagery_requests_dirty = true;
    this->last_physical_size = QSize();
}

void MapBgfxSurface::shutdownBackend()
{
    if (!this->initialized)
        return;
    stopFrameLoop();
    releaseGpuResources();
    bgfx::shutdown();
    this->initialized = false;
    g_bgfx_map_initialized = false;
    this->reinitialize_on_surface = true;
}

void MapBgfxSurface::scheduleBackendRestart()
{
    // Queued: the platform window must be fully set up, and the widget may
    // become visible only after the surface was created (or the other way
    // round); both paths end here and the first one that can start wins.
    QTimer::singleShot(0, this, [this]()
    {
        if (!this->reinitialize_on_surface || this->initialized || this->failed || !isVisible())
            return;
#ifndef Q_OS_WASM
        if (this->native_window == nullptr || this->native_window->handle() == nullptr)
            return;
#endif
        this->reinitialize_on_surface = false;
        if (!initializeBackend())
            fail(QStringLiteral("bgfx could not restart on the recreated map window"));
        else
            startFrameLoop();
    });
}

void MapBgfxSurface::setHudActionHandler(std::function<void(MapBgfxHud::Action)> handler)
{
    this->hud_action_handler = std::move(handler);
}

void MapBgfxSurface::setHudGpsPosition(bool valid, double longitude_deg, double latitude_deg)
{
    this->hud.setGps(valid, longitude_deg, latitude_deg);
}

void MapBgfxSurface::setHudDownloadActivity(int tiles_active, int tiles_queued,
                                            int terrain_active, int terrain_queued)
{
    this->hud.setDownloadActivity(tiles_active, tiles_queued, terrain_active, terrain_queued);
}

void MapBgfxSurface::setHudFullscreen(bool available, bool active)
{
    this->hud.setFullscreen(available, active);
}

MapBgfxSurface::HudPointer MapBgfxSurface::handleHudMouseEvent(QMouseEvent *event)
{
    HudPointer result;
    if (event == nullptr || !this->initialized)
        return result;
    const QPointF position = event->position();
    result.over_panel = this->hud.blocksInput(position.x(), position.y());
    switch (event->type())
    {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
            result.consumed = event->button() == Qt::LeftButton
                ? this->hud.mousePress(position.x(), position.y())
                : result.over_panel;
            break;
        case QEvent::MouseButtonRelease:
            if (event->button() == Qt::LeftButton)
            {
                const MapBgfxHud::Action action =
                    this->hud.mouseRelease(position.x(), position.y(), &result.consumed);
                if (action != MapBgfxHud::Action::None && this->hud_action_handler)
                {
                    // Deferred: an action may reparent this widget (fullscreen),
                    // which must not happen inside the native window's event.
                    const std::function<void(MapBgfxHud::Action)> handler =
                        this->hud_action_handler;
                    QTimer::singleShot(0, this, [handler, action]() { handler(action); });
                }
            }
            else
            {
                result.consumed = result.over_panel;
            }
            break;
        case QEvent::MouseMove:
            result.consumed = this->hud.mouseMove(position.x(), position.y(), &result.over_button);
            break;
        default:
            break;
    }
    return result;
}

void MapBgfxSurface::setHoverCursor(bool hovered)
{
#ifndef Q_OS_WASM
    // The embedded native window has its own cursor; MapWidget's cursor is
    // never shown over it.
    if (this->native_window == nullptr)
        return;
    if (hovered)
        this->native_window->setCursor(Qt::PointingHandCursor);
    else
        this->native_window->unsetCursor();
#else
    if (hovered)
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
#endif
}

void MapBgfxSurface::renderHud(const QSizeF &logical, const MapBgfx2d::Camera &camera)
{
    if (!this->hud_gpu.ready())
        return;
#ifdef Q_OS_WASM
    const qreal ratio = devicePixelRatioF();
    const QScreen *hud_screen = screen();
#else
    const qreal ratio = this->native_window->devicePixelRatio();
    const QScreen *hud_screen = this->native_window->screen();
#endif
    this->hud_text_cache.setDevicePixelRatio(ratio);
    if (hud_screen != nullptr)
        this->hud_text_cache.setLogicalDpi(hud_screen->logicalDotsPerInch());
    // 3D is offered once the bgfx globe exists (Phase 3).
    this->hud.setViewMode(this->camera_state.mode == MapViewMode::TwoD, false);
    MapBgfxHud::FrameInputs inputs;
    inputs.viewport = logical;
    inputs.two_d = this->camera_state.mode == MapViewMode::TwoD;
    inputs.camera = camera;
    inputs.time_s = double(this->hud_clock.elapsed()) / 1000.0;
    this->hud.build(&this->hud_canvas, &this->hud_text_cache, inputs,
                    MapBgfxHud::Palette::fromQPalette(palette()));
    this->hud_gpu.uploadAtlas(&this->hud_text_cache);
    this->hud_gpu.draw(kMapBgfxView, this->hud_canvas, logical);
}

void MapBgfxSurface::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (this->initialized) {
        synchronizeGeometry();
        startFrameLoop();
        return;
    }
    if (this->reinitialize_on_surface)
    {
        scheduleBackendRestart();
        return;
    }
    if (this->failed || this->initialization_attempted)
        return;
    // Native platform window and Qt's WASM shadow-root must exist first.
    QTimer::singleShot(0, this, [this]() {
        if (!isVisible() || this->failed || this->initialization_attempted)
            return;
        this->initialization_attempted = true;
        if (!initializeBackend())
            fail(QStringLiteral("bgfx initialization failed; original map renderer retained"));
        else
            startFrameLoop();
    });
}

void MapBgfxSurface::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    stopFrameLoop();
#ifdef Q_OS_WASM
    aowisBgfxMapGeometry(0, 0, 0, 0, 0);
#endif
}

void MapBgfxSurface::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    synchronizeGeometry();
}

QSize MapBgfxSurface::physicalSize() const
{
#ifdef Q_OS_WASM
    const QWidget *viewport = this;
#else
    const QWindow *viewport = this->native_window;
#endif
    const qreal ratio = viewport->devicePixelRatio();
    return QSize(std::max(1, qRound(viewport->width() * ratio)),
                 std::max(1, qRound(viewport->height() * ratio)));
}

// The camera works in logical (device-independent) pixels like MapModel,
// MapWidget, the scale bar and the CPU renderer: one 2D zoom level means
// 256 * 2^zoom *logical* pixels per world. Passing physical pixels made the
// bgfx map one octave too small at devicePixelRatio 2 (and fixed-pixel
// markers half-size). The clip transform only needs the ratio, so the
// physical framebuffer size is still used for the view rectangle.
QSizeF MapBgfxSurface::logicalSize() const
{
#ifdef Q_OS_WASM
    const QWidget *viewport = this;
#else
    const QWindow *viewport = this->native_window;
#endif
    return QSizeF(std::max(1, viewport->width()), std::max(1, viewport->height()));
}

double MapBgfxSurface::refreshPeriodSeconds() const
{
#ifdef Q_OS_WASM
    // Unknown in the browser; MapFramePacer estimates it from rAF stamps.
    return 0.0;
#else
    const QScreen *screen = this->native_window != nullptr
        ? this->native_window->screen() : nullptr;
    const double rate = screen != nullptr ? double(screen->refreshRate()) : 0.0;
    if (!std::isfinite(rate) || rate < 20.0 || rate > 500.0)
        return 0.0;
    return 1.0 / rate;
#endif
}

void MapBgfxSurface::startFrameLoop()
{
    if (this->frame_loop_running || !this->initialized || this->failed)
        return;
    this->frame_loop_running = true;
    this->frame_pacer.reset();
    this->frame_clock.invalidate();
    this->last_frame_time_ms = -1.0;
#ifdef Q_OS_WASM
    g_bgfx_frame_loop_surface = this;
    ++g_bgfx_frame_loop_generation;
    emscripten_request_animation_frame_loop(
        &MapBgfxSurface::animationFrameCallback,
        reinterpret_cast<void *>(g_bgfx_frame_loop_generation));
#else
    // Fire slightly faster than the display: bgfx::frame() blocks in the
    // vsync swap, so every tick presents exactly one refresh. A timer
    // *slower* than the display (e.g. 17 ms at 59.94 Hz) would instead
    // repeat a frame every ~1 s. One timer only -- see MapWidget.
    const double period_s = refreshPeriodSeconds() > 0.0
        ? refreshPeriodSeconds() : 1.0 / kFallbackRefreshHz;
    this->frame_timer.setInterval(std::max(1, int(std::floor(period_s * 1000.0)) - 1));
    this->frame_timer.start();
#endif
}

void MapBgfxSurface::stopFrameLoop()
{
    this->frame_loop_running = false;
    this->frame_timer.stop();
#ifdef Q_OS_WASM
    ++g_bgfx_frame_loop_generation;
#endif
}

#ifdef Q_OS_WASM
EM_BOOL MapBgfxSurface::animationFrameCallback(double time_ms, void *user_data)
{
    MapBgfxSurface *surface = g_bgfx_frame_loop_surface;
    if (surface == nullptr || !surface->frame_loop_running
        || reinterpret_cast<quintptr>(user_data) != g_bgfx_frame_loop_generation)
        return EM_FALSE;

    // rAF timestamps are the browser's frame (vsync) times: they are the
    // presentation clock, unlike Qt's setTimeout-based QTimer on WASM.
    double elapsed_seconds = 0.0;
    if (surface->last_frame_time_ms >= 0.0)
        elapsed_seconds = surface->frame_pacer.advance(
            (time_ms - surface->last_frame_time_ms) / 1000.0, 0.0);
    surface->last_frame_time_ms = time_ms;
    surface->renderFrameAt(elapsed_seconds);
    return surface->frame_loop_running ? EM_TRUE : EM_FALSE;
}
#endif

void MapBgfxSurface::synchronizeGeometry()
{
#ifdef Q_OS_WASM
    const QPoint position = mapToGlobal(QPoint(0, 0));
    aowisBgfxMapGeometry(position.x(), position.y(), width(), height(),
                         isVisible() && this->initialized ? 1 : 0);
#endif
    if (!this->initialized)
        return;
    const QSize pixels = physicalSize();
    if (pixels == this->last_physical_size)
        return;
    this->last_physical_size = pixels;
    bgfx::SwapChain swap_chain{};
    swap_chain.width = static_cast<uint32_t>(pixels.width());
    swap_chain.height = static_cast<uint32_t>(pixels.height());
    bgfx::reset(kMapBgfxResetFlags, &swap_chain);
}

bool MapBgfxSurface::initializeBackend()
{
    if (g_bgfx_map_initialized) {
        qWarning() << "A bgfx map surface is already active";
        return false;
    }
    bgfx::Init settings{};
#ifdef Q_OS_WASM
    if (!aowisBgfxMapPrepare())
        return false;
    settings.swapChain.nwh = const_cast<char *>("!aowis-bgfx-map");
#else
    settings.swapChain.nwh = reinterpret_cast<void *>(this->native_window->winId());
#if defined(Q_OS_LINUX)
    // The native handles are renderer-independent: OpenGL (EGL/GLX) and
    // Vulkan (VK_KHR_wayland/xlib surface) both consume display + surface.
    const QString platform = QGuiApplication::platformName();
    if (platform.startsWith(QStringLiteral("wayland"))) {
#if defined(AOWIS_BGFX_NATIVE_WAYLAND_PRIVATE)
        QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
        settings.swapChain.ndt = native
            ? native->nativeResourceForIntegration(QByteArrayLiteral("display")) : nullptr;
        settings.platformData.type = bgfx::NativeWindowHandleType::Wayland;
        settings.swapChain.nwh = native
            ? native->nativeResourceForWindow(QByteArrayLiteral("surface"), this->native_window)
            : nullptr;
#else
        qWarning() << "bgfx monitor requires Qt6::GuiPrivate for Wayland";
        return false;
#endif
    } else if (platform == QStringLiteral("xcb")) {
#if defined(AOWIS_BGFX_NATIVE_WAYLAND_PRIVATE)
        QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
        settings.swapChain.ndt = native
            ? native->nativeResourceForIntegration(QByteArrayLiteral("display")) : nullptr;
#else
        qWarning() << "bgfx monitor requires Qt6::GuiPrivate for X11";
        return false;
#endif
    }
#endif
    if (settings.swapChain.nwh == nullptr)
        return false;
#endif
    const QSize pixels = physicalSize();
    settings.swapChain.width = static_cast<uint32_t>(pixels.width());
    settings.swapChain.height = static_cast<uint32_t>(pixels.height());
    settings.reset = kMapBgfxResetFlags;

    // Renderer choice is delegated to bgfx (RendererType::Count ranks the
    // platform's renderers and falls through when one fails to initialise).
    // AOWIS only adds two things on top: an optional diagnostic override, and
    // a retry when bgfx picks a renderer for which this build embeds no
    // shaders (e.g. D3D shaders need a Windows-hosted shaderc).
    QVector<bgfx::RendererType::Enum> attempts;
#ifdef Q_OS_WASM
    // WebGL 2 is the only renderer compiled for the browser build.
    attempts.append(bgfx::RendererType::OpenGLES);
#else
    const bgfx::RendererType::Enum requested = MapBgfxBackend::requestedRenderer();
    if (requested != bgfx::RendererType::Count)
        attempts.append(requested);
    attempts.append(bgfx::RendererType::Count);
    const QVector<bgfx::RendererType::Enum> usable = MapBgfxBackend::usableRenderers();
    for (const bgfx::RendererType::Enum renderer : usable)
    {
        if (!attempts.contains(renderer))
            attempts.append(renderer);
    }
#endif

    for (const bgfx::RendererType::Enum attempt : attempts)
    {
        settings.type = attempt;
        if (!bgfx::init(settings))
        {
            qWarning() << "bgfx init failed for"
                       << (attempt == bgfx::RendererType::Count
                           ? "automatic selection" : bgfx::getRendererName(attempt));
            continue;
        }
        const bgfx::RendererType::Enum selected = bgfx::getRendererType();
        if (!MapBgfxBackend::shadersAvailable(selected))
        {
            qWarning() << "bgfx selected" << bgfx::getRendererName(selected)
                       << "but this build has no shaders for it; trying the next renderer";
            bgfx::shutdown();
            continue;
        }
        this->initialized = true;
        g_bgfx_map_initialized = true;
        if (!this->gpu_2d_scene.initialize(mapIconAtlasImage()) || !this->imagery_gpu.initialize()
            || !this->hud_gpu.initialize())
        {
            qWarning() << "bgfx map shader initialization failed on"
                       << bgfx::getRendererName(selected);
            releaseGpuResources();
            bgfx::shutdown();
            this->initialized = false;
            g_bgfx_map_initialized = false;
            continue;
        }
        qInfo() << "Map Monitor renderer:" << graphicsApiName();
        // bgfx's text overlay covers the HUD; diagnostics only on request.
        bgfx::setDebug(this->debug_overlay_enabled ? BGFX_DEBUG_TEXT : BGFX_DEBUG_NONE);
        synchronizeGeometry();
        return true;
    }
    return false;
}

void MapBgfxSurface::renderFrame()
{
    // Desktop timer tick. Measure from the start of the previous tick; with
    // the blocking vsync swap that is one refresh +- dispatch jitter, which
    // the pacer turns into exactly one refresh period.
    //
    // Never present into an unexposed window (minimized, other virtual
    // desktop, fully covered where the platform reports it). With
    // BGFX_RESET_VSYNC the swap can block until the compositor's next frame
    // callback, which a hidden Wayland surface may never receive; that would
    // freeze the GUI thread. The next exposed frame restarts with dt = 0.
#ifndef Q_OS_WASM
    if (this->native_window != nullptr && !this->native_window->isExposed())
    {
        this->frame_clock.invalidate();
        this->frame_pacer.reset();
        return;
    }
#endif
    double elapsed_seconds = 0.0;
    if (this->frame_clock.isValid())
    {
        elapsed_seconds = this->frame_pacer.advance(
            double(this->frame_clock.nsecsElapsed()) * 1e-9, refreshPeriodSeconds());
    }
    this->frame_clock.start();
    renderFrameAt(elapsed_seconds);
}

void MapBgfxSurface::renderFrameAt(double elapsed_seconds)
{
    if (!this->initialized || !isVisible())
        return;
    synchronizeGeometry();
    // Advance navigation (keyboard/wheel zoom, keyboard pan) by exactly the
    // time this frame represents, *then* sample the camera. Each presented
    // frame therefore shows one uniform camera step.
    if (this->navigation_frame && elapsed_seconds > 0.0)
        this->navigation_frame(elapsed_seconds);
    if (!this->initialized)
        return;
    this->camera_state = this->map_model->cameraViewState();
    const QSize pixels = physicalSize();
    const QSizeF logical = logicalSize();
    bgfx::setViewRect(kMapBgfxView, 0, 0,
                      static_cast<uint16_t>(std::min(65535, pixels.width())),
                      static_cast<uint16_t>(std::min(65535, pixels.height())));
    bgfx::setViewClear(kMapBgfxView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                       0x16324aff, 1.0f, 0);
    bgfx::setViewMode(kMapBgfxView, bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(kMapBgfxView, nullptr, nullptr);
    bgfx::touch(kMapBgfxView);
    this->network_frame_stats = MapBgfx2dGpu::FrameStats();
    if (this->camera_state.mode == MapViewMode::TwoD) {
        // Imagery first (opaque), network on top; the view is Sequential.
        renderImagery(logical);
        // Icon placement comes from the shared QRhi atlas layout. Atlas v
        // runs downwards (uv_rect.top() < bottom()); see appendIcon().
        const MapBgfx2dMesh::IconLookup icon_lookup = [](InfrastructureEntity entity_type)
        {
            const MapIconAtlasEntry entry = mapIconAtlasEntry(entity_type);
            MapBgfx2dIconSprite sprite;
            sprite.valid = entry.valid;
            sprite.u0 = float(entry.uv_rect.left());
            sprite.v0 = float(entry.uv_rect.top());
            sprite.u1 = float(entry.uv_rect.right());
            sprite.v1 = float(entry.uv_rect.bottom());
            sprite.half_width_ratio = float(entry.width_ratio * 0.5);
            sprite.half_height_ratio = float(entry.height_ratio * 0.5);
            return sprite;
        };
        if (!this->gpu_2d_scene.synchronize(this->prepared_2d_scene, icon_lookup)) {
            fail(QStringLiteral("bgfx network vertex-buffer upload failed"));
            return;
        }
        const MapBgfx2d::Camera view = MapBgfx2d::cameraFor(
            this->camera_state.center_lon_deg, this->camera_state.center_lat_deg,
            this->camera_state.continuous_2d_zoom,
            logical.width(), logical.height());
        // Colour stream: rebuilt only after a geometry upload or a symbology
        // change, never per frame.
        if (this->gpu_2d_scene.colorsStale() || this->symbology_colors_dirty)
        {
            const MapNetworkRenderSymbology &symbology = this->symbology;
            const MapBgfx2dMesh::ColorLookup lookup =
                [&symbology](MapBgfx2dPrimitive primitive, quint32 render_id,
                             InfrastructureEntity entity_type)
            {
                return MapBgfx2dSymbology::color(symbology, primitive, render_id, entity_type,
                                                 mapHasIcon(entity_type));
            };
            const MapBgfx2dMesh::FlowLookup flow_lookup = [&symbology](quint32 link_render_id)
            {
                return MapBgfx2dSymbology::flow(symbology, link_render_id);
            };
            if (this->gpu_2d_scene.updateColors(lookup, flow_lookup))
                this->symbology_colors_dirty = false;
        }
        synchronizeHighlights();
        // Sizes are uniforms from the continuous camera: metre-based widths
        // scale smoothly with the zoom.
        const MapBgfx2dSymbology::Sizes sizes = MapBgfx2dSymbology::sizes(
            this->symbology, this->camera_state.center_lat_deg,
            this->camera_state.continuous_2d_zoom);
        MapBgfx2dGpu::Style style;
        style.link_width_px = sizes.link_width_px;
        style.attachment_width_px = sizes.attachment_width_px;
        style.node_half_px = sizes.node_half_px;
        style.icon_size_px = sizes.icon_size_px;
        style.flow_length_px = this->symbology.show_flow_direction ? sizes.flow_length_px : 0.0;
        style.flow_half_stroke_px = sizes.flow_half_stroke_px;
        style.heatmap_radius_px = sizes.heatmap_radius_px;
        style.heatmap_opacity = sizes.heatmap_opacity;
        style.heatmap_solid_fraction = sizes.heatmap_solid_fraction;
        this->network_frame_stats = this->gpu_2d_scene.draw(view, style);
    }
    // The HUD is submitted last into the Sequential map view: always on top.
    renderHud(logical, MapBgfx2d::cameraFor(
        this->camera_state.center_lon_deg, this->camera_state.center_lat_deg,
        this->camera_state.continuous_2d_zoom, logical.width(), logical.height()));
    if (this->debug_overlay_enabled)
        renderDebugOverlay();
    bgfx::frame();
    ++this->frame_count;
    if (!this->ready_reported) {
        this->ready_reported = true;
        emit signalRendererReady();
    }
}


// AOWIS_BGFX_DEBUG=1: bgfx's text overlay with renderer statistics (it would
// cover the HUD, so it is off by default).
void MapBgfxSurface::renderDebugOverlay()
{
    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(2, 2, 0x0f, "AOWIS bgfx Map Monitor (EXPERIMENTAL)");
    bgfx::dbgTextPrintf(2, 4, 0x0e, "2D network, imagery, heatmap, picking, HUD active");
    bgfx::dbgTextPrintf(2, 6, 0x0f, "Camera lon %.5f lat %.5f",
                        this->camera_state.center_lon_deg, this->camera_state.center_lat_deg);
    if (this->camera_state.mode == MapViewMode::TwoD)
        bgfx::dbgTextPrintf(2, 8, 0x0f, "Continuous zoom %.4f", this->camera_state.continuous_2d_zoom);
    else
        bgfx::dbgTextPrintf(2, 8, 0x0f, "Globe distance %.2f m", this->camera_state.globe_distance_m);
    // Nodes include demand points (snapshot nodes of entity DemandPoint).
    bgfx::dbgTextPrintf(2, 10, 0x0f, "Links %lld (%lld segments) Nodes %lld Attachments %lld",
                        static_cast<long long>(this->prepared_2d_scene.linkPolylines().size()),
                        static_cast<long long>(this->prepared_2d_scene.linkSegmentCount()),
                        static_cast<long long>(this->prepared_2d_scene.networkNodes().size()),
                        static_cast<long long>(this->prepared_2d_scene.attachments().size()));
    bgfx::dbgTextPrintf(2, 11, 0x0f,
                        "Preparations %llu GPU uploads %llu (%llu bytes) Frames %llu",
                        static_cast<unsigned long long>(this->prepared_2d_scene.preparationCount()),
                        static_cast<unsigned long long>(this->gpu_2d_scene.uploadCount()),
                        static_cast<unsigned long long>(this->gpu_2d_scene.uploadedBytes()),
                        static_cast<unsigned long long>(this->frame_count));
    bgfx::dbgTextPrintf(2, 14, 0x0f, "Symbology colour uploads %llu, flow chevrons %lld",
                        static_cast<unsigned long long>(this->gpu_2d_scene.colorUploadCount()),
                        static_cast<long long>(this->gpu_2d_scene.chevronCount()));
    bgfx::dbgTextPrintf(2, 13, 0x0f, "Network chunks %d/%d drawn, %d draw calls",
                        this->network_frame_stats.chunks_drawn,
                        this->network_frame_stats.chunks_total,
                        this->network_frame_stats.draw_calls);
    bgfx::dbgTextPrintf(2, 12, 0x0f,
                        "Imagery z%d quads %d textures %d uploads %llu",
                        this->imagery_zoom, this->imagery_quad_count,
                        this->imagery_gpu.textureCount(),
                        static_cast<unsigned long long>(this->imagery_gpu.uploadCount()));
}

void MapBgfxSurface::fail(const QString &reason)
{
    this->failed = true;
    stopFrameLoop();
    if (this->initialized) {
        releaseGpuResources();
        bgfx::shutdown();
        this->initialized = false;
        g_bgfx_map_initialized = false;
    }
    qWarning().noquote() << reason;
    emit signalRendererFailed(reason);
}

void MapBgfxSurface::setTileRepository(MapTileRepository *tile_repository)
{
    if (this->tile_repository == tile_repository)
        return;
    if (this->tile_repository != nullptr)
        QObject::disconnect(this->tile_repository, nullptr, this, nullptr);
    this->tile_repository = tile_repository;
    this->imagery_layout_key = 0;
    this->imagery_requests_dirty = true;
    if (this->tile_repository == nullptr)
        return;
    // A failed tile becoming retryable needs a fresh request; an arriving
    // tile needs nothing (the next frame finds it in the repository cache).
    connect(this->tile_repository, &MapTileRepository::signalTileRetryReady, this,
            [this](const QString &) { this->imagery_requests_dirty = true; });
    connect(this->tile_repository, &MapTileRepository::signalTilesDeleted, this, [this]()
    {
        if (this->initialized)
            this->imagery_gpu.clearTextures();
        this->imagery_requests_dirty = true;
    });
}

void MapBgfxSurface::setBackgroundOpacity(int opacity)
{
    this->background_opacity = qBound(0, opacity, 100);
}

void MapBgfxSurface::renderImagery(const QSizeF &logical)
{
    this->imagery_quad_count = 0;
    const MapBgfx2d::WorldPoint center = MapBgfx2d::worldFromWgs84(
        this->camera_state.center_lon_deg, this->camera_state.center_lat_deg);
    MapBgfx2dImagery::Camera camera;
    camera.center_x = center.x;
    camera.center_y = center.y;
    camera.zoom = this->camera_state.continuous_2d_zoom;
    camera.viewport_width = logical.width();
    camera.viewport_height = logical.height();

    // Only GPU-resident textures count as available for drawing; repository
    // tiles are uploaded below within a per-frame budget, nearest first.
    const MapBgfx2dImagery::Availability resident =
        [this](const MapBgfx2dImagery::TileId &tile) { return this->imagery_gpu.hasTexture(tile); };
    MapBgfx2dImagery::Layout layout = MapBgfx2dImagery::buildLayout(
        camera, MapModel::MinZoom, MapModel::MaxZoom, resident);
    this->imagery_zoom = layout.imagery_zoom;

    if (this->tile_repository != nullptr && !layout.requests.empty())
    {
        if (layout.layout_key != this->imagery_layout_key)
        {
            // New target tile set: a new batch lets the repository drop
            // queued requests that are no longer visible.
            this->imagery_layout_key = layout.layout_key;
            this->imagery_request_batch = this->tile_repository->beginTileRequestBatch(
                this, QStringLiteral("bgfx|%1|%2")
                    .arg(this->map_model->tileCachePrefix(layout.imagery_zoom))
                    .arg(layout.layout_key));
            this->imagery_requests_dirty = true;
        }
        int uploads = 0;
        for (const MapBgfx2dImagery::Request &request : layout.requests)
        {
            const QString key = this->map_model->tileCacheKeyAtZoom(
                request.tile.x, request.tile.y, request.tile.zoom);
            const QPixmap *pixmap = this->tile_repository->tile(key);
            if (pixmap != nullptr)
            {
                // Bounded so a burst of arriving tiles never stalls a frame.
                if (uploads < kImageryUploadsPerFrame
                    && this->imagery_gpu.uploadTexture(request.tile, pixmap->toImage()))
                {
                    ++uploads;
                }
                continue;
            }
            if (this->imagery_requests_dirty)
            {
                this->tile_repository->requestTile(
                    this->map_model->tileEndpointAtZoom(
                        request.tile.x, request.tile.y, request.tile.zoom),
                    key, request.tile.x, request.tile.y, request.priority,
                    this->imagery_request_batch);
            }
        }
        this->imagery_requests_dirty = false;
        if (uploads > 0)
            layout = MapBgfx2dImagery::buildLayout(
                camera, MapModel::MinZoom, MapModel::MaxZoom, resident);
    }

    this->imagery_quad_count = int(layout.quads.size());
    this->imagery_gpu.draw(kMapBgfxView, layout, logical.width(), logical.height(),
                           palette().color(QPalette::Window),
                           double(this->background_opacity) / 100.0);
    this->imagery_gpu.endFrame();
}

void MapBgfxSurface::setSymbology(const MapNetworkRenderSymbology &symbology)
{
    this->symbology = symbology;
    this->symbology_colors_dirty = true;
}

void MapBgfxSurface::setVisualControlSettings(const NetworkSymbologySettings &settings)
{
    // Cheap path for slider/toggle changes: sizes, the heatmap radius,
    // opacity and solid centre are uniforms; only the junction and flow-arrow
    // toggles need the attribute stream again. Heatmap values and palette
    // arrive through setSymbology().
    const NetworkSymbologySettings bounded = settings.bounded();
    const bool attributes_changed = this->symbology.show_junctions != bounded.show_junctions
        || this->symbology.show_flow_direction != bounded.show_flow_direction;
    this->symbology.node_size_unit = bounded.node_size_unit;
    this->symbology.node_size_px = bounded.node_size_px;
    this->symbology.node_size_m = bounded.node_size_m;
    this->symbology.link_thickness_unit = bounded.link_thickness_unit;
    this->symbology.link_thickness_px = bounded.link_thickness_px;
    this->symbology.link_thickness_m = bounded.link_thickness_m;
    this->symbology.show_junctions = bounded.show_junctions;
    this->symbology.icon_size_unit = bounded.icon_size_unit;
    this->symbology.icon_size_px = bounded.icon_size_px;
    this->symbology.icon_size_m = bounded.icon_size_m;
    this->symbology.show_flow_direction = bounded.show_flow_direction;
    this->symbology.flow_direction_size_px = bounded.flow_direction_size_px;
    this->symbology.heatmap_opacity = bounded.heatmap_opacity;
    this->symbology.heatmap_radius_unit = bounded.heatmap_radius_unit;
    this->symbology.heatmap_radius_m = bounded.heatmap_radius_m;
    this->symbology.heatmap_radius_px = bounded.heatmap_radius_px;
    this->symbology.heatmap_solid_center_percent = bounded.heatmap_solid_center_percent;
    if (attributes_changed)
        this->symbology_colors_dirty = true;
}
