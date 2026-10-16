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
#include "network/infrastructure_entity_traits.h"

#include <QByteArray>
#include <QDebug>
#include <QGuiApplication>
#include <QFocusEvent>
#include <QFont>
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
#include <emscripten/html5.h>

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
// Latest uncaught browser error recorded by index.html, written as UTF-8
// into buffer (NUL-terminated, truncated to capacity). Uses only HEAPU8 so
// it does not depend on optional Emscripten string helpers being linked.
EM_JS(int, aowisLastBrowserError, (char *buffer, int capacity), {
    const errors = window.aowisRecentErrors;
    const text = Array.isArray(errors) && errors.length > 0 ? errors[errors.length - 1] : "";
    const bytes = new TextEncoder().encode(String(text));
    const length = Math.min(bytes.length, capacity - 1);
    for (let index = 0; index < length; ++index)
        HEAPU8[buffer + index] = bytes[index];
    HEAPU8[buffer + length] = 0;
    return length;
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
// bgfx::init/bgfx::shutdown own a process-global renderer. It belongs to one
// surface at a time: the visible one (Map Monitor or Map Editor tab). A
// surface that becomes visible takes it over from a hidden owner.
MapBgfxSurface *g_bgfx_device_owner = nullptr;
// Room for the editor overlay (one icon quad per visible node) on top of the
// HUD and imagery: bgfx's defaults are 6 MiB vertices / 2 MiB indices.
constexpr uint32_t kTransientVertexBytes = 16u << 20;
constexpr uint32_t kTransientIndexBytes = 8u << 20;
// Tile texture uploads per frame (~256 KiB each). Enough to fill a screen
// within a few frames, small enough to never cost a frame while streaming.
constexpr int kImageryUploadsPerFrame = 8;
// VSYNC: presentation pacing (see MapFramePacer). MSAA x4: antialiased thick
// line and marker edges; bgfx clamps it to what the renderer supports.
#ifdef Q_OS_WASM
// NOT on WebGL: bgfx's OpenGL ES MSAA back buffer calls
// glFramebufferTexture2DMultisampleEXT (EXT_multisampled_render_to_texture),
// which WebGL does not provide. The function pointer stays NULL and the first
// frame traps with "RuntimeError: null function" inside
// RendererContextGL::submit(). The browser multisamples the canvas instead:
// the WebGL context is created with antialias=true (createBrowserContext()).
constexpr uint32_t kMapBgfxResetFlags = BGFX_RESET_VSYNC;
#else
constexpr uint32_t kMapBgfxResetFlags = BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X4;
#endif
// Fallback when the platform reports no usable refresh rate.
constexpr double kFallbackRefreshHz = 60.0;
#ifdef Q_OS_WASM
// The WebGL 2 context for the map canvas, created here instead of by bgfx
// (bgfx hard-codes antialias=false for the contexts it creates). bgfx adopts
// it through Init::platformData.context and destroys it on bgfx::shutdown().
// Opaque canvas (alpha=false), depth + stencil for the default framebuffer,
// browser multisampling for antialiased line and marker edges.
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE createBrowserContext()
{
    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion = 2;
    attributes.minorVersion = 0;
    attributes.alpha = false;
    attributes.depth = true;
    attributes.stencil = true;
    attributes.antialias = true;
    attributes.premultipliedAlpha = false;
    attributes.enableExtensionsByDefault = true;
    return emscripten_webgl_create_context("!aowis-bgfx-map", &attributes);
}

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
        this->initialized = false;
        if (g_bgfx_device_owner == this)
            g_bgfx_device_owner = nullptr;
    }
    if (g_bgfx_device_owner == this)
        g_bgfx_device_owner = nullptr;
#ifdef Q_OS_WASM
    // The canvas is shared; only the last surface removes it.
    if (g_bgfx_device_owner == nullptr)
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
    if (this->keyboard_focus_widget)
    {
        // Editor: keys go to its canvas widget (ordinary QWidget focus).
        this->keyboard_focus_widget->setFocus(Qt::OtherFocusReason);
        return;
    }
#ifndef Q_OS_WASM
    // The embedded native window owns key focus on desktop. QWidget focus on
    // the hidden MapWidget cannot be used as a substitute.
    setFocus(Qt::ShortcutFocusReason);
    if (this->native_window != nullptr)
        this->native_window->requestActivate();
#endif
}

