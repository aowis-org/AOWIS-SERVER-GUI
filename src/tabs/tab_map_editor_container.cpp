#include "tabs/tab_map_editor_container.h"
#include "config/gui_configuration.h"
#include "config/shortcut_registry.h"

#include "network/network_symbology_rendering.h"
#include "map/bgfx/map_bgfx_2d_imagery_layout.h"
#include "map/bgfx/map_bgfx_editor_icons.h"
#include "map/bgfx/map_bgfx_editor_overlay.h"
#include "map/bgfx/map_bgfx_globe_hud_controller.h"
#include "map/bgfx/map_bgfx_pan_reuse.h"
#include "map/bgfx/map_bgfx_surface.h"
#include "map/render/map_entity_pixmap_renderer.h"

#include <QCoreApplication>
#ifndef Q_OS_WASM
#include <QGeoCoordinate>
#include <QGeoPositionInfo>
#endif
#include <QDebug>
#include <QKeyEvent>
#include <QMessageBox>
#include <QHideEvent>
#include <functional>
#include <QMouseEvent>
#include <QShortcut>
#include <QWheelEvent>

#include <memory>
#include <optional>

namespace
{
void bindEditorShortcut(QAbstractButton *button, GuiShortcutId id,
                        const QString &label, QObject *context)
{
    if (button == nullptr || context == nullptr)
        return;

    button->setText(QStringLiteral("[%1] %2").arg(
        guiShortcutPresentation(id), label));
    button->setShortcut(guiShortcutRegistry().keySequence(id));
    installShortcutEditContextMenu(button, id);

    QObject::connect(&guiShortcutRegistry(), &GuiShortcutRegistry::shortcutChanged,
                     context, [button, id, label](GuiShortcutId changed_id)
    {
        if (changed_id != id)
            return;

        button->setText(QStringLiteral("[%1] %2").arg(
            guiShortcutPresentation(id), label));
        button->setShortcut(guiShortcutRegistry().keySequence(id));
    });
}

// Label symbol boxes of the editor's icons (MapEntityPixmapRenderer rects at
// the editor's entity width): nodes bottom-anchored on their position,
// pumps and valves centred on the link's middle, pipes none. Memoized per
// entity type and width (the labels ask once per on-screen entity).
class EditorLabelSymbols
{
public:
    MapBgfxHudRect node(InfrastructureEntity entity_type, int width)
    {
        return rect(entity_type, width, false);
    }
    MapBgfxHudRect link(InfrastructureEntity entity_type, int width)
    {
        if (entity_type == InfrastructureEntity::Pipe)
            return MapBgfxHudRect();
        return rect(entity_type, width, true);
    }

private:
    MapBgfxHudRect rect(InfrastructureEntity entity_type, int width, bool centered)
    {
        const quint64 key = (quint64(quint8(entity_type)) << 33) | (quint64(centered) << 32)
            | quint64(quint32(width));
        const QHash<quint64, MapBgfxHudRect>::const_iterator found = this->rects.constFind(key);
        if (found != this->rects.cend())
            return found.value();
        const QString path = MapEntityPixmapRenderer::pixmapPathForEntity(entity_type);
        const QRectF bounds = centered
            ? this->metrics.centeredRect(QPointF(0.0, 0.0), path, width)
            : this->metrics.bottomAnchoredRect(QPointF(0.0, 0.0), path, width);
        const MapBgfxHudRect result{bounds.x(), bounds.y(), bounds.width(), bounds.height()};
        this->rects.insert(key, result);
        return result;
    }

    MapEntityPixmapRenderer metrics;
    QHash<quint64, MapBgfxHudRect> rects;
};
}


MapEditorContainer::MapEditorContainer(MapModel *map_model, MapTileRepository *tile_repository,
                                       MapTerrainRepository *terrain_repository,
                                       HydraulicData *hydraulic_data, GpsProvider *gps,
                                       EntityInspectorDock *map_inspector, QWidget *parent)
    : QWidget{parent},
    hydraulic_data( hydraulic_data ),
    gps( gps ),
    map_inspector( map_inspector ),
    map_model( map_model ),
    tile_repository( tile_repository ),
    terrain_repository( terrain_repository ),
    map_stack( new QWidget(this) ),
    map_stack_layout( new QStackedLayout(this->map_stack) ),
    map( new MapWidget(this->map_model, this->tile_repository, this->gps, this->map_stack) ),
    map_canvas( new MapCanvasWidget(this->map_model, this->map, this->hydraulic_data, this->map_stack) ),
    renderer_status( new QLabel(this->map_stack) ),
    editor_controller( new MapEditorController(this->map_model, this->map_canvas->mapCanvasEntities(), this) ),
    map_menu( new MapEditorMenuWidget(this->map, this->map_canvas, this->editor_controller,
                                      this->terrain_repository, CanvasMode::Edit, this) ),
    layout( new QHBoxLayout(this) )
{
    this->map_canvas->setEditorController(this->editor_controller);

    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    this->map_menu_scroll = scroll_controls;
    scroll_controls->setMinimumWidth(Sizes::SidebarMapEditLeftWidth);
    scroll_controls->setMaximumWidth(Sizes::SidebarMapEditLeftWidth);
    scroll_controls->setWidgetResizable(true);
    scroll_controls->setFrameShape(QFrame::NoFrame);
    scroll_controls->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_controls->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    scroll_controls->setWidget(this->map_menu);
    
    // Stack (StackAll, all at the map's size): MapWidget (navigation owner,
    // paints only its background), the canvas (editor input, paints
    // nothing), the bgfx surface and the status label.
    this->map_stack_layout->setContentsMargins(0, 0, 0, 0);
    this->map_stack_layout->setSpacing(0);
    this->map_stack_layout->setStackingMode(QStackedLayout::StackAll);
    
    this->map_stack_layout->addWidget(this->map);
    this->map_stack_layout->addWidget(this->map_canvas);
    this->map_stack_layout->setCurrentWidget(this->map_canvas);
    // Text only, never an opaque cover (the monitor's rule): the surface
    // must keep receiving paint/expose events to present its first frame.
    this->renderer_status->setAlignment(Qt::AlignCenter);
    this->renderer_status->setWordWrap(true);
    this->renderer_status->setAutoFillBackground(false);
    this->renderer_status->setAttribute(Qt::WA_TranslucentBackground);
    this->renderer_status->setAttribute(Qt::WA_TransparentForMouseEvents);
    this->map_stack_layout->addWidget(this->renderer_status);
    // HUD routing and the map fullscreen window's close button.
    this->map_canvas->installEventFilter(this);
    this->map_stack->installEventFilter(this);
    setupBgfxEditor();
    
    this->layout->addWidget(scroll_controls);
    this->layout->addWidget(this->map_stack);
    
    connect(this->map_menu, &MapEditorMenuWidget::signalSlideOpacityChanged,
            this->map_canvas, &MapCanvasWidget::setBackgroundOpacity);
    connect(this->map_menu->mapNavigationWidget(), &MapNavigationWidget::signalIconSizeChanged,
            this->map_canvas, &MapCanvasWidget::setIconSizePercent);
    // The opacity slider follows whether there is a network to show:
    // leftmost without one, NetworkOpacityPercent once one is loaded, left
    // again when it is deleted (MapNavigationWidget::setNetworkPresent()).
    const std::function<void()> sync_network_present = [this]()
    {
        const NetworkRenderSnapshot &snapshot = this->hydraulic_data->networkRenderSnapshot();
        this->map_menu->mapNavigationWidget()->setNetworkPresent(
            !snapshot.nodes.isEmpty() || !snapshot.links.isEmpty());
    };
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, sync_network_present);
    connect(this->hydraulic_data, &HydraulicData::signalNetworkGeometryChanged, this,
            [sync_network_present](quint64) { sync_network_present(); });
    sync_network_present();
    connect(this->map_menu, &MapEditorMenuWidget::signalMapEditorGuideVisibilityChanged, this, &MapEditorContainer::signalMapEditorGuideVisibilityChanged);
    connect(this->map_menu, &MapEditorMenuWidget::signalEditNetworkSectionActive, this, &MapEditorContainer::signalEditNetworkSectionActive);
    
    this->map_canvas->setFocusPolicy(Qt::StrongFocus);
    QTimer::singleShot(0, this->map_canvas, [this]()
    {
        this->map_canvas->setFocus(Qt::OtherFocusReason);
    });
}

MapEditorContainer::~MapEditorContainer()
{
    // The resolvers capture the bgfx surface (the monitor's destructor).
    this->map->setRenderSurfaceScreenCoordinateResolver(MapWidget::ScreenCoordinateResolver());
    this->map->setRenderSurfaceGlobeSurfacePointResolver(MapWidget::GlobeSurfacePointResolver());
    this->map->setCachedTerrainElevationResolver(MapWidget::CachedTerrainElevationResolver());
    this->editor_controller->setGlobeCoordinateResolver(MapEditorController::ScreenCoordinateResolver());
}

