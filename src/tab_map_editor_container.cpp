#include "tab_map_editor_container.h"

#ifdef Q_OS_WASM
#include "wasm/browser_network_snapshot_serializer.h"
#endif

#include <QMessageBox>

#ifdef Q_OS_WASM
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>

#include <emscripten.h>

EM_JS(int, aowisBrowserMapEditorSetNetworkSnapshot, (const char *json_data, int json_size),
{
    if (!window.aowisBrowserMapEditor ||
        typeof window.aowisBrowserMapEditor.setNetworkSnapshot !== "function")
        return 0;

    try
    {
        window.aowisBrowserMapEditor.setNetworkSnapshot(
            JSON.parse(UTF8ToString(json_data, json_size)));
        return 1;
    }
    catch (error)
    {
        console.error("Could not transfer AOWIS map editor network snapshot:", error);
        return 0;
    }
});

EM_JS(int, aowisBrowserMapEditorUpdateGeometry, (const char *json_data, int json_size),
{
    if (!window.aowisBrowserMapEditor ||
        typeof window.aowisBrowserMapEditor.updateGeometry !== "function")
        return 0;

    try
    {
        window.aowisBrowserMapEditor.updateGeometry(
            JSON.parse(UTF8ToString(json_data, json_size)));
        return 1;
    }
    catch (error)
    {
        console.error("Could not transfer AOWIS map editor geometry update:", error);
        return 0;
    }
});

EM_JS(int, aowisBrowserMapEditorSetVisualState, (const char *json_data, int json_size),
{
    if (!window.aowisBrowserMapEditor ||
        typeof window.aowisBrowserMapEditor.setVisualState !== "function")
        return 0;

    try
    {
        window.aowisBrowserMapEditor.setVisualState(
            JSON.parse(UTF8ToString(json_data, json_size)));
        return 1;
    }
    catch (error)
    {
        console.error("Could not transfer AOWIS map editor visual state:", error);
        return 0;
    }
});

EM_JS(int, aowisBrowserMapEditorSetViewportState, (const char *json_data, int json_size),
{
    if (!window.aowisBrowserMapEditor ||
        typeof window.aowisBrowserMapEditor.setViewportState !== "function")
        return 0;

    try
    {
        window.aowisBrowserMapEditor.setViewportState(
            JSON.parse(UTF8ToString(json_data, json_size)));
        return 1;
    }
    catch (error)
    {
        console.error("Could not transfer AOWIS map editor viewport state:", error);
        return 0;
    }
});

EM_JS(void, aowisBrowserMapEditorSetBackground, (int red, int green, int blue),
{
    if (!window.aowisBrowserMapEditor ||
        typeof window.aowisBrowserMapEditor.setBackground !== "function")
        return;

    window.aowisBrowserMapEditor.setBackground(red, green, blue);
});

EM_JS(void, aowisBrowserMapEditorSetOwnerId, (int owner_id),
{
    if (!window.aowisBrowserMapEditor ||
        typeof window.aowisBrowserMapEditor.setOwnerId !== "function")
        return;

    window.aowisBrowserMapEditor.setOwnerId(owner_id);
});

EM_JS(void, aowisBrowserMapEditorClear, (),
{
    if (window.aowisBrowserMapEditor &&
        typeof window.aowisBrowserMapEditor.clear === "function")
        window.aowisBrowserMapEditor.clear();
});