void MapBgfxSurface::setKeyboardFocusWidget(QWidget *widget)
{
    this->keyboard_focus_widget = widget;
#ifndef Q_OS_WASM
    // Clicks must not activate the embedded window, or the top-level loses
    // its focus widget (and the editor's window shortcuts with it).
    if (this->native_window != nullptr)
        this->native_window->setFlag(Qt::WindowDoesNotAcceptFocus, widget != nullptr);
#endif
}

bool MapBgfxSurface::eventFilter(QObject *watched, QEvent *event)
{
#ifndef Q_OS_WASM
    // Platforms that destroy and recreate the whole native window announce
    // it here: bgfx must let go of the old surface first and start again on
    // the new one. This is only the fallback; Qt Wayland destroys the
    // wl_surface on reparent WITHOUT this event, so planned reparents
    // (fullscreen) go through suspendForNativeWindowChange() beforehand.
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
    // A suspended backend restarts once the (new) surface is on screen.
    if (watched == this->native_window && event->type() == QEvent::Expose
        && this->reinitialize_on_surface && this->native_window->isExposed())
    {
        scheduleBackendRestart();
    }
    if (watched == this->native_window && this->initialized)
    {
        if (event->type() == QEvent::KeyPress && handleHudKeyPress(static_cast<QKeyEvent *>(event)))
            return true;
        if (event->type() == QEvent::KeyPress && this->navigation_key_press)
            return this->navigation_key_press(static_cast<QKeyEvent *>(event));
        if (event->type() == QEvent::KeyRelease && this->navigation_key_release)
            return this->navigation_key_release(static_cast<QKeyEvent *>(event));
        // The native window fills this surface, which has the same geometry
        // as MapWidget in the stack, so the position is a valid anchor as-is.
        // The HUD sees the wheel first: over a HUD panel it scrolls a list
        // or is swallowed, never zooming the map underneath.
        if (event->type() == QEvent::Wheel && this->navigation_wheel)
        {
            QWheelEvent *wheel = static_cast<QWheelEvent *>(event);
            if (!handleHudWheelEvent(wheel))
                this->navigation_wheel(wheel);
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
            // A click must also move keyboard focus to the map, otherwise
            // zoom/pan keys keep going to the previous widget: the embedded
            // window itself, or the configured focus widget (editor).
            if (this->keyboard_focus_widget)
            {
                this->keyboard_focus_widget->setFocus(Qt::MouseFocusReason);
                this->keyboard_focus_widget->window()->activateWindow();
            }
            else
            {
                this->native_window->requestActivate();
            }
            return this->navigation_mouse_press(static_cast<QMouseEvent *>(event));
        }
        if (event->type() == QEvent::MouseButtonRelease && this->navigation_mouse_release)
            return this->navigation_mouse_release(static_cast<QMouseEvent *>(event));
        if (event->type() == QEvent::MouseMove && this->navigation_mouse_move)
            return this->navigation_mouse_move(static_cast<QMouseEvent *>(event));
        if (event->type() == QEvent::FocusOut && this->navigation_cancel)
            this->navigation_cancel();
        // Like a Qt popup, an open HUD popup closes when the map loses focus.
        if (event->type() == QEvent::FocusOut)
            this->hud.closePopup();
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
    if (handleHudKeyPress(event))
        return;
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
    // New entities or renamed ones: the labels read the IDs again.
    if (update.geometry_changed || update.visual_changed)
        this->labels_dirty = true;
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
    this->overlay_gpu.release();
    this->label_gpu.release();
    // Everything staged on the GPU is gone with the device.
    this->highlights_dirty = true;
    this->labels_dirty = true;
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
    if (g_bgfx_device_owner == this)
        g_bgfx_device_owner = nullptr;
    this->reinitialize_on_surface = true;
}

void MapBgfxSurface::scheduleBackendRestart()
{
    // Queued: the platform window must be fully set up, and the widget may
    // become visible only after the surface was created (or the other way
    // round); show, SurfaceCreated and Expose all end here and the first one
    // that finds a mapped, exposed window starts the backend. Starting on an
    // unexposed Wayland surface would block the first vsync swap.
    QTimer::singleShot(0, this, [this]()
    {
        if (!this->reinitialize_on_surface || this->initialized || this->failed || !isVisible())
            return;
#ifndef Q_OS_WASM
        if (this->native_window == nullptr || this->native_window->handle() == nullptr
            || !this->native_window->isExposed())
        {
            return;
        }
#endif
        this->reinitialize_on_surface = false;
        if (initializeBackend())
        {
            startFrameLoop();
            return;
        }
        if (this->device_busy)
        {
            // Another visible map still renders; try again shortly.
            this->reinitialize_on_surface = true;
            QTimer::singleShot(250, this, [this]() { scheduleBackendRestart(); });
            return;
        }
        fail(QStringLiteral("bgfx could not restart on the recreated map window"));
    });
}

void MapBgfxSurface::suspendForNativeWindowChange()
{
    // bgfx::shutdown() destroys the swapchain and the VkSurface / EGL window
    // while the native surface still exists. Without this, Qt Wayland
    // destroys the wl_surface under a live Vulkan swapchain on reparent and
    // Mesa's explicit-sync object then hits a dead surface: fatal Wayland
    // protocol error ("Surface got destroyed already").
    shutdownBackend();
}

void MapBgfxSurface::resumeAfterNativeWindowChange()
{
    // Restarts when the window is exposed again (see the Expose handling in
    // eventFilter()); no-op when the backend was not running.
    scheduleBackendRestart();
}

void MapBgfxSurface::setHudActionHandler(std::function<void(const MapBgfxHud::Command &)> handler)
{
    this->hud_action_handler = std::move(handler);
}

void MapBgfxSurface::setHudLegend(const MapBgfxHud::Legend &legend)
{
    this->hud.setLegend(legend);
}

void MapBgfxSurface::closeHudPopup()
{
    this->hud.closePopup();
}

void MapBgfxSurface::setHudViewSwitchVisible(bool visible)
{
    this->hud.setViewSwitchVisible(visible);
}

void MapBgfxSurface::setOverlayBuilder(OverlayBuilder builder)
{
    this->overlay_builder = std::move(builder);
}

void MapBgfxSurface::setLabelSettings(const MapLabelSettings &settings)
{
    if (this->label_settings == settings)
        return;
    this->label_settings = settings;
    this->labels_dirty = true;
}

void MapBgfxSurface::setLabelValueFormats(const MapBgfxLabelValueFormat &node_format,
                                          const MapBgfxLabelValueFormat &link_format)
{
    if (this->label_node_format == node_format && this->label_link_format == link_format)
        return;
    this->label_node_format = node_format;
    this->label_link_format = link_format;
    this->labels_dirty = true;
}

void MapBgfxSurface::setLabelSymbolRects(MapBgfxLabels::SymbolRect node_symbol,
                                         MapBgfxLabels::SymbolRect link_symbol)
{
    this->label_node_symbol = std::move(node_symbol);
    this->label_link_symbol = std::move(link_symbol);
    this->labels_dirty = true;
}

void MapBgfxSurface::setHiddenEntityUuids(const QSet<QUuid> &uuids)
{
    if (this->hidden_entity_uuids == uuids)
        return;
    this->hidden_entity_uuids = uuids;
    // Only the colour stream changes (alpha 0 for hidden entities).
    this->symbology_colors_dirty = true;
    // Moved entities lose their labels during the move.
    this->labels_dirty = true;
}

void MapBgfxSurface::setNetworkScreenTranslation(const QPointF &translation)
{
    this->network_screen_translation = translation;
}

bool MapBgfxSurface::handleHudWheelEvent(QWheelEvent *event)
{
    if (event == nullptr || !this->initialized)
        return false;
    const QPointF position = event->position();
    return this->hud.mouseWheel(position.x(), position.y(), double(event->angleDelta().y()));
}

bool MapBgfxSurface::handleHudKeyPress(QKeyEvent *event)
{
    if (event == nullptr || !this->initialized || event->key() != Qt::Key_Escape)
        return false;
    return this->hud.closePopup();
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
                MapBgfxHud::Command command;
                const MapBgfxHud::Action action = this->hud.mouseRelease(
                    position.x(), position.y(), &result.consumed, &command);
                if (action != MapBgfxHud::Action::None && this->hud_action_handler)
                {
                    // Deferred: an action may reparent this widget (fullscreen),
                    // which must not happen inside the native window's event.
                    const std::function<void(const MapBgfxHud::Command &)> handler =
                        this->hud_action_handler;
                    QTimer::singleShot(0, this, [handler, command]() { handler(command); });
                }
            }
            else
            {
                result.consumed = result.over_panel;
            }
            break;
        case QEvent::MouseMove:
        {
            result.consumed = this->hud.mouseMove(position.x(), position.y(), nullptr);
            switch (this->hud.cursor())
            {
                case MapBgfxHud::Cursor::PointingHand:
                    result.cursor = Qt::PointingHandCursor;
                    break;
                case MapBgfxHud::Cursor::Cross:
                    result.cursor = Qt::CrossCursor;
                    break;
                case MapBgfxHud::Cursor::Arrow:
                    result.cursor = Qt::ArrowCursor;
                    break;
            }
            break;
        }
        default:
            break;
    }
    return result;
}