void MapEditorContainer::hideEvent(QHideEvent *event)
{
    // The main navigation switched to another page (not a minimized
    // window: spontaneous): the cache sections' selections are left behind
    // with the tool.
    if (!event->spontaneous())
        this->map_menu->clearCacheSelections();
    QWidget::hideEvent(event);
}

void MapEditorContainer::showRendererStatus(const QString &text)
{
    this->renderer_status->setText(text);
    this->renderer_status->show();
    this->map_stack_layout->setCurrentWidget(this->renderer_status);
    this->renderer_status->raise();
}

void MapEditorContainer::connectRenderSurfaceSync()
{
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded,
            this, &MapEditorContainer::syncRenderSurfaceNetworkSnapshot);
    connect(this->hydraulic_data, &HydraulicData::signalNetworkGeometryChanged,
            this, [this](quint64)
    {
        syncRenderSurfaceNetworkSnapshot();
    });
    // Renames (no geometry change): the labels' IDs follow the snapshot.
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged,
            this, [this](InfrastructureEntity, const QUuid &)
    {
        if (this->bgfx_surface != nullptr)
            this->bgfx_surface->setNetworkSnapshot(this->hydraulic_data->networkRenderSnapshot());
    });
    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged,
            this, [this](InfrastructureEntity, const QUuid &)
    {
        if (this->bgfx_surface != nullptr)
            this->bgfx_surface->setNetworkSnapshot(this->hydraulic_data->networkRenderSnapshot());
    });
    connect(this->hydraulic_data, &HydraulicData::signalDemandPointChanged,
            this, [this](const QUuid &)
    {
        if (this->bgfx_surface != nullptr)
            this->bgfx_surface->setNetworkSnapshot(this->hydraulic_data->networkRenderSnapshot());
    });
    connect(this->map_canvas->mapCanvasEntities(),
            &MapCanvasEntities::signalVisualStateChanged,
            this, [this](quint64)
    {
        syncRenderSurfaceEditorState();
    });
    connect(this->editor_controller, &MapEditorController::signalStateChanged,
            this, [this]
    {
        syncRenderSurfaceEditorState();
        this->map_canvas->requestRenderUpdate();
    });
    // No symbology work on zoom changes: the editor's GPU network has fixed
    // pixel sizes and no GPU icons (the overlay draws them), so the colour
    // stream never depends on the zoom.
}

void MapEditorContainer::syncRenderSurfaceNetworkSnapshot()
{
    if (this->bgfx_surface == nullptr || this->hydraulic_data == nullptr)
        return;

    this->bgfx_surface->setNetworkSnapshot(this->hydraulic_data->networkRenderSnapshot());
    applyRenderSurfaceEditorSymbology();
    syncRenderSurfaceEditorState();
}

void MapEditorContainer::syncRenderSurfaceEditorState()
{
    if (this->bgfx_surface == nullptr || this->hydraulic_data == nullptr)
        return;

    // Called on every editor state change (pointer moves included) and on
    // every visual state change, also while the editor is hidden (synced map
    // movement): build the full visual state only when a move is involved.
    if (!this->map_canvas->mapCanvasEntities()->isMoveActive()
        && this->render_move_session_id == 0 && !this->render_full_network_move)
        return;

    const MapEditorVisualState state = this->map_canvas->visualState();
    const NetworkRenderSnapshot &snapshot = this->hydraulic_data->networkRenderSnapshot();

    if (!state.move.active)
    {
        if (this->render_move_session_id != 0 || this->render_full_network_move)
        {
            this->render_move_session_id = 0;
            this->render_full_network_move = false;
            this->render_move_start_mouse_position = QPointF();
            this->render_move_translation = QPointF();
            this->bgfx_surface->setNetworkScreenTranslation(QPointF());
            this->bgfx_surface->setHiddenEntityUuids(QSet<QUuid>());
        }
        return;
    }

    // A move session hides the dragged entities in the GPU network (the
    // overlay draws them moving) - or, when the whole network moves, shifts
    // the GPU network instead (camera offset, no upload).
    if (this->render_move_session_id != state.move.session_id)
    {
        this->render_move_session_id = state.move.session_id;
        this->render_move_start_mouse_position = state.placement.mouse_position;

        QSet<QUuid> hidden_entity_uuids;
        hidden_entity_uuids.reserve(state.move.markers.size() + state.move.links.size());
        for (const MapEditorDynamicMarkerVisualState &marker : state.move.markers)
            hidden_entity_uuids.insert(marker.uuid);
        for (const MapEditorDynamicLinkVisualState &link : state.move.links)
            hidden_entity_uuids.insert(link.uuid);

        this->render_full_network_move = !hidden_entity_uuids.isEmpty();
        if (this->render_full_network_move)
        {
            for (const NetworkRenderNode &node : snapshot.nodes)
            {
                if (!hidden_entity_uuids.contains(node.uuid))
                {
                    this->render_full_network_move = false;
                    break;
                }
            }
        }
        if (this->render_full_network_move)
        {
            for (const NetworkRenderLink &link : snapshot.links)
            {
                if (!hidden_entity_uuids.contains(link.uuid))
                {
                    this->render_full_network_move = false;
                    break;
                }
            }
        }

        if (this->render_full_network_move)
            this->bgfx_surface->setHiddenEntityUuids(QSet<QUuid>());
        else
            this->bgfx_surface->setHiddenEntityUuids(hidden_entity_uuids);
    }

    if (this->render_full_network_move)
    {
        const QPointF translation_pixels =
            state.placement.mouse_position - this->render_move_start_mouse_position;
        this->render_move_translation = translation_pixels;
        this->bgfx_surface->setNetworkScreenTranslation(translation_pixels);
        return;
    }

    this->render_move_translation = QPointF();
    this->bgfx_surface->setNetworkScreenTranslation(QPointF());
}

void MapEditorContainer::applyRenderSurfaceEditorSymbology()
{
    if (this->bgfx_surface == nullptr || this->hydraulic_data == nullptr)
        return;

    // The editor look: black pipes and node dots, brown device links, no
    // GPU icons in 2D (the overlay draws the full-colour editor icons), no
    // flow. 3D has no editor overlay: there the globe network draws its
    // icons (re-applied on every view mode switch).
    MapNetworkRenderSymbology symbology;
    symbology.node_size_unit = NetworkSymbologySizeUnit::Pixels;
    symbology.node_size_px = 10;
    symbology.node_size_m = NetworkSymbologyDefaultNodeSizeM;
    // No GPU icons (show_icons false): their size is irrelevant here.
    symbology.icon_size_unit = NetworkSymbologySizeUnit::Pixels;
    symbology.icon_size_px = NetworkSymbologyDefaultIconSizePx;
    symbology.icon_size_m = NetworkSymbologyDefaultIconSizeM;
    symbology.show_icons = this->map_model->viewMode() == MapViewMode::Globe;
    symbology.link_thickness_unit = NetworkSymbologySizeUnit::Pixels;
    symbology.link_thickness_px = NetworkSymbologyDefaultLinkThicknessPx;
    symbology.link_thickness_m = NetworkSymbologyDefaultLinkThicknessM;
    symbology.show_flow_direction = false;
    symbology.visual_heatmap = VisualHeatmap::None;

    // Visual None everywhere: the base colours are the editor look (the
    // GPU colours every entity from its class, no per-entity data).
    symbology.node_base_color = qRgb(0, 0, 0);
    symbology.pipe_base_color = qRgb(0, 0, 0);
    symbology.device_link_base_color = qRgb(139, 90, 43);

    this->bgfx_surface->setSymbology(symbology);
}