namespace
{
QJsonArray coordinateToJson(const CoordinateWGS84 &coordinate)
{
    QJsonArray result;
    result.append(coordinate.longitude_deg);
    result.append(coordinate.latitude_deg);
    return result;
}

QJsonArray uuidListToJson(const QList<QUuid> &uuids)
{
    QJsonArray result;
    for (const QUuid &uuid : uuids)
        result.append(uuid.toString(QUuid::WithoutBraces));
    return result;
}

QSet<QUuid> networkNodeUuids(const NetworkRenderSnapshot &snapshot)
{
    QSet<QUuid> result;
    result.reserve(snapshot.nodes.size());
    for (const NetworkRenderNode &node : snapshot.nodes)
        result.insert(node.uuid);
    return result;
}

QSet<QUuid> networkLinkUuids(const NetworkRenderSnapshot &snapshot)
{
    QSet<QUuid> result;
    result.reserve(snapshot.links.size());
    for (const NetworkRenderLink &link : snapshot.links)
        result.insert(link.uuid);
    return result;
}

QByteArray serializeMapEditorVisualState(const MapEditorVisualState &state)
{
    QJsonArray intermediate_vertices;
    for (const CoordinateWGS84 &coordinate : state.placement.pipe_intermediate_vertices)
        intermediate_vertices.append(coordinateToJson(coordinate));

    QJsonArray move_markers;
    for (const MapEditorDynamicMarkerVisualState &marker : state.move.markers)
    {
        QJsonObject item;
        item.insert(QStringLiteral("entity"), static_cast<int>(marker.entity));
        item.insert(QStringLiteral("uuid"), marker.uuid.toString(QUuid::WithoutBraces));
        item.insert(QStringLiteral("coordinate"), coordinateToJson(marker.coordinate_wgs84));
        item.insert(QStringLiteral("pixmapPath"), marker.pixmap_path);
        move_markers.append(item);
    }

    QJsonArray move_links;
    for (const MapEditorDynamicLinkVisualState &link : state.move.links)
    {
        QJsonArray vertices;
        for (const CoordinateWGS84 &coordinate : link.vertices_wgs84)
            vertices.append(coordinateToJson(coordinate));

        QJsonObject item;
        item.insert(QStringLiteral("entity"), static_cast<int>(link.entity));
        item.insert(QStringLiteral("uuid"), link.uuid.toString(QUuid::WithoutBraces));
        item.insert(QStringLiteral("vertices"), vertices);
        move_links.append(item);
    }

    QJsonObject move;
    move.insert(QStringLiteral("active"), state.move.active);
    move.insert(QStringLiteral("sessionId"), QString::number(state.move.session_id));
    move.insert(QStringLiteral("markers"), move_markers);
    move.insert(QStringLiteral("links"), move_links);

    QJsonObject placement;
    placement.insert(QStringLiteral("creating"), state.placement.creating);
    placement.insert(QStringLiteral("floatingMarkerVisible"),
                     state.placement.floating_marker_visible);
    placement.insert(QStringLiteral("entity"), static_cast<int>(state.placement.entity));
    placement.insert(QStringLiteral("mouseX"), state.placement.mouse_position.x());
    placement.insert(QStringLiteral("mouseY"), state.placement.mouse_position.y());
    placement.insert(QStringLiteral("connectionTargetUuid"),
                     state.placement.connection_target_uuid.toString(QUuid::WithoutBraces));
    placement.insert(QStringLiteral("pipeStartNodeUuid"),
                     state.placement.pipe_start_node_uuid.toString(QUuid::WithoutBraces));
    placement.insert(QStringLiteral("pipeIntermediateVertices"), intermediate_vertices);
    placement.insert(QStringLiteral("deviceLinkStartNodeUuid"),
                     state.placement.device_link_start_node_uuid.toString(QUuid::WithoutBraces));
    placement.insert(QStringLiteral("floatingWidth"), state.placement.floating_width);

    QJsonObject root;
    root.insert(QStringLiteral("revision"), QString::number(state.revision));
    root.insert(QStringLiteral("selectedMarkerUuids"),
                uuidListToJson(state.selected_marker_uuids));
    root.insert(QStringLiteral("selectedPipeUuids"),
                uuidListToJson(state.selected_pipe_uuids));
    root.insert(QStringLiteral("wrapReferenceLongitude"), state.wrap_reference_longitude);
    root.insert(QStringLiteral("entityWidth"), state.entity_width);
    root.insert(QStringLiteral("placement"), placement);
    root.insert(QStringLiteral("move"), move);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray serializeMapEditorViewportState(const MapEditorViewportRenderState &state)
{
    QJsonObject tile_selection;
    tile_selection.insert(QStringLiteral("visible"), state.tile_selection_visible);
    tile_selection.insert(QStringLiteral("xMin"), state.tile_x_min);
    tile_selection.insert(QStringLiteral("xMax"), state.tile_x_max);
    tile_selection.insert(QStringLiteral("yMin"), state.tile_y_min);
    tile_selection.insert(QStringLiteral("yMax"), state.tile_y_max);

    QJsonObject rectangle_selection;
    rectangle_selection.insert(QStringLiteral("visible"), state.rectangle_selection_visible);
    rectangle_selection.insert(QStringLiteral("x"), state.rectangle_selection.x());
    rectangle_selection.insert(QStringLiteral("y"), state.rectangle_selection.y());
    rectangle_selection.insert(QStringLiteral("width"), state.rectangle_selection.width());
    rectangle_selection.insert(QStringLiteral("height"), state.rectangle_selection.height());

    QJsonObject root;
    root.insert(QStringLiteral("backgroundOpacity"), state.background_opacity);
    root.insert(QStringLiteral("tileSelection"), tile_selection);
    root.insert(QStringLiteral("rectangleSelection"), rectangle_selection);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
}
#endif

MapEditorContainer::MapEditorContainer(MapModel *map_model, MapTileRepository *tile_repository, HydraulicData *hydraulic_data, GpsProvider *gps, EntityInspectorDock *map_inspector, QWidget *parent)
    : QWidget{parent},
    hydraulic_data( hydraulic_data ),
    gps( gps ),
    map_inspector( map_inspector ),
    map_model( map_model ),
    tile_repository( tile_repository ),
    map( new MapWidget(this->map_model, this->tile_repository, this->gps, this) ),
    map_canvas( new MapCanvasWidget(this->map_model, this->map, this->hydraulic_data, this) ),
    editor_controller( new MapEditorController(this->map_model, this->map_canvas->mapCanvasEntities(), this) ),
    map_menu( new MapEditorMenuWidget(this->map, this->map_canvas, this->editor_controller,
                                      CanvasMode::Edit, this) ),
    layout( new QHBoxLayout(this) ),
    map_stack( new QWidget(this) ),
    map_stack_layout( new QStackedLayout(this->map_stack) )
{
    this->map_canvas->setEditorController(this->editor_controller);

    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    scroll_controls->setMinimumWidth(Sizes::SidebarMapEditLeftWidth);
    scroll_controls->setMaximumWidth(Sizes::SidebarMapEditLeftWidth);
    scroll_controls->setWidgetResizable(true);
    scroll_controls->setFrameShape(QFrame::NoFrame);
    scroll_controls->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_controls->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    scroll_controls->setWidget(this->map_menu);
    
    this->map_stack_layout->setContentsMargins(0, 0, 0, 0);
    this->map_stack_layout->setSpacing(0);
    this->map_stack_layout->setStackingMode(QStackedLayout::StackAll);
    
    this->map_stack_layout->addWidget(this->map);
    this->map_stack_layout->addWidget(this->map_canvas);
    this->map_canvas->raise();
#ifdef Q_OS_WASM
    this->map->setBrowserMapLayerEnabled(true);
    this->map->setBrowserMapLayerTopmost(true);
    aowisBrowserMapEditorSetOwnerId(this->map->browserMapLayerOwnerId());

    this->wasm_map_layer_sync_timer = new QTimer(this);
    this->wasm_map_layer_sync_timer->setSingleShot(true);
    connect(this->wasm_map_layer_sync_timer, &QTimer::timeout,
            this, &MapEditorContainer::syncWasmMapLayers);

    this->installEventFilter(this);
    this->map->installEventFilter(this);
    this->map_stack->installEventFilter(this);
    this->map_canvas->installEventFilter(this);
    if (this->window())
        this->window()->installEventFilter(this);

    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded,
            this, &MapEditorContainer::scheduleWasmMapLayerSync);
    connect(this->hydraulic_data, &HydraulicData::signalNetworkGeometryChanged,
            this, &MapEditorContainer::scheduleWasmMapLayerSync);
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this,
            [this](InfrastructureEntity, const QUuid &uuid)
    {
        this->wasm_dirty_node_uuids.insert(uuid);
        this->scheduleWasmMapLayerSync();
    });
    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
            [this](InfrastructureEntity, const QUuid &uuid)
    {
        this->wasm_dirty_link_uuids.insert(uuid);
        this->scheduleWasmMapLayerSync();
    });
    connect(this->map_canvas->mapCanvasEntities(), &MapCanvasEntities::signalVisualStateChanged,
            this, &MapEditorContainer::scheduleWasmMapLayerSync);
    connect(this->editor_controller, &MapEditorController::signalStateChanged,
            this, &MapEditorContainer::scheduleWasmMapLayerSync);

    this->syncWasmBackground();
    this->scheduleWasmMapLayerSync();