void MapBgfxSurface::setMapCursor(Qt::CursorShape shape)
{
#ifndef Q_OS_WASM
    // The embedded native window has its own cursor; MapWidget's cursor is
    // never shown over it.
    if (this->native_window == nullptr)
        return;
    if (shape != Qt::ArrowCursor)
        this->native_window->setCursor(shape);
    else
        this->native_window->unsetCursor();
#else
    if (shape != Qt::ArrowCursor)
        setCursor(shape);
    else
        unsetCursor();
#endif
}

void MapBgfxSurface::renderOverlay(const QSizeF &logical, const MapBgfx2d::Camera &camera)
{
    if (!this->overlay_builder || !this->overlay_gpu.ready())
        return;
#ifdef Q_OS_WASM
    const qreal ratio = devicePixelRatioF();
    const QScreen *overlay_screen = screen();
#else
    const qreal ratio = this->native_window->devicePixelRatio();
    const QScreen *overlay_screen = this->native_window->screen();
#endif
    this->overlay_text_cache.setDevicePixelRatio(ratio);
    if (overlay_screen != nullptr)
        this->overlay_text_cache.setLogicalDpi(overlay_screen->logicalDotsPerInch());
    this->overlay_builder(&this->overlay_canvas, &this->overlay_text_cache, camera, logical);
    this->overlay_gpu.uploadAtlas(&this->overlay_text_cache);
    this->overlay_gpu.draw(kMapBgfxView, this->overlay_canvas, logical);
}