void MapEditorContainer::setupBgfxEditor()
{
    // Map editor on bgfx (roadmap Phase 2). bgfx draws imagery, the static
    // network in the editor look, every editor overlay, map labels and the
    // HUD; MapCanvasWidget stays the input owner (editor controller first,
    // then MapWidget navigation) and paints nothing.
    MapBgfxSurface *surface = new MapBgfxSurface(this->map_model, this->map_stack);
    // The globe is offered (MapBgfxSurface's default): 3D shows the terrain
    // and the network; of the editor, only the cache sections' tile
    // selection works there (see the class comment).
    surface->setObjectName(QStringLiteral("aowis-bgfx-editor-surface"));
    this->bgfx_surface = surface;
    // Editor HUD: crosshair, scale bar, the fullscreen button with the
    // 2D/3D switch (3D is offered once the bgfx globe initialized), the pan
    // joystick and the zoom buttons; in 3D the globe HUD (compass, camera
    // sliders, terrain toggles) as in the monitor.
    surface->setHudViewSwitchVisible(true);
    this->bgfx_globe_hud = new MapBgfxGlobeHudController(this->map_model, this);
    surface->setHudActionHandler([this](const MapBgfxHud::Command &command)
    {
        switch (command.action)
        {
            case MapBgfxHud::Action::GlobeSlider:
            case MapBgfxHud::Action::GlobeSliderReset:
            case MapBgfxHud::Action::CompassDrag:
            case MapBgfxHud::Action::CompassWheel:
                this->bgfx_globe_hud->handleCommand(command);
                break;
            case MapBgfxHud::Action::JoystickDrag:
                this->map->beginJoystickPan(command.position);
                break;
            default:
                handleBgfxHudAction(static_cast<int>(command.action), command.value);
                break;
        }
    });
    // The joystick's knob follows MapWidget's captured drag, its arrows also
    // the held keyboard pan keys.
    connect(this->map, &MapWidget::joystickChanged, surface,
            [surface](QPointF deflection, bool active) { surface->setHudJoystick(deflection, active); });
    connect(this->map, &MapWidget::keyboardPanChanged, surface,
            [surface](QPointF direction) { surface->setHudKeyboardPan(direction); });
    // Keys stay on the canvas (editor keys, window shortcuts of the tools).
    surface->setKeyboardFocusWidget(this->map_canvas);
    // Node dots at their true positions, exactly under the overlay icons and
    // the editor's hit testing (the monitor spreads coincident nodes).
    surface->setNodeDeclutteringEnabled(false);

    // Desktop: the embedded native window receives the pointer; deliver
    // every event to the canvas as if it had received it (same geometry),
    // where this container's event filter lets the HUD see it first.
    // WASM: the bgfx canvas has pointer-events:none, the canvas widget on
    // top of the Qt stack receives them directly (same filter).
    const std::function<bool(QMouseEvent *)> to_canvas = [this](QMouseEvent *event)
    {
        return QCoreApplication::sendEvent(this->map_canvas, event);
    };
    surface->setNavigationMouseHandlers(to_canvas, to_canvas, to_canvas);
    surface->setNavigationHandlers(
        [this](QKeyEvent *event)
        {
            QCoreApplication::sendEvent(this->map_canvas, event);
            return event->isAccepted();
        },
        [this](QKeyEvent *event)
        {
            QCoreApplication::sendEvent(this->map_canvas, event);
            return event->isAccepted();
        },
        [this]() { this->map->clearKeyboardPanInput(); });
    surface->setNavigationWheelHandler([this](QWheelEvent *event) { this->map->handleWheelEvent(event); });
    surface->setNavigationFrameHandler(
        [this](double elapsed_seconds) { this->map->advanceNavigationFrame(elapsed_seconds); });

    surface->setTileRepository(this->tile_repository);
    // DEM tiles for the globe's relief, and terrain-aware navigation and
    // picking as in the monitor: pointer coordinates with DEM altitude,
    // globe pans on the drawn terrain, the tile selection's corners on it.
    surface->setTerrainRepository(this->terrain_repository);
    this->map->setRenderSurfaceScreenCoordinateResolver(
        [surface](const QPointF &screen_position, CoordinateWGS84 *coordinate)
    {
        return surface->terrainCoordinateAtScreen(screen_position, coordinate, true);
    });
    this->map->setCachedTerrainElevationResolver(
        [surface](const CoordinateWGS84 &coordinate, double *elevation_m)
    {
        return surface->cachedTerrainElevationAtCoordinate(coordinate, elevation_m);
    });
    this->map->setRenderSurfaceGlobeSurfacePointResolver(
        [surface](const QPointF &screen_position, GeoWgs84Ellipsoid::EcefPositionD *point)
    {
        return surface->globeSurfacePointAtScreen(screen_position, point);
    });
    this->editor_controller->setGlobeCoordinateResolver(
        [surface](const QPointF &screen_position, CoordinateWGS84 *coordinate)
    {
        return surface->terrainCoordinateAtScreen(screen_position, coordinate, true);
    });
    // GPU icons on the globe only (applyRenderSurfaceEditorSymbology()).
    // The 2D and the globe overlay share the surface's overlay canvas: the
    // 2D builder must not reuse (pan) a canvas the globe frames rebuilt.
    connect(this->map_model, &MapModel::viewModeChanged, this, [this](MapViewMode)
    {
        applyRenderSurfaceEditorSymbology();
        this->bgfx_visual_state_dirty = true;
    });
    surface->setBackgroundOpacity(this->map_canvas->backgroundOpacity());
    connect(this->map_menu, &MapEditorMenuWidget::signalSlideOpacityChanged,
            surface, [surface](int opacity) { surface->setBackgroundOpacity(opacity); });

#ifndef Q_OS_WASM
    // GPS position dot on the HUD (formerly painted by MapWidget's CPU map;
    // the monitor's wiring).
    if (this->gps != nullptr)
    {
        connect(this->gps, &GpsProvider::positionChanged, surface,
                [surface](const QGeoPositionInfo &info)
        {
            const QGeoCoordinate coordinate = info.coordinate();
            surface->setHudGpsPosition(info.isValid() && coordinate.isValid(),
                                       coordinate.longitude(), coordinate.latitude());
        });
        connect(this->gps, &GpsProvider::gpsDisconnected, surface, [surface]()
        {
            surface->setHudGpsPosition(false, 0.0, 0.0);
        });
    }
#endif

    // Map labels: IDs from the left menu's switches, placed around the
    // editor's icons (the GPU network only has dots here).
    MapNavigationWidget *navigation = this->map_menu->mapNavigationWidget();
    surface->setLabelSettings(navigation->labelSettings());
    connect(navigation, &MapNavigationWidget::signalLabelSettingsChanged,
            surface, [surface](const MapLabelSettings &settings)
    {
        surface->setLabelSettings(settings);
    });
    const std::shared_ptr<EditorLabelSymbols> label_symbols = std::make_shared<EditorLabelSymbols>();
    surface->setLabelSymbolRects(
        [this, label_symbols](InfrastructureEntity entity_type)
        {
            return label_symbols->node(entity_type, qMax(1, this->bgfx_visual_state.entity_width));
        },
        [this, label_symbols](InfrastructureEntity entity_type)
        {
            return label_symbols->link(entity_type, qMax(1, this->bgfx_visual_state.entity_width));
        });

    // Editor overlay: built per frame from the cached visual state.
    const std::shared_ptr<MapBgfxEditorIcons> icons = std::make_shared<MapBgfxEditorIcons>();
    const std::shared_ptr<MapBgfxEditorOverlay> overlay = std::make_shared<MapBgfxEditorOverlay>();
    overlay->setIconProvider(
        [icons](MapBgfxHudTextCache *cache, InfrastructureEntity entity, const QString &path,
                int width, MapBgfxEditorOverlay::Highlight highlight)
        {
            return icons->icon(cache, entity, path, width, highlight);
        });
    // The last built overlay frame. While the editor's content is unchanged
    // it is drawn again as is (an idle map) or translated (a pure pan: built
    // with a margin around the viewport, rebuilt only when the pan leaves
    // it; map_bgfx_pan_reuse.h). Content with screen-anchored parts (pointer
    // rubber bands, the floating marker, the selection rectangle) and
    // interactive moves get no margin and are rebuilt on every camera change
    // as before.
    struct OverlayFrame
    {
        MapBgfxPanReuse pan;
        // The last build had no screen-anchored parts.
        bool pannable = false;
        quint64 atlas_generation = 0;
        // A build with margin exceeded the draw-list budget at this zoom
        // (huge networks): no margin until the zoom changes.
        double margin_overflow_world_pixels = 0.0;
        // The tile selection the last build drew (it is not part of the
        // visual state): a changed selection is never reused.
        quint64 tile_selection_revision = 0;
    };
    const std::shared_ptr<OverlayFrame> overlay_frame = std::make_shared<OverlayFrame>();
    surface->setOverlayBuilder(
        [this, overlay, overlay_frame](MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                                       const MapBgfx2d::Camera &camera, const QSizeF &viewport)
        {
            const double ratio = cache->devicePixelRatio();
            // The camera-dirty work below only refreshes the pointer's map
            // coordinate, which pannable content does not show.
            const bool content_unchanged = !this->bgfx_visual_state_dirty
                && (overlay_frame->pannable || !this->bgfx_visual_camera_dirty)
                && overlay_frame->atlas_generation == cache->generation()
                && overlay_frame->tile_selection_revision
                       == this->editor_controller->tileSelectionRevision();
            QPointF offset;
            if (content_unchanged && overlay_frame->pan.reuse(camera, viewport, ratio, &offset))
                return offset;
            // Full rebuild only when the editor's content changed; camera
            // motion only moves the pointer's map coordinate (the visual
            // state is O(demand points) and must not run every frame).
            if (this->bgfx_visual_state_dirty)
            {
                this->bgfx_visual_state = this->map_canvas->visualState();
                this->bgfx_visual_state_dirty = false;
                this->bgfx_visual_camera_dirty = false;
            }
            else if (this->bgfx_visual_camera_dirty)
            {
                this->bgfx_visual_camera_dirty = false;
                if (this->bgfx_visual_state.placement.mouse_coordinate_wgs84_valid)
                {
                    this->bgfx_visual_state.placement.mouse_coordinate_wgs84 =
                        this->map_model->wgs84FromScreen(
                            this->bgfx_visual_state.placement.mouse_position.toPoint(),
                            this->map_canvas->size());
                }
            }
            const MapEditorViewportRenderState viewport_state =
                this->map_canvas->viewportRenderState();
            // Screen-anchored or changing with every pointer move: no margin
            // (it would only make each of those rebuilds larger).
            const bool pannable = !this->bgfx_visual_state.placement.creating
                && !this->bgfx_visual_state.placement.floating_marker_visible
                && !this->bgfx_visual_state.move.active
                && !viewport_state.rectangle_selection_visible
                && !this->render_full_network_move
                && overlay_frame->margin_overflow_world_pixels != camera.world_pixels;
            MapBgfxEditorOverlay::Inputs inputs;
            inputs.snapshot = &this->hydraulic_data->networkRenderSnapshot();
            inputs.visual_state = &this->bgfx_visual_state;
            inputs.viewport_state = &viewport_state;
            inputs.full_network_move = this->render_full_network_move;
            inputs.full_network_move_translation = this->render_move_translation;
            MapBgfxPanReuse::Build build = overlay_frame->pan.begin(camera, viewport, ratio, pannable);
            inputs.viewport = build.viewport;
            inputs.camera = build.camera;
            overlay->build(canvas, cache, inputs);
            if (canvas->overflowed() && build.margin > 0.0)
            {
                // The margin pushed a huge network over the draw-list budget:
                // this frame exactly, and per pan frame as before at this zoom.
                overlay_frame->margin_overflow_world_pixels = camera.world_pixels;
                build = overlay_frame->pan.begin(camera, viewport, ratio, false);
                inputs.viewport = build.viewport;
                inputs.camera = build.camera;
                overlay->build(canvas, cache, inputs);
            }
            overlay_frame->pannable = pannable;
            overlay_frame->atlas_generation = cache->generation();
            overlay_frame->tile_selection_revision = this->editor_controller->tileSelectionRevision();
            return build.offset;
        });
    // Globe: the cache sections' tile selection on the drawn surface, built
    // every globe frame (cheap: a bounded lattice, no network content).
    surface->setGlobeOverlayBuilder(
        [this, overlay](MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                        const MapBgfxSurface::GlobeProjector &projector, const QSizeF &)
        {
            const MapEditorViewportRenderState viewport_state =
                this->map_canvas->viewportRenderState();
            overlay->buildGlobe(canvas, cache, viewport_state, projector);
        });
    connect(this->map_canvas, &MapCanvasWidget::signalRenderUpdateRequested, this, [this]()
    {
        this->bgfx_visual_state_dirty = true;
        // The surface renders on demand; the overlay's content is not one of
        // its own inputs.
        if (this->bgfx_surface != nullptr)
            this->bgfx_surface->requestRedraw();
    });
    connect(this->map_canvas, &MapCanvasWidget::signalCameraChanged, this, [this]()
    {
        this->bgfx_visual_camera_dirty = true;
    });
    // The native window shows its own cursor (desktop); the tool's cursor
    // unless a HUD button is under the pointer.
    connect(this->editor_controller, &MapEditorController::signalStateChanged, this, [this]()
    {
        applyEditorCursor();
    });

    // Map fullscreen shortcut (the monitor's "Toggle map fullscreen"),
    // enabled while bgfx presents frames and no shortcut is being captured.
    // Its parent is the map stack, so only the visible map's shortcut fires.
    this->bgfx_fullscreen_shortcut = new QShortcut(
        guiShortcutRegistry().keySequence(GuiShortcutId::MapMonitorFullscreen), this->map_stack);
    this->bgfx_fullscreen_shortcut->setContext(Qt::ApplicationShortcut);
    this->bgfx_fullscreen_shortcut->setAutoRepeat(false);
    this->bgfx_fullscreen_shortcut->setEnabled(false);
    connect(this->bgfx_fullscreen_shortcut, &QShortcut::activated, this, [this]()
    {
        if (!this->bgfx_editor_active)
            return;
        if (!this->bgfx_fullscreen_active && !isVisible())
            return;
        setBgfxFullscreen(!this->bgfx_fullscreen_active);
    });
    connect(&guiShortcutRegistry(), &GuiShortcutRegistry::shortcutChanged, this,
            [this](GuiShortcutId id)
    {
        if (id != GuiShortcutId::MapMonitorFullscreen || this->bgfx_fullscreen_shortcut == nullptr)
            return;
        this->bgfx_fullscreen_shortcut->setKey(
            guiShortcutRegistry().keySequence(GuiShortcutId::MapMonitorFullscreen));
    });
    connect(&guiShortcutRegistry(), &GuiShortcutRegistry::shortcutCaptureActiveChanged,
            this, [this](bool active)
    {
        if (this->bgfx_fullscreen_shortcut != nullptr)
            this->bgfx_fullscreen_shortcut->setEnabled(this->bgfx_editor_active && !active);
    });

    syncRenderSurfaceNetworkSnapshot();
    connectRenderSurfaceSync();

    connect(surface, &MapBgfxSurface::signalRendererReady, this, [this, surface]()
    {
        this->bgfx_editor_active = true;
        this->renderer_status->hide();
        // bgfx presents the frames, so it is the only navigation clock.
        this->map->setNavigationFrameDriver(true);
        if (this->bgfx_fullscreen_shortcut != nullptr)
        {
            this->bgfx_fullscreen_shortcut->setEnabled(
                !guiShortcutRegistry().shortcutCaptureActive());
        }
#ifndef Q_OS_WASM
        this->map_stack_layout->setCurrentWidget(surface);
        // The joystick's pointer lock (Wayland) also on the native window.
        this->map->setOrbitPointerLockWindow(surface->nativeInputWindow());
#else
        // The canvas widget stays on top of the Qt stack for the pointer.
        this->map_stack_layout->setCurrentWidget(this->map_canvas);
        this->map_canvas->raise();
#endif
        this->map_canvas->setFocus(Qt::OtherFocusReason);
        this->bgfx_visual_state_dirty = true;
        applyEditorCursor();
        qInfo() << "Editor map renderer: bgfx (" << surface->graphicsApiName()
                << ") with imagery, network, editor overlay, labels and HUD";
    });
    connect(surface, &MapBgfxSurface::signalRendererFailed, this,
            [this, surface](const QString &reason)
    {
        qWarning().noquote()
            << QStringLiteral("Editor bgfx surface failed; no map renderer is available: %1")
                   .arg(reason);
        if (this->bgfx_fullscreen_active)
            setBgfxFullscreen(false);
        if (this->bgfx_fullscreen_shortcut != nullptr)
            this->bgfx_fullscreen_shortcut->setEnabled(false);
#ifndef Q_OS_WASM
        this->map->setOrbitPointerLockWindow(nullptr);
#endif
        this->bgfx_editor_active = false;
        this->bgfx_hud_cursor = Qt::ArrowCursor;
        this->map->setNavigationFrameDriver(false);
        surface->hide();
        showRendererStatus(tr("The bgfx map renderer could not start:\n%1").arg(reason));
    });

    // The StackAll layout keeps the surface at the real map size; the
    // status label covers the start (text only, see above).
    this->map_stack_layout->addWidget(surface);
    surface->show();
    showRendererStatus(tr("Starting map renderer…"));
    // Startup watchdog (the monitor's): never an endless "Starting...".
    QTimer::singleShot(10000, surface, [this, surface]()
    {
        if (this->bgfx_editor_active || !surface->isVisible())
            return;
        const QString diagnostics = surface->startupDiagnostics();
        qWarning().noquote() << "bgfx map editor did not start within 10 s:" << diagnostics;
        showRendererStatus(tr("The bgfx map renderer has not presented a frame "
                              "after 10 s.\n%1").arg(diagnostics));
    });
}