#endif
    
    this->layout->addWidget(scroll_controls);
    this->layout->addWidget(this->map_stack);
    
    connect(this->map_menu, &MapEditorMenuWidget::signalSlideOpacityChanged,
            this->map_canvas, &MapCanvasWidget::setBackgroundOpacity);
    connect(this->map_menu->mapNavigationWidget(), &MapNavigationWidget::signalIconSizeChanged,
            this->map_canvas, &MapCanvasWidget::setIconSizePercent);
#ifdef Q_OS_WASM
    connect(this->map_menu, &MapEditorMenuWidget::signalSlideOpacityChanged,
            this, &MapEditorContainer::scheduleWasmMapLayerSync);
#endif
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
#ifdef Q_OS_WASM
    this->map->setBrowserMapLayerGeometry(QRect(), false);
    aowisBrowserMapEditorClear();
#endif
}

#ifdef Q_OS_WASM
bool MapEditorContainer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this || watched == this->map || watched == this->map_stack ||
        watched == this->map_canvas || watched == this->window())
    {
        switch (event->type())
        {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::LayoutRequest:
        case QEvent::ParentChange:
        case QEvent::WindowStateChange:
            this->scheduleWasmMapLayerSync();
            break;
        case QEvent::ApplicationPaletteChange:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
            this->syncWasmBackground();
            this->scheduleWasmMapLayerSync();
            break;
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void MapEditorContainer::scheduleWasmMapLayerSync()
{
    if (!this->wasm_map_layer_sync_timer->isActive())
        this->wasm_map_layer_sync_timer->start(0);
}

void MapEditorContainer::syncWasmMapLayers()
{
    const bool visible = this->isVisible() && this->map->isVisible()
        && this->map->width() > 0 && this->map->height() > 0;

    if (!visible)
    {
        this->map->setBrowserMapLayerGeometry(QRect(), false);
        return;
    }

    const QRect map_geometry(this->map->mapToGlobal(QPoint(0, 0)), this->map->size());
    this->map->setBrowserMapLayerGeometry(map_geometry, true);
    this->syncWasmNetworkSnapshot();
    this->syncWasmVisualState();
    this->syncWasmViewportState();
}

void MapEditorContainer::syncWasmNetworkSnapshot()
{
    if (!this->hydraulic_data)
        return;

    const quint64 geometry_revision = this->hydraulic_data->geometryRevision();
    if (this->wasm_network_snapshot_sent &&
        this->wasm_network_geometry_revision_sent == geometry_revision)
    {
        this->wasm_dirty_node_uuids.clear();
        this->wasm_dirty_link_uuids.clear();
        return;
    }

    const NetworkRenderSnapshot &snapshot = this->hydraulic_data->networkRenderSnapshot();
    const QSet<QUuid> node_uuids = networkNodeUuids(snapshot);
    const QSet<QUuid> link_uuids = networkLinkUuids(snapshot);
    const bool structure_unchanged = this->wasm_network_snapshot_sent &&
        node_uuids == this->wasm_network_node_uuids_sent &&
        link_uuids == this->wasm_network_link_uuids_sent;
    const bool has_geometry_patch = structure_unchanged &&
        (!this->wasm_dirty_node_uuids.isEmpty() || !this->wasm_dirty_link_uuids.isEmpty());

    int transferred = 0;
    if (has_geometry_patch)
    {
        const QByteArray json = BrowserNetworkSnapshotSerializer::serializeGeometryPatch(
            snapshot, this->wasm_dirty_node_uuids, this->wasm_dirty_link_uuids);
        transferred = aowisBrowserMapEditorUpdateGeometry(
            json.constData(), static_cast<int>(json.size()));
    }
    else
    {
        const QByteArray json = BrowserNetworkSnapshotSerializer::serialize(snapshot);
        transferred = aowisBrowserMapEditorSetNetworkSnapshot(
            json.constData(), static_cast<int>(json.size()));
    }

    if (transferred == 0)
        return;

    this->wasm_network_geometry_revision_sent = snapshot.geometry_revision;
    this->wasm_network_node_uuids_sent = node_uuids;
    this->wasm_network_link_uuids_sent = link_uuids;
    this->wasm_dirty_node_uuids.clear();
    this->wasm_dirty_link_uuids.clear();
    this->wasm_network_snapshot_sent = true;
}

void MapEditorContainer::syncWasmVisualState()
{
    const MapEditorVisualState state = this->map_canvas->visualState();
    if (this->wasm_visual_state_sent &&
        this->wasm_visual_state_revision_sent == state.revision)
    {
        return;
    }

    const QByteArray json = serializeMapEditorVisualState(state);
    if (aowisBrowserMapEditorSetVisualState(
            json.constData(), static_cast<int>(json.size())) == 0)
    {
        return;
    }

    this->wasm_visual_state_revision_sent = state.revision;
    this->wasm_visual_state_sent = true;
}

void MapEditorContainer::syncWasmViewportState()
{
    const QByteArray json = serializeMapEditorViewportState(
        this->map_canvas->viewportRenderState());
    if (json == this->wasm_viewport_state_sent)
        return;

    if (aowisBrowserMapEditorSetViewportState(
            json.constData(), static_cast<int>(json.size())) == 0)
    {
        return;
    }

    this->wasm_viewport_state_sent = json;
}

void MapEditorContainer::syncWasmBackground()
{
    const QColor background = this->map_canvas->palette().color(QPalette::Window);
    aowisBrowserMapEditorSetBackground(
        background.red(), background.green(), background.blue());
}
#endif

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
                                         MapEditorController *editor_controller, CanvasMode mode,
                                         QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) ),
    mode( mode ),
    map( map ),
    map_nav( new MapNavigationWidget(this->map, this->mode, map_canvas, this) ),
    map_canvas( map_canvas ),
    editor_controller( editor_controller ),
    toolbox( new QToolBox(this) )
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    
    this->toolbox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    createToolboxEdit(this->toolbox);
    createToolboxCache(this->toolbox);
    
    this->layout->addWidget(this->map_nav);
    this->layout->addWidget(this->toolbox);
    this->layout->addStretch();
    
    connect(this->toolbox, &QToolBox::currentChanged, this, &MapEditorMenuWidget::setToolboxMode);
    connect(this->map_nav, &MapNavigationWidget::signalSlideOpacityChanged, this, &MapEditorMenuWidget::signalSlideOpacityChanged);
    
    setToolboxMode(this->toolbox->currentIndex());
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