void MapBgfxSurface::renderLabels(const QSizeF &logical, const MapBgfx2d::Camera &network_camera,
                                  const MapBgfx2dSymbology::Sizes &sizes)
{
    if (!this->label_settings.anyEnabled() || !this->label_gpu.ready()
        || !this->retained_scene.hasSnapshot() || !this->prepared_2d_scene.isPrepared())
    {
        return;
    }
#ifdef Q_OS_WASM
    const qreal ratio = devicePixelRatioF();
    const QScreen *label_screen = screen();
#else
    const qreal ratio = this->native_window->devicePixelRatio();
    const QScreen *label_screen = this->native_window->screen();
#endif
    if (!this->label_text_cache)
        this->label_text_cache = std::make_unique<MapBgfxHudTextCache>(2048);
    MapBgfxHudTextCache *cache = this->label_text_cache.get();
    cache->setDevicePixelRatio(ratio);
    const double dpi = label_screen != nullptr ? label_screen->logicalDotsPerInch() : 96.0;
    cache->setLogicalDpi(dpi);

    LabelBuildKey key;
    key.center_x = network_camera.center.x;
    key.center_y = network_camera.center.y;
    key.world_pixels = network_camera.world_pixels;
    key.viewport_width = logical.width();
    key.viewport_height = logical.height();
    key.node_half_px = sizes.node_half_px;
    key.icon_size_px = sizes.icon_size_px;
    key.device_pixel_ratio = ratio;
    key.preparation_id = this->prepared_2d_scene.preparationId();
    key.atlas_generation = cache->generation();
    if (this->labels_dirty || !(key == this->label_build_key))
    {
        // The application font one step smaller, pixel sized (the halo
        // outline path and the metrics then agree; see haloText()).
        QFont font = QGuiApplication::font();
        const double point_size = font.pointSizeF() > 0.0 ? font.pointSizeF() : 9.0;
        font.setPixelSize(std::max(9, int(std::lround(0.92 * point_size * dpi / 72.0))));

        // Hidden entities (editor move preview) by entity key.
        QSet<quint64> hidden_keys;
        for (const QUuid &uuid : this->hidden_entity_uuids)
        {
            const QHash<QUuid, quint64>::const_iterator found =
                this->entity_key_by_uuid.constFind(uuid);
            if (found != this->entity_key_by_uuid.cend())
                hidden_keys.insert(found.value());
        }
        // Default symbol boxes: what the GPU network draws (dots, or icons
        // of icon_size_px with the atlas aspect; pipes have none).
        const MapNetworkRenderSymbology &symbology = this->symbology;
        const MapBgfxLabels::SymbolRect icon_box = [sizes](InfrastructureEntity entity_type)
        {
            const MapIconAtlasEntry entry = mapIconAtlasEntry(entity_type);
            const double half_width = sizes.icon_size_px * entry.width_ratio * 0.5;
            const double half_height = sizes.icon_size_px * entry.height_ratio * 0.5;
            return MapBgfxHudRect{-half_width, -half_height, 2.0 * half_width, 2.0 * half_height};
        };
        const MapBgfxLabels::SymbolRect default_node_symbol =
            [&symbology, sizes, icon_box](InfrastructureEntity entity_type)
        {
            if (entity_type != InfrastructureEntity::Junction && symbology.show_icons
                && mapHasIcon(entity_type))
                return icon_box(entity_type);
            if (entity_type == InfrastructureEntity::Junction && !symbology.show_junctions)
                return MapBgfxHudRect();
            const double half = sizes.node_half_px;
            return MapBgfxHudRect{-half, -half, 2.0 * half, 2.0 * half};
        };
        const MapBgfxLabels::SymbolRect default_link_symbol =
            [&symbology, icon_box](InfrastructureEntity entity_type)
        {
            if (entity_type != InfrastructureEntity::Pipe && symbology.show_icons
                && mapHasIcon(entity_type))
                return icon_box(entity_type);
            return MapBgfxHudRect();
        };

        MapBgfxLabels::Inputs inputs;
        inputs.viewport = logical;
        inputs.camera = network_camera;
        inputs.scene = &this->prepared_2d_scene;
        inputs.snapshot = &this->retained_scene.snapshot();
        inputs.settings = this->label_settings;
        inputs.node_values = &this->symbology.node_values;
        inputs.link_values = &this->symbology.link_values;
        inputs.node_value_format = this->label_node_format;
        inputs.link_value_format = this->label_link_format;
        inputs.hidden_keys = &hidden_keys;
        inputs.node_symbol = this->label_node_symbol ? this->label_node_symbol : default_node_symbol;
        inputs.link_symbol = this->label_link_symbol ? this->label_link_symbol : default_link_symbol;
        inputs.font = font;
        const bool complete = this->labels.build(&this->label_canvas, cache, inputs);
        // Deferred labels: build again next frame even without a change.
        this->labels_dirty = !complete;
        key.atlas_generation = cache->generation();
        this->label_build_key = key;
    }
    this->label_gpu.uploadAtlas(cache);
    this->label_gpu.draw(kMapBgfxView, this->label_canvas, logical);
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
    beginInitialization();
}