void MapEditorContainer::applyEditorCursor()
{
    // A HUD button under the pointer wins; otherwise the tool's cursor.
    Qt::CursorShape shape = Qt::ArrowCursor;
    if (this->bgfx_hud_cursor != Qt::ArrowCursor)
    {
        shape = this->bgfx_hud_cursor;
    }
    else
    {
        const std::optional<Qt::CursorShape> tool_shape = this->editor_controller->cursorShape();
        if (tool_shape.has_value())
            shape = tool_shape.value();
    }
    // WASM: the canvas widget is under the pointer; desktop: the native
    // window (the canvas lies below it).
    if (shape != Qt::ArrowCursor)
        this->map_canvas->setCursor(shape);
    else
        this->map_canvas->unsetCursor();
    if (this->bgfx_surface != nullptr)
        this->bgfx_surface->setMapCursor(shape);
}

bool MapEditorContainer::routeBgfxHudEvent(QEvent *event)
{
    if (this->bgfx_surface == nullptr || !this->bgfx_editor_active
        || !this->bgfx_surface->isVisible())
    {
        return false;
    }
    switch (event->type())
    {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseMove:
        {
            QMouseEvent *mouse_event = static_cast<QMouseEvent *>(event);
            const MapBgfxSurface::HudPointer pointer =
                this->bgfx_surface->handleHudMouseEvent(mouse_event);
            if (event->type() == QEvent::MouseMove && pointer.cursor != this->bgfx_hud_cursor)
            {
                this->bgfx_hud_cursor = pointer.cursor;
                applyEditorCursor();
            }
            return pointer.consumed;
        }
        case QEvent::Wheel:
            // WASM (desktop routes the native window's wheel internally).
            return this->bgfx_surface->handleHudWheelEvent(static_cast<QWheelEvent *>(event));
        case QEvent::KeyPress:
            return this->bgfx_surface->handleHudKeyPress(static_cast<QKeyEvent *>(event));
        case QEvent::FocusOut:
            // Losing focus closes a HUD popup like a Qt popup.
            this->bgfx_surface->closeHudPopup();
            return false;
        default:
            return false;
    }
}