void MapEditorMenuWidget::setMapEditorGuideChecked(bool checked)
{
    this->checkbox_map_editor_guide->setChecked(checked);
}

void MapEditorMenuWidget::createToolboxCache(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QGridLayout *grid = new QGridLayout(wgt);
    
    QLabel *label_explanation_rectangle = new QLabel("<b>Select a region first:</b> right-click and drag on the map.", this);
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
    connect(this->map, &MapWidget::signalZoomChanged, this->spin_zoom_from, &QSpinBox::setValue);
    
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
void MapEditorMenuWidget::createToolboxEdit(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QVBoxLayout *lay = new QVBoxLayout(wgt);
    
    this->button_group_tools = new QButtonGroup(this);
    
    this->button_radio_select = new QRadioButton("[Esc] Select", wgt);
    this->button_radio_select->setToolTip("Cancel placement and return to selection mode");
    this->button_radio_select->setShortcut(Qt::Key_Escape);
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
    button_delete->setText("[Del] Delete Selected");
    button_delete->setShortcut(Qt::Key_Delete);
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
    
    QRadioButton *button_radio_pipe = new QRadioButton("[1] Pipe / Cable", wgt);
    button_radio_pipe->setShortcut(Qt::Key_1);
    lay->addWidget(button_radio_pipe);
    this->button_group_tools->addButton(button_radio_pipe, 1);
    connect(button_radio_pipe, &QRadioButton::clicked, this, [this]
    {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::Pipe);
    });
    
    QRadioButton *button_radio_junction = new QRadioButton("[2] Junction", wgt);
    button_radio_junction->setShortcut(Qt::Key_2);
    lay->addWidget(button_radio_junction);
    this->button_group_tools->addButton(button_radio_junction, 2);
    connect(button_radio_junction, &QRadioButton::clicked, this, [this]
    {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::Junction);
    });
    
    QRadioButton *button_radio_valve = new QRadioButton("[3] Valve / Switch", wgt);
    button_radio_valve->setShortcut(Qt::Key_3);
    lay->addWidget(button_radio_valve);
    this->button_group_tools->addButton(button_radio_valve, 3);
    connect(button_radio_valve, &QRadioButton::clicked, this, [this] {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::Valve);
    });
    
    QRadioButton *button_radio_customer = new QRadioButton("[4] Customer Point", wgt);
    button_radio_customer->setShortcut(Qt::Key_4);
    lay->addWidget(button_radio_customer);
    this->button_group_tools->addButton(button_radio_customer, 4);
    button_radio_customer->setEnabled(false);
    
    QRadioButton *button_radio_pump = new QRadioButton("[5] Pump", wgt);
    button_radio_pump->setShortcut(Qt::Key_5);
    lay->addWidget(button_radio_pump);
    this->button_group_tools->addButton(button_radio_pump, 5);
    connect(button_radio_pump, &QRadioButton::clicked, this, [this] {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::Pump);
    });
    
    QRadioButton *button_radio_tank = new QRadioButton("[6] Tank", wgt);
    button_radio_tank->setShortcut(Qt::Key_6);
    lay->addWidget(button_radio_tank);
    this->button_group_tools->addButton(button_radio_tank, 6);
    connect(button_radio_tank, &QRadioButton::clicked, this, [this]
    {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::Tank);
    });
    
    QRadioButton *button_radio_power = new QRadioButton("[7] Power Source", wgt);
    button_radio_power->setShortcut(Qt::Key_7);
    lay->addWidget(button_radio_power);
    this->button_group_tools->addButton(button_radio_power, 7);
    button_radio_power->setEnabled(false);
    
    QRadioButton *button_radio_reservoir = new QRadioButton("[8] Reservoir", wgt);
    button_radio_reservoir->setShortcut(Qt::Key_8);
    lay->addWidget(button_radio_reservoir);
    this->button_group_tools->addButton(button_radio_reservoir, 8);
    connect(button_radio_reservoir, &QRadioButton::clicked, this, [this]
    {
        this->editor_controller->startEntityPositioning(InfrastructureEntity::Reservoir);
    });
    
    QRadioButton *button_radio_note = new QRadioButton("[9] Note", wgt);
    button_radio_note->setShortcut(Qt::Key_9);
    lay->addWidget(button_radio_note);
    this->button_group_tools->addButton(button_radio_note, 9);
    button_radio_note->setEnabled(false);
    
    
    
    this->checkbox_map_editor_guide = new QCheckBox("Map Editor Guide", wgt);
    this->checkbox_map_editor_guide->setChecked(true);
    this->checkbox_map_editor_guide->setToolTip("Show or hide the Map Editor Guide");
    lay->addWidget(this->checkbox_map_editor_guide);
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

    if (index == this->toolbox_cache_index)
    {
        this->editor_controller->startRectangleSelection(false, false);
    }
    else if (index == this->toolbox_edit_index)
    {
        this->button_radio_select->setChecked(true);
        this->editor_controller->startRectangleSelection(false, true);
    }

    updateToolboxHeight(index);
}