void MapBgfxSurface::beginInitialization()
{
    // Native platform window and Qt's WASM shadow-root must exist first.
    QTimer::singleShot(0, this, [this]() {
        if (!isVisible() || this->failed || this->initialization_attempted || this->initialized)
            return;
        this->initialization_attempted = true;
        qInfo() << "bgfx map: initializing at" << physicalSize() << "physical pixels";
        if (initializeBackend())
        {
            qInfo() << "bgfx map: initialized, waiting for the first frame";
            startFrameLoop();
            return;
        }
        if (this->device_busy)
        {
            // The other map tab has not released the device yet (it is
            // still visible); not a failure.
            this->initialization_attempted = false;
            QTimer::singleShot(250, this, [this]() { beginInitialization(); });
            return;
        }
        fail(QStringLiteral("bgfx initialization failed (%1)").arg(startupDiagnostics()));
    });
}

void MapBgfxSurface::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    stopFrameLoop();
    // No popup survives the map being hidden (tab switch, fullscreen).
    this->hud.closePopup();
    // The device follows the visible map: a hidden map releases it so the
    // other tab's map can start (restarts on the next show). A fullscreen
    // reparent has already suspended the backend; nothing to do then.
    if (this->initialized)
        shutdownBackend();
#ifdef Q_OS_WASM
    // The canvas is shared; hide it only when no other map owns it now.
    if (g_bgfx_device_owner == nullptr)
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
    // The shared canvas follows the map that owns the device.
    if (g_bgfx_device_owner == this || g_bgfx_device_owner == nullptr)
    {
        const QPoint position = mapToGlobal(QPoint(0, 0));
        aowisBgfxMapGeometry(position.x(), position.y(), width(), height(),
                             isVisible() && this->initialized ? 1 : 0);
    }
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
    this->device_busy = false;
    if (g_bgfx_device_owner != nullptr && g_bgfx_device_owner != this)
    {
        if (g_bgfx_device_owner->isVisible())
        {
            // Two maps visible at once (not a layout the application has):
            // wait for the other one to be hidden.
            qInfo() << "bgfx map: the device is in use by another visible map; waiting";
            this->device_busy = true;
            return false;
        }
        // Take the device over from a hidden map (it restarts when shown).
        g_bgfx_device_owner->shutdownBackend();
    }
    bgfx::Init settings{};
    settings.limits.maxTransientVbSize = kTransientVertexBytes;
    settings.limits.maxTransientIbSize = kTransientIndexBytes;
    this->startup_step = "preparing the render target";