void MapEditorContainer::handleBgfxHudAction(int action, int value)
{
    switch (static_cast<MapBgfxHud::Action>(action))
    {
        case MapBgfxHud::Action::ViewMode2d:
            if (this->map_model->viewMode() != MapViewMode::TwoD)
                this->map_model->setViewMode(MapViewMode::TwoD, this->map->size());
            break;
        case MapBgfxHud::Action::ViewMode3d:
            // Offered (enabled) only when the globe surface initialized
            // (MapBgfxSurface::globeAvailable()).
            if (this->bgfx_surface != nullptr && this->bgfx_surface->globeAvailable()
                && this->map_model->viewMode() != MapViewMode::Globe)
                this->map_model->setViewMode(MapViewMode::Globe, this->map->size());
            break;
        // The globe HUD's toggles and underground combo (the monitor's).
        case MapBgfxHud::Action::ToggleWireframe:
            if (this->bgfx_surface != nullptr)
                this->bgfx_surface->setTerrainWireframeVisible(!this->bgfx_surface->terrainWireframeVisible());
            break;
        case MapBgfxHud::Action::ToggleMapTiles:
            if (this->bgfx_surface != nullptr)
                this->bgfx_surface->setMapTilesVisible(!this->bgfx_surface->mapTilesVisible());
            break;
        case MapBgfxHud::Action::ToggleHillshade:
            if (this->bgfx_surface != nullptr)
                this->bgfx_surface->setTerrainHillshadeVisible(!this->bgfx_surface->terrainHillshadeVisible());
            break;
        case MapBgfxHud::Action::UndergroundMode:
            if (this->bgfx_surface != nullptr)
                this->bgfx_surface->setUndergroundMode(static_cast<MapUndergroundMode>(value));
            break;
        case MapBgfxHud::Action::ToggleFullscreen:
            setBgfxFullscreen(!this->bgfx_fullscreen_active);
            break;
        case MapBgfxHud::Action::ZoomIn:
            this->map->zoomIn();
            break;
        case MapBgfxHud::Action::ZoomOut:
            this->map->zoomOut();
            break;
        default:
            // Downloads and the legend are monitor-only HUD parts.
            break;
    }
}

void MapEditorContainer::setBgfxFullscreen(bool fullscreen)
{
    if (fullscreen == this->bgfx_fullscreen_active)
        return;
    if (fullscreen && (!this->bgfx_editor_active || this->bgfx_surface == nullptr))
        return;

    // In place, as MapMonitorContainer::setBgfxFullscreen(): the main window
    // hides everything but this map; no reparenting, so bgfx keeps running.
    applyMapFullscreen(fullscreen);
    emit signalMapFullscreenRequested(fullscreen);
}

void MapEditorContainer::applyMapFullscreen(bool fullscreen)
{
    if (fullscreen == this->bgfx_fullscreen_active)
        return;

    this->bgfx_fullscreen_active = fullscreen;
    if (this->map_menu_scroll != nullptr)
        this->map_menu_scroll->setVisible(!fullscreen);
    if (this->bgfx_surface != nullptr)
        this->bgfx_surface->setHudFullscreen(fullscreen);
    // Keys belong to the canvas (editor keys; in fullscreen also the left
    // menu's button shortcuts, see eventFilter()).
    this->map_canvas->setFocus(Qt::OtherFocusReason);
}

bool MapEditorContainer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this->map_canvas)
    {
#ifdef Q_OS_WASM
        // A press on the map or a HUD element brings the keys back to the
        // canvas (desktop: MapBgfxSurface does it for its native window),
        // also when the HUD consumes the press.
        if (event->type() == QEvent::MouseButtonPress)
            this->map_canvas->setFocus(Qt::MouseFocusReason);
#endif
        // A joystick drag's release belongs to the map only: the editor
        // tool never saw its press (the HUD took it).
        if (event->type() == QEvent::MouseButtonRelease && this->map->joystickPanActive())
        {
            this->map->handleMouseReleaseEvent(static_cast<QMouseEvent *>(event));
            event->accept();
            return true;
        }
        // The HUD sits on top of the map: it sees pointer, wheel and Escape
        // first, and what it consumes never reaches the editor.
        if (routeBgfxHudEvent(event))
        {
            event->accept();
            return true;
        }
        // Fullscreen: the left menu is hidden, so its buttons' shortcuts
        // cannot fire (Qt ignores hidden widgets' shortcuts); the canvas' key
        // presses try them first - exactly what the shortcut would have done
        // before the key reached the canvas.
        if (event->type() == QEvent::KeyPress && this->bgfx_fullscreen_active
            && this->map_menu->triggerButtonShortcut(static_cast<QKeyEvent *>(event)))
        {
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}


MapWidget *MapEditorContainer::getMap()
{
    return this->map;
}
MapNavigationWidget *MapEditorContainer::mapNavigationWidget()
{
    return this->map_menu->mapNavigationWidget();
}

bool MapEditorContainer::isEditNetworkSectionActive() const
{
    return this->map_menu->isEditNetworkSectionActive();
}

void MapEditorContainer::setMapEditorGuideChecked(bool checked)
{
    this->map_menu->setMapEditorGuideChecked(checked);
}

MapEditorMenuWidget::MapEditorMenuWidget(MapWidget *map, MapCanvasWidget *map_canvas,
                                         MapEditorController *editor_controller,
                                         MapTerrainRepository *terrain_repository, CanvasMode mode,
                                         QWidget *parent)
    : QWidget{parent},
    mode( mode ),
    layout( new QVBoxLayout(this) ),
    map( map ),
    map_nav( new MapNavigationWidget(this->map, this->mode, map_canvas, this) ),
    map_canvas( map_canvas ),
    editor_controller( editor_controller ),
    terrain_repository( terrain_repository ),
    toolbox( new QToolBox(this) )
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    
    this->toolbox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    createToolboxEdit(this->toolbox);
    createToolboxCache(this->toolbox);
    createToolboxTerrain(this->toolbox);
    
    this->layout->addWidget(this->map_nav);
    this->layout->addWidget(this->toolbox);
    this->layout->addStretch();
    
    connect(this->toolbox, &QToolBox::currentChanged, this, &MapEditorMenuWidget::setToolboxMode);
    connect(this->map_nav, &MapNavigationWidget::signalSlideOpacityChanged, this, &MapEditorMenuWidget::signalSlideOpacityChanged);
    connect(this->map->model(), &MapModel::viewModeChanged, this, &MapEditorMenuWidget::applyViewMode);
    if (this->terrain_repository != nullptr)
    {
        connect(this->terrain_repository, &MapTerrainRepository::signalTerrainDatasetChanged,
                this, &MapEditorMenuWidget::applyTerrainDataset);
    }
    
    setToolboxMode(this->toolbox->currentIndex());
    applyTerrainDataset();
    applyViewMode(this->map->model()->viewMode());
}

void MapEditorMenuWidget::applyViewMode(MapViewMode view_mode)
{
    const bool globe = view_mode == MapViewMode::Globe;
    // A tool in use ends with the switch (the controller stops placing):
    // back to Select, so nothing looks armed when 2D returns.
    if (globe && this->button_radio_select != nullptr)
        this->button_radio_select->setChecked(true);
    if (this->edit_tools != nullptr)
        this->edit_tools->setEnabled(!globe);
    if (this->label_edit_globe_hint != nullptr)
        this->label_edit_globe_hint->setVisible(globe);
    updateToolboxHeight(this->toolbox->currentIndex());
}