#ifdef Q_OS_WASM
    if (!aowisBgfxMapPrepare())
    {
        // window.aowisBgfxMap missing (aowis-bgfx-map.js not loaded), Qt's
        // screen element not found, or specialHTMLTargets unavailable.
        qWarning() << "bgfx map: the browser canvas could not be prepared";
        return false;
    }
    settings.swapChain.nwh = const_cast<char *>("!aowis-bgfx-map");
    {
        const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = createBrowserContext();
        if (context <= 0)
        {
            qWarning() << "bgfx map: the WebGL 2 context could not be created, error"
                       << int(context);
            return false;
        }
        settings.platformData.context =
            reinterpret_cast<void *>(static_cast<intptr_t>(context));
    }
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
        this->startup_step = "bgfx::init()";
        qInfo() << "bgfx map: bgfx::init() for"
                << (attempt == bgfx::RendererType::Count
                    ? "automatic selection" : bgfx::getRendererName(attempt));
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
        g_bgfx_device_owner = this;
        this->startup_step = "creating shaders and GPU resources";
        // Each part reports what it rejected; name the part here as well.
        const bool network_ready = this->gpu_2d_scene.initialize(mapIconAtlasImage());
        const bool imagery_ready = network_ready && this->imagery_gpu.initialize();
        const bool hud_ready = imagery_ready && this->hud_gpu.initialize()
            && this->overlay_gpu.initialize() && this->label_gpu.initialize();
        if (!network_ready || !imagery_ready || !hud_ready)
        {
            const bgfx::Caps *caps = bgfx::getCaps();
            qWarning() << "bgfx map shader initialization failed on"
                       << bgfx::getRendererName(selected) << "in"
                       << (!network_ready ? "network" : !imagery_ready ? "imagery" : "HUD")
                       << "- max texture size" << (caps != nullptr ? int(caps->limits.maxTextureSize) : -1)
                       << "RGBA8 caps" << Qt::hex
                       << (caps != nullptr ? int(caps->formats[bgfx::TextureFormat::RGBA8]) : -1);
            releaseGpuResources();
            bgfx::shutdown();
            this->initialized = false;
            g_bgfx_device_owner = nullptr;
            continue;
        }
        this->startup_step = "running";
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
    // Counted before any early return: tells a dead frame loop apart from a
    // loop that runs but skips (startupDiagnostics()).
    ++this->frame_callback_count;
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
            // Hidden entities (editor move preview) by the keys the colour
            // lookup sees: links by render id, nodes by entity key, demand
            // point attachments by the demand point's render id.
            QSet<quint32> hidden_links;
            QSet<quint64> hidden_nodes;
            QSet<quint32> hidden_attachments;
            for (const QUuid &uuid : this->hidden_entity_uuids)
            {
                const QHash<QUuid, quint64>::const_iterator found =
                    this->entity_key_by_uuid.constFind(uuid);
                if (found == this->entity_key_by_uuid.cend())
                    continue;
                const InfrastructureEntity entity_type =
                    static_cast<InfrastructureEntity>(quint8(found.value() >> 32));
                const quint32 render_id = quint32(found.value() & 0xffffffffu);
                if (InfrastructureEntityTraits::isHydraulicNetworkLink(entity_type))
                {
                    hidden_links.insert(render_id);
                    continue;
                }
                hidden_nodes.insert(found.value());
                if (entity_type == InfrastructureEntity::DemandPoint)
                    hidden_attachments.insert(render_id);
            }
            const MapBgfx2dMesh::ColorLookup lookup =
                [&symbology, &hidden_links, &hidden_nodes, &hidden_attachments](
                    MapBgfx2dPrimitive primitive, quint32 render_id,
                    InfrastructureEntity entity_type)
            {
                switch (primitive)
                {
                    case MapBgfx2dPrimitive::Link:
                    case MapBgfx2dPrimitive::LinkIcon:
                        if (hidden_links.contains(render_id))
                            return 0u;
                        break;
                    case MapBgfx2dPrimitive::Attachment:
                        if (hidden_attachments.contains(render_id))
                            return 0u;
                        break;
                    default:
                        if (!hidden_nodes.isEmpty()
                            && hidden_nodes.contains(MapBgfx2dScene::entityKey(entity_type, render_id)))
                            return 0u;
                        break;
                }
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
        // Editor full-network move: the network is drawn shifted (camera
        // offset), the imagery underneath is not.
        MapBgfx2d::Camera network_view = view;
        if (!this->network_screen_translation.isNull() && view.world_pixels > 0.0)
        {
            network_view.center.x -= this->network_screen_translation.x() / view.world_pixels;
            network_view.center.y -= this->network_screen_translation.y() / view.world_pixels;
        }
        this->network_frame_stats = this->gpu_2d_scene.draw(network_view, style);
        renderOverlay(logical, view);
        // Labels above the network and the editor's icons, below the HUD.
        renderLabels(logical, network_view, sizes);
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
        qInfo() << "bgfx map: first frame presented";
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

QString MapBgfxSurface::startupDiagnostics() const
{
    QString stage;
    if (this->failed)
        stage = QStringLiteral("failed");
    else if (!this->initialization_attempted)
        stage = isVisible() ? QStringLiteral("not initialized yet (waiting for the event loop)")
                            : QStringLiteral("not initialized: map surface not visible");
    else if (!this->initialized)
        stage = QStringLiteral("initialization did not complete (stuck in: %1)")
                    .arg(QString::fromLatin1(this->startup_step));
    else if (!this->frame_loop_running)
        stage = QStringLiteral("initialized, frame loop not running");
    else if (this->frame_count == 0)
        stage = QStringLiteral("initialized, no frame presented yet");
    else
        stage = QStringLiteral("presenting frames");
    const QSize pixels = physicalSize();
    return QStringLiteral("%1; visible %2, size %3x%4 px, frame callbacks %5, frames %6")
        .arg(stage)
        .arg(isVisible() ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(pixels.width())
        .arg(pixels.height())
        .arg(this->frame_callback_count)
        .arg(this->frame_count)
#ifdef Q_OS_WASM
        // A JavaScript exception thrown inside bgfx/Emscripten code unwinds
        // the Qt callback silently; the page recorded it.
        + []()
        {
            QByteArray buffer(512, '\0');
            const int length = aowisLastBrowserError(buffer.data(), int(buffer.size()));
            const QString error = QString::fromUtf8(buffer.constData(), std::max(0, length));
            return error.isEmpty() ? QString()
                                   : QStringLiteral("\nlast browser error: %1").arg(error);
        }()
#endif
        ;
}

void MapBgfxSurface::fail(const QString &reason)
{
    this->failed = true;
    stopFrameLoop();
    if (this->initialized) {
        releaseGpuResources();
        bgfx::shutdown();
        this->initialized = false;
        if (g_bgfx_device_owner == this)
            g_bgfx_device_owner = nullptr;
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
    // New values (time step, metric) and symbol sizes.
    this->labels_dirty = true;
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
    // Junction visibility and icon sizes move the labels' symbol boxes.
    this->labels_dirty = true;
}