void MapEditorMenuWidget::applyTerrainDataset()
{
    const MapTerrainDatasetInfo *dataset =
        this->terrain_repository != nullptr ? this->terrain_repository->activeDataset() : nullptr;
    if (dataset == nullptr)
    {
        this->label_terrain_dataset->setText(
            "<i>No terrain dataset yet:</i> terrain tiles can be deleted once the map "
            "server's terrain service described its dataset.");
        this->editor_controller->setTerrainZoomRange(0, -1);
        this->spin_terrain_zoom_from->setEnabled(false);
        this->spin_terrain_zoom_to->setEnabled(false);
        this->checkbox_terrain_source->setEnabled(false);
        refreshTerrainDeleteButton();
        return;
    }

    this->spin_terrain_zoom_from->setEnabled(true);
    this->spin_terrain_zoom_to->setEnabled(true);
    this->checkbox_terrain_source->setEnabled(true);

    this->label_terrain_dataset->setText(
        QString("Dataset <b>%1</b> (%2, %3 m), terrain zoom levels %4 to %5.")
            .arg(dataset->dataset.toHtmlEscaped(), dataset->provider.toHtmlEscaped())
            .arg(dataset->nominal_resolution_m)
            .arg(dataset->minimum_zoom)
            .arg(dataset->maximum_zoom));
    this->editor_controller->setTerrainZoomRange(dataset->minimum_zoom, dataset->maximum_zoom);
    // "From" at the terrain level the map draws now, "to" at the dataset's
    // native detail (QSpinBox clamps both into the new range).
    this->spin_terrain_zoom_from->setRange(dataset->minimum_zoom, dataset->maximum_zoom);
    this->spin_terrain_zoom_from->setValue(
        this->editor_controller->imageryZoom(this->map_canvas->size()));
    this->spin_terrain_zoom_to->setRange(this->spin_terrain_zoom_from->value(), dataset->maximum_zoom);
    this->spin_terrain_zoom_to->setValue(dataset->maximum_zoom);
    refreshTerrainDeleteButton();
}

void MapEditorMenuWidget::refreshTerrainDeleteButton()
{
    if (this->button_terrain_delete == nullptr)
        return;
    const bool dataset_known =
        this->terrain_repository != nullptr && this->terrain_repository->activeDataset() != nullptr;
    this->button_terrain_delete->setEnabled(
        this->terrain_selection_valid && dataset_known && !this->terrain_deletion_running);
}

void MapEditorMenuWidget::updateToolboxHeight(int index)
{
    QTimer::singleShot(0, this, [this, index]
    {
        if (index != this->toolbox->currentIndex())
            return;

        QWidget *page = this->toolbox->widget(index);
        if (page == nullptr)
            return;

        const int page_width = qMax(1, this->toolbox->contentsRect().width());
        int page_height = page->sizeHint().height();
        if (page->layout() != nullptr)
        {
            page->layout()->invalidate();
            page->layout()->activate();
            if (page->layout()->hasHeightForWidth())
                page_height = page->layout()->totalHeightForWidth(page_width);
        }
        else if (page->hasHeightForWidth())
        {
            page_height = page->heightForWidth(page_width);
        }

        int tab_height = 0;
        const QList<QAbstractButton *> tab_buttons = this->toolbox->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly);
        for (QAbstractButton *button : tab_buttons)
            tab_height += button->sizeHint().height();

        if (tab_height == 0)
            tab_height = qMax(0, this->toolbox->height() - page->height());

        const QMargins margins = this->toolbox->contentsMargins();
        this->toolbox->setFixedHeight(margins.top() + tab_height + page_height + margins.bottom() + 16);
        this->toolbox->updateGeometry();
        updateGeometry();
    });
}

MapNavigationWidget *MapEditorMenuWidget::mapNavigationWidget()
{
    return this->map_nav;
}

bool MapEditorMenuWidget::isEditNetworkSectionActive() const
{
    return this->toolbox->currentIndex() == this->toolbox_edit_index;
}

bool MapEditorMenuWidget::triggerButtonShortcut(const QKeyEvent *event)
{
    if (event == nullptr || event->isAutoRepeat())
        return false;
    const QKeySequence pressed(event->keyCombination());
    const QList<QAbstractButton *> buttons = findChildren<QAbstractButton *>();
    for (QAbstractButton *button : buttons)
    {
        // What QShortcutMap would accept in the main window: an enabled
        // button whose own page is shown.
        if (!button->isEnabled() || !button->isVisibleTo(this) || button->shortcut().isEmpty())
            continue;
        if (button->shortcut().matches(pressed) != QKeySequence::ExactMatch)
            continue;
        button->click();
        return true;
    }
    return false;
}

void MapEditorMenuWidget::clearCacheSelections()
{
    this->editor_controller->clearTileSelectionOverlay();
    this->button_tiles_delete->setChecked(false);
    this->button_tiles_delete->setEnabled(false);
    this->terrain_selection_valid = false;
    refreshTerrainDeleteButton();
}

void MapEditorMenuWidget::setMapEditorGuideChecked(bool checked)
{
    this->checkbox_map_editor_guide->setChecked(checked);
}

void MapEditorMenuWidget::createToolboxCache(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QGridLayout *grid = new QGridLayout(wgt);
    
    QLabel *label_explanation_rectangle = new QLabel("<b>Select a region first:</b> right-click and drag on the map (2D or 3D).", this);
    label_explanation_rectangle->setWordWrap(true);
    
    QLabel *label_explanation_spinners = new QLabel("<b>Select the zoom level range</b> (from - to) for the tiles you want to delete:");
    label_explanation_spinners->setWordWrap(true);
    QLabel *label_explanation_actions = new QLabel("Then delete the cached tiles for the selected area and zoom levels:", this);
    label_explanation_actions->setWordWrap(true);
    
    this->spin_zoom_from = new QSpinBox();
    this->spin_zoom_from->setRange(1, 19);
    
    this->spin_zoom_to = new QSpinBox();
    this->spin_zoom_to->setRange(2, 19);
    this->spin_zoom_to->setValue(19);
    connect(this->spin_zoom_from, &QSpinBox::valueChanged, this, [this]
    {
        int zoom_from = this->spin_zoom_from->value();
        this->spin_zoom_to->setRange(zoom_from, 19);
    });
    // "From" follows the imagery level the map draws (the selection frame
    // shows the tiles of that level). In 3D the zoom level is the camera
    // distance's 2D equivalent (MapModel::viewGlobeZoomLevel()).
    connect(this->map, &MapWidget::signalZoomLevelChanged, this->spin_zoom_from, [this](double zoom)
    {
        this->spin_zoom_from->setValue(
            MapBgfx2dImagery::imageryZoomFor(zoom, MapModel::MinZoom, MapModel::MaxZoom));
    });
    
    this->button_tiles_delete = new QToolButton(wgt);
    this->button_tiles_delete->setText("Delete Tiles");
    this->button_tiles_delete->setCheckable(true);
    this->button_tiles_delete->setEnabled(false);
    
    connect(this->editor_controller, &MapEditorController::signalRectangleSelectionCanceled, this, [this]
    {
        if (this->toolbox->currentIndex() != this->toolbox_cache_index)
            return;

        this->button_tiles_delete->setChecked(false);
        this->button_tiles_delete->setEnabled(false);
    });
    connect(this->editor_controller, &MapEditorController::signalRectangleSelected, this, [this]
    {
        if (this->toolbox->currentIndex() != this->toolbox_cache_index)
            return;

        this->button_tiles_delete->setEnabled(true);
    });
    connect(this->button_tiles_delete, &QToolButton::clicked, this, [this]
    {
        this->button_tiles_delete->setChecked(false);

        QMessageBox *box = new QMessageBox(this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setWindowTitle("Delete selected map tiles?");
        box->setIcon(QMessageBox::Warning);
        box->setText(QString("This will permanently delete all cached map tiles in the selected area for zoom levels %1 through %2.")
            .arg(this->spin_zoom_from->value())
            .arg(this->spin_zoom_to->value()));
        box->setInformativeText("Only continue when you have an internet connection. Deleted tiles may need to be downloaded again; without internet access, the affected map area can remain blank.");

        QPushButton *delete_button = box->addButton("Delete Tiles", QMessageBox::DestructiveRole);
        QPushButton *cancel_button = box->addButton(QMessageBox::Cancel);
        box->setDefaultButton(cancel_button);
        box->setEscapeButton(cancel_button);

        connect(delete_button, &QPushButton::clicked, this, [this]
        {
            for (int zoom = this->spin_zoom_from->value(); zoom <= this->spin_zoom_to->value(); ++zoom)
            {
                const MapEditorController::TileSelectionRange range = this->editor_controller->tileSelectionRange(zoom);
                if (!range.valid)
                    continue;

                this->map->deleteCachedTiles(
                    zoom,
                    range.tile_x_min,
                    range.tile_x_max,
                    range.tile_y_min,
                    range.tile_y_max);
            }

            this->map->repaint();
            this->editor_controller->clearTileSelectionOverlay();
            this->button_tiles_delete->setEnabled(false);
        });
        box->open();
    });
    
    grid->addWidget(label_explanation_rectangle, 0, 0, 1, 2);
    grid->addWidget(label_explanation_spinners, 1, 0, 1, 2);
    grid->addWidget(this->spin_zoom_from, 2, 0);
    grid->addWidget(this->spin_zoom_to, 2, 1);
    grid->addWidget(label_explanation_actions, 3, 0, 1, 2);
    grid->addWidget(this->button_tiles_delete, 4, 0, 1, 2);

    this->toolbox_cache_index = tbx->addItem(wgt, "Tile Cache");
}

void MapEditorMenuWidget::createToolboxTerrain(QToolBox *tbx)
{
    // The Tile Cache section for the terrain (DEM) tiles of the map server's
    // terrain service: the same region selection (brown instead of green),
    // terrain zoom levels of the dataset's range, and optionally the
    // provider's downloaded source data, so the area is rebuilt from fresh
    // downloads.
    QWidget *wgt = new QWidget(tbx);
    QGridLayout *grid = new QGridLayout(wgt);

    QLabel *label_explanation_rectangle = new QLabel(
        "<b>Select a region first:</b> right-click and drag on the map (2D or 3D).", wgt);
    label_explanation_rectangle->setWordWrap(true);

    this->label_terrain_dataset = new QLabel(wgt);
    this->label_terrain_dataset->setWordWrap(true);

    QLabel *label_explanation_spinners = new QLabel(
        "<b>Select the terrain zoom level range</b> (from - to) for the terrain tiles you want to delete:", wgt);
    label_explanation_spinners->setWordWrap(true);

    this->spin_terrain_zoom_from = new QSpinBox(wgt);
    this->spin_terrain_zoom_to = new QSpinBox(wgt);
    connect(this->spin_terrain_zoom_from, &QSpinBox::valueChanged, this, [this](int zoom_from)
    {
        this->spin_terrain_zoom_to->setRange(zoom_from, this->spin_terrain_zoom_from->maximum());
    });
    // "From" follows the terrain level drawn around the view (the imagery
    // level within the dataset's range; QSpinBox clamps), as the selection
    // frame shows the terrain tiles of that level.
    connect(this->map, &MapWidget::signalZoomLevelChanged, this->spin_terrain_zoom_from, [this](double zoom)
    {
        if (this->terrain_repository == nullptr || this->terrain_repository->activeDataset() == nullptr)
            return;
        this->spin_terrain_zoom_from->setValue(
            MapBgfx2dImagery::imageryZoomFor(zoom, MapModel::MinZoom, MapModel::MaxZoom));
    });

    this->checkbox_terrain_source = new QCheckBox("Also delete source elevation data", wgt);
    this->checkbox_terrain_source->setToolTip(
        "Also delete the terrain provider's downloaded source data (for Copernicus GLO-30 the "
        "1\u00b0 \u00d7 1\u00b0 GeoTIFF cells) covering the selected area, so its terrain is "
        "rebuilt from freshly downloaded data. Source cells are large downloads and may also "
        "cover terrain outside the selection.");

    QLabel *label_explanation_actions = new QLabel(
        "Then delete the cached terrain for the selected area and zoom levels:", wgt);
    label_explanation_actions->setWordWrap(true);

    this->button_terrain_delete = new QToolButton(wgt);
    this->button_terrain_delete->setText("Delete Terrain");
    this->button_terrain_delete->setEnabled(false);

    this->label_terrain_result = new QLabel(wgt);
    this->label_terrain_result->setWordWrap(true);
    this->label_terrain_result->setVisible(false);

    connect(this->editor_controller, &MapEditorController::signalRectangleSelectionCanceled, this, [this]
    {
        if (this->toolbox->currentIndex() != this->toolbox_terrain_index)
            return;

        this->terrain_selection_valid = false;
        refreshTerrainDeleteButton();
    });
    connect(this->editor_controller, &MapEditorController::signalRectangleSelected, this, [this]
    {
        if (this->toolbox->currentIndex() != this->toolbox_terrain_index)
            return;

        this->terrain_selection_valid = true;
        refreshTerrainDeleteButton();
    });
    connect(this->button_terrain_delete, &QToolButton::clicked, this, [this]
    {
        const MapTerrainDatasetInfo *dataset = this->terrain_repository != nullptr
            ? this->terrain_repository->activeDataset() : nullptr;
        if (dataset == nullptr || this->terrain_deletion_running)
            return;

        const QString dataset_id = dataset->dataset;
        const int zoom_from = this->spin_terrain_zoom_from->value();
        const int zoom_to = this->spin_terrain_zoom_to->value();
        const bool with_source = this->checkbox_terrain_source->isChecked();

        QMessageBox *box = new QMessageBox(this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setWindowTitle("Delete selected terrain?");
        box->setIcon(QMessageBox::Warning);
        QString text = QString("This will permanently delete all cached terrain tiles of dataset %1 "
                               "in the selected area for terrain zoom levels %2 through %3.")
            .arg(dataset_id.toHtmlEscaped())
            .arg(zoom_from)
            .arg(zoom_to);
        if (with_source)
            text += " The downloaded source elevation data covering the area is deleted as well.";
        box->setText(text);
        box->setInformativeText(with_source
            ? "Only continue when the map server has an internet connection. The source data "
              "has to be downloaded again (large files); without internet access the affected "
              "area stays without terrain relief."
            : "The terrain tiles are rebuilt from the map server's cached source data, or "
              "downloaded again where it has none.");

        QPushButton *delete_button = box->addButton("Delete Terrain", QMessageBox::DestructiveRole);
        QPushButton *cancel_button = box->addButton(QMessageBox::Cancel);
        box->setDefaultButton(cancel_button);
        box->setEscapeButton(cancel_button);

        connect(delete_button, &QPushButton::clicked, this,
                [this, dataset_id, zoom_from, zoom_to, with_source]
        {
            // The selection scaled to every chosen terrain level (the frame
            // shows one of them); the source data covering the finest one,
            // the tightest area.
            QVector<MapTerrainCacheDeletionRange> ranges;
            for (int zoom = zoom_from; zoom <= zoom_to; ++zoom)
            {
                const MapEditorController::TileSelectionRange range =
                    this->editor_controller->tileSelectionRange(zoom);
                if (!range.valid)
                    continue;

                MapTerrainCacheDeletionRange deletion;
                deletion.zoom = zoom;
                deletion.tile_x_min = range.tile_x_min;
                deletion.tile_x_max = range.tile_x_max;
                deletion.tile_y_min = range.tile_y_min;
                deletion.tile_y_max = range.tile_y_max;
                ranges.append(deletion);
            }
            std::optional<MapTerrainCacheDeletionRange> source_range;
            if (with_source && !ranges.isEmpty())
                source_range = ranges.last();
            if (ranges.isEmpty() || this->terrain_repository == nullptr)
                return;

            this->terrain_deletion_running = true;
            this->terrain_deletion_with_source = with_source;
            this->label_terrain_result->setText("Deleting the cached terrain\u2026");
            this->label_terrain_result->setVisible(true);
            this->editor_controller->clearTileSelectionOverlay();
            this->terrain_selection_valid = false;
            refreshTerrainDeleteButton();
            updateToolboxHeight(this->toolbox->currentIndex());
            this->terrain_repository->deleteCache(dataset_id, ranges, source_range);
        });
        box->open();
    });
    if (this->terrain_repository != nullptr)
    {
        connect(this->terrain_repository, &MapTerrainRepository::signalTerrainCacheDeletionFinished,
                this, [this](int deleted_tiles, int deleted_source_files, const QString &error)
        {
            if (!this->terrain_deletion_running)
                return;

            this->terrain_deletion_running = false;
            QString text = deleted_tiles >= 0
                ? QString("Deleted %1 terrain tile(s)").arg(deleted_tiles)
                : QString("Deleted the terrain tiles");
            if (this->terrain_deletion_with_source)
            {
                text += deleted_source_files >= 0
                    ? QString(" and %1 source data file(s)").arg(deleted_source_files)
                    : QString(" and the source data");
            }
            text += ".";
            if (!error.isEmpty())
            {
                text = QString("<b>Terrain deletion failed:</b> %1<br>Partially: %2")
                    .arg(error.toHtmlEscaped(), text);
            }
            this->label_terrain_result->setText(text);
            this->label_terrain_result->setVisible(true);
            refreshTerrainDeleteButton();
            updateToolboxHeight(this->toolbox->currentIndex());
        });
    }

    grid->addWidget(label_explanation_rectangle, 0, 0, 1, 2);
    grid->addWidget(this->label_terrain_dataset, 1, 0, 1, 2);
    grid->addWidget(label_explanation_spinners, 2, 0, 1, 2);
    grid->addWidget(this->spin_terrain_zoom_from, 3, 0);
    grid->addWidget(this->spin_terrain_zoom_to, 3, 1);
    grid->addWidget(this->checkbox_terrain_source, 4, 0, 1, 2);
    grid->addWidget(label_explanation_actions, 5, 0, 1, 2);
    grid->addWidget(this->button_terrain_delete, 6, 0, 1, 2);
    grid->addWidget(this->label_terrain_result, 7, 0, 1, 2);

    this->toolbox_terrain_index = tbx->addItem(wgt, "Terrain Cache");
}
void MapEditorMenuWidget::createToolboxEdit(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QVBoxLayout *page_layout = new QVBoxLayout(wgt);
    // 3D: the tools are disabled as a whole (their own enabled states are
    // kept for 2D) and this hint says why.
    this->label_edit_globe_hint = new QLabel(
        "<b>Network editing works in 2D.</b> Switch the map to 2D with the 2D/3D button "
        "to edit; the cache sections below work in 3D too.", wgt);
    this->label_edit_globe_hint->setWordWrap(true);
    this->label_edit_globe_hint->setVisible(false);
    page_layout->addWidget(this->label_edit_globe_hint);
    this->edit_tools = new QWidget(wgt);
    QVBoxLayout *lay = new QVBoxLayout(this->edit_tools);
    lay->setContentsMargins(0, 0, 0, 0);
    page_layout->addWidget(this->edit_tools);
    
    this->button_group_tools = new QButtonGroup(this);
    this->button_radio_select = new QRadioButton(wgt);
    this->button_radio_select->setToolTip("Cancel placement and return to selection mode");
    bindEditorShortcut(this->button_radio_select, GuiShortcutId::MapEditorSelect,
                       QStringLiteral("Select"), this);
    lay->addWidget(this->button_radio_select);
    this->button_group_tools->addButton(this->button_radio_select, 100);
    connect(this->button_radio_select, &QRadioButton::clicked, this, [this]
    {
        if (this->toolbox->currentIndex() != this->toolbox_edit_index)
            return;

        this->editor_controller->stopEntityPositioning();
        this->editor_controller->startRectangleSelection(false, true);
    });
    
    QToolButton *button_delete = new QToolButton(wgt);
    bindEditorShortcut(button_delete, GuiShortcutId::MapEditorDelete,
                       QStringLiteral("Delete Selected"), this);
    button_delete->setEnabled(false);
    lay->addWidget(button_delete);
    connect(this->editor_controller, &MapEditorController::signalEntitySelectionChanged, this,
    [button_delete](bool selected)
    {
        button_delete->setEnabled(selected);
    });
    connect(button_delete, &QPushButton::clicked, this->editor_controller,
            &MapEditorController::deleteSelectedEntities);
    
    QLabel *label_add = new QLabel("Add:", this);
    lay->addWidget(label_add);
    
    QRadioButton *button_radio_pipe = new QRadioButton(wgt);
    bindEditorShortcut(button_radio_pipe, GuiShortcutId::MapEditorAddPipe,
                       QStringLiteral("Pipe / Cable"), this);
    lay->addWidget(button_radio_pipe);
    this->button_group_tools->addButton(button_radio_pipe, 1);
    connect(button_radio_pipe, &QRadioButton::clicked, this, [this]
    {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::Pipe);
    });
    
    QRadioButton *button_radio_junction = new QRadioButton(wgt);
    bindEditorShortcut(button_radio_junction, GuiShortcutId::MapEditorAddJunction,
                       QStringLiteral("Junction"), this);
    lay->addWidget(button_radio_junction);
    this->button_group_tools->addButton(button_radio_junction, 2);
    connect(button_radio_junction, &QRadioButton::clicked, this, [this]
    {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::Junction);
    });
    
    QRadioButton *button_radio_valve = new QRadioButton(wgt);
    bindEditorShortcut(button_radio_valve, GuiShortcutId::MapEditorAddValve,
                       QStringLiteral("Valve / Switch"), this);
    lay->addWidget(button_radio_valve);
    this->button_group_tools->addButton(button_radio_valve, 3);
    connect(button_radio_valve, &QRadioButton::clicked, this, [this] {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::Valve);
    });
    
    QRadioButton *button_radio_demand_point = new QRadioButton(wgt);
    bindEditorShortcut(button_radio_demand_point, GuiShortcutId::MapEditorAddDemandPoint,
                       QStringLiteral("Demand Point"), this);
    lay->addWidget(button_radio_demand_point);
    this->button_group_tools->addButton(button_radio_demand_point, 4);
    connect(button_radio_demand_point, &QRadioButton::clicked, this, [this]
    {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::DemandPoint);
    });
    
    QRadioButton *button_radio_pump = new QRadioButton(wgt);
    bindEditorShortcut(button_radio_pump, GuiShortcutId::MapEditorAddPump,
                       QStringLiteral("Pump"), this);
    lay->addWidget(button_radio_pump);
    this->button_group_tools->addButton(button_radio_pump, 5);
    connect(button_radio_pump, &QRadioButton::clicked, this, [this] {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::Pump);
    });
    
    QRadioButton *button_radio_tank = new QRadioButton(wgt);
    bindEditorShortcut(button_radio_tank, GuiShortcutId::MapEditorAddTank,
                       QStringLiteral("Tank"), this);
    lay->addWidget(button_radio_tank);
    this->button_group_tools->addButton(button_radio_tank, 6);
    connect(button_radio_tank, &QRadioButton::clicked, this, [this]
    {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::Tank);
    });
    
    QRadioButton *button_radio_power = new QRadioButton(wgt);
    bindEditorShortcut(button_radio_power, GuiShortcutId::MapEditorAddPowerSource,
                       QStringLiteral("Power Source"), this);
    lay->addWidget(button_radio_power);
    this->button_group_tools->addButton(button_radio_power, 7);
    button_radio_power->setEnabled(false);
    
    QRadioButton *button_radio_reservoir = new QRadioButton(wgt);
    bindEditorShortcut(button_radio_reservoir, GuiShortcutId::MapEditorAddReservoir,
                       QStringLiteral("Reservoir"), this);
    lay->addWidget(button_radio_reservoir);
    this->button_group_tools->addButton(button_radio_reservoir, 8);
    connect(button_radio_reservoir, &QRadioButton::clicked, this, [this]
    {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::Reservoir);
    });
    
    QRadioButton *button_radio_note = new QRadioButton(wgt);
    bindEditorShortcut(button_radio_note, GuiShortcutId::MapEditorAddNote,
                       QStringLiteral("Note"), this);
    lay->addWidget(button_radio_note);
    this->button_group_tools->addButton(button_radio_note, 9);
    button_radio_note->setEnabled(false);
    
    
    
    this->checkbox_map_editor_guide = new QCheckBox("Map Editor Guide", wgt);
    this->checkbox_map_editor_guide->setChecked(true);
    this->checkbox_map_editor_guide->setToolTip("Show or hide the Map Editor Guide");
    page_layout->addWidget(this->checkbox_map_editor_guide);
    connect(this->checkbox_map_editor_guide, &QCheckBox::toggled, this, &MapEditorMenuWidget::signalMapEditorGuideVisibilityChanged);
    
    connect(this->button_group_tools, &QButtonGroup::idToggled, this, [this]
    {
        this->editor_controller->stopEntityPositioning();
    });
    
    this->toolbox_edit_index = tbx->addItem(wgt, "Edit Network");
}

void MapEditorMenuWidget::setToolboxMode(int index)
{
    emit signalEditNetworkSectionActive(index == this->toolbox_edit_index);

    this->editor_controller->stopEntityPositioning();
    this->editor_controller->clearTileSelectionOverlay();
    this->button_tiles_delete->setChecked(false);
    this->button_tiles_delete->setEnabled(false);
    this->terrain_selection_valid = false;
    refreshTerrainDeleteButton();

    if (index == this->toolbox_cache_index)
    {
        this->editor_controller->setTileSelectionKind(MapEditorTileSelectionKind::MapTiles);
        this->editor_controller->startRectangleSelection(false, false);
    }
    else if (index == this->toolbox_terrain_index)
    {
        this->editor_controller->setTileSelectionKind(MapEditorTileSelectionKind::Terrain);
        this->editor_controller->startRectangleSelection(false, false);
    }
    else if (index == this->toolbox_edit_index)
    {
        this->editor_controller->setTileSelectionKind(MapEditorTileSelectionKind::MapTiles);
        this->button_radio_select->setChecked(true);
        this->editor_controller->startRectangleSelection(false, true);
    }

    updateToolboxHeight(index);
}

