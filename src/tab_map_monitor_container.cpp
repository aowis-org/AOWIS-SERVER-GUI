#include "tab_map_monitor_container.h"

#include "hydraulic_data.h"

#ifdef Q_OS_WASM
#include "wasm/browser_network_snapshot_serializer.h"
#endif

#ifndef Q_OS_WASM
#include "map/map_network_overlay_widget.h"
#endif

#include <QColor>
#include <QContextMenuEvent>
#include <QDebug>
#include <QMouseEvent>

#include <cmath>
#include <functional>

namespace
{
class SymbologySlider final : public QSlider
{
public:
    SymbologySlider(int value_minimum, int value_maximum, int value_default, const QString &description, const QString &unit_suffix, QWidget *parent = nullptr)
        : QSlider(Qt::Horizontal, parent),
          value_default(value_default),
          description(description),
          unit_suffix(unit_suffix)
    {
        setRange(value_minimum, value_maximum);
        setValue(this->value_default);
        connect(this, &QSlider::valueChanged, this, [this]
        {
            updateToolTip();
        });
        updateToolTip();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton)
        {
            setValue(this->value_default);
            event->accept();
            return;
        }

        QSlider::mousePressEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        setValue(this->value_default);
        event->accept();
    }

private:
    void updateToolTip()
    {
        setToolTip(QStringLiteral("%1\nCurrent: %2%3\nRange: %4%3 to %5%3\nDefault: %6%3\nRight-click to reset.")
                       .arg(this->description)
                       .arg(value())
                       .arg(this->unit_suffix)
                       .arg(minimum())
                       .arg(maximum())
                       .arg(this->value_default));
    }

    int value_default;
    QString description;
    QString unit_suffix;
};
}

#ifdef Q_OS_WASM
#include <emscripten.h>

EM_JS(int, aowisBrowserNetworkSetSnapshot, (const char *json_data, int json_size),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.setSnapshot !== "function")
        return 0;

    try
    {
        const snapshot = JSON.parse(UTF8ToString(json_data, json_size));
        window.aowisBrowserNetwork.setSnapshot(snapshot);
        return 1;
    }
    catch (error)
    {
        console.error("Could not transfer AOWIS network snapshot:", error);
        return 0;
    }
});

EM_JS(int, aowisBrowserNetworkSetSymbology, (const char *json_data, int json_size),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.setSymbology !== "function")
    {
        console.error(
            "AOWIS browser network symbology API is unavailable. " +
            "The cached aowis-browser-network.js is outdated.");
        return 0;
    }

    try
    {
        const symbology = JSON.parse(UTF8ToString(json_data, json_size));
        window.aowisBrowserNetwork.setSymbology(symbology);
        return 1;
    }
    catch (error)
    {
        console.error("Could not transfer AOWIS network symbology:", error);
        return 0;
    }
});

EM_JS(double, aowisBrowserNetworkHitTest, (double x, double y),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.hitTest !== "function")
        return 0;

    const hit = window.aowisBrowserNetwork.hitTest(x, y);
    if (!hit)
        return 0;

    const entityType = Number(hit.entityType) >>> 0;
    const renderId = Number(hit.renderId) >>> 0;
    if (entityType === 0 || renderId === 0)
        return 0;

    return entityType * 4294967296 + renderId;
});

EM_JS(void, aowisBrowserNetworkSetBackground, (int red, int green, int blue, int opacity),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.setBackground !== "function")
        return;

    window.aowisBrowserNetwork.setBackground(red, green, blue, opacity);
});

EM_JS(void, aowisBrowserNetworkSetOwnerId, (int owner_id),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.setOwnerId !== "function")
        return;

    window.aowisBrowserNetwork.setOwnerId(owner_id);
});

EM_JS(void, aowisBrowserNetworkSetSelectedEntity, (int entity_type, double render_id),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.setSelectedEntity !== "function")
        return;

    window.aowisBrowserNetwork.setSelectedEntity(render_id, entity_type);
});

#endif

MapMonitorContainer::MapMonitorContainer(MapModel *map_model, MapTileRepository *tile_repository, HydraulicData *hydraulic_data, GpsProvider *gps, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    gps( gps ),
    map_model( map_model ),
    tile_repository( tile_repository ),
    hydraulic_data( hydraulic_data ),
    map_stack( new QWidget(this) ),
    map_stack_layout( new QStackedLayout(this->map_stack) ),
    map( new MapWidget(this->map_model, this->tile_repository, this->gps, this->map_stack) ),
#ifndef Q_OS_WASM
    desktop_network_overlay( new MapNetworkOverlayWidget(this->map_model, this->hydraulic_data, this->map_stack) ),
#endif
    map_menu( new MapMonitorMenuWidget(this->map, this) )
{
    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    scroll_controls->setMinimumWidth(Sizes::SidebarLeftWidth);
    scroll_controls->setMaximumWidth(Sizes::SidebarLeftWidth);
    scroll_controls->setWidgetResizable(true);
    scroll_controls->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_controls->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    // unfortunately we need to fix widget width depending on scrollbar visible with some black magic
    connect(scroll_controls->verticalScrollBar(), &QScrollBar::rangeChanged, this, [scroll_controls]
    {
        QScrollBar *sb = scroll_controls->verticalScrollBar();
        bool should_show = sb->maximum() > sb->minimum();
        
        int width_base = Sizes::SidebarLeftWidth;
        int width_sb = sb->sizeHint().width();
        int w = should_show ? width_base + width_sb : width_base;
        
        scroll_controls->setMinimumWidth(w);
        scroll_controls->setMaximumWidth(w);
    });
    
    scroll_controls->setWidget(this->map_menu);

    this->map_stack_layout->setContentsMargins(0, 0, 0, 0);
    this->map_stack_layout->setSpacing(0);
    this->map_stack_layout->setStackingMode(QStackedLayout::StackAll);
    connect(this->map_menu->mapNavigationWidget(), &MapNavigationWidget::signalSlideOpacityChanged,
        this, &MapMonitorContainer::setNetworkBackgroundOpacity);
    connect(this->map_menu->mapNavigationWidget(), &MapNavigationWidget::signalIconSizeChanged, this,
        [this](int size_percent)
    {
        this->symbology_settings.icon_size_percent = size_percent;
        applySymbology();
    });
    this->map->installEventFilter(this);
    this->map_stack_layout->addWidget(this->map);
#ifndef Q_OS_WASM
    this->map_stack_layout->addWidget(this->desktop_network_overlay);
    this->map_stack_layout->setCurrentWidget(this->desktop_network_overlay);
#endif

#ifdef Q_OS_WASM
    this->map->setBrowserMapLayerEnabled(true);
    this->map->setBrowserMapLayerTopmost(true);
    aowisBrowserNetworkSetOwnerId(this->map->browserMapLayerOwnerId());

    this->wasm_map_layer_sync_timer = new QTimer(this);
    this->wasm_map_layer_sync_timer->setSingleShot(true);
    connect(this->wasm_map_layer_sync_timer, &QTimer::timeout, this, &MapMonitorContainer::syncWasmMapLayer);

    this->wasm_network_symbology_sync_timer = new QTimer(this);
    this->wasm_network_symbology_sync_timer->setSingleShot(true);
    connect(this->wasm_network_symbology_sync_timer, &QTimer::timeout, this, &MapMonitorContainer::syncWasmNetworkSymbology);

    this->installEventFilter(this);
    this->map_stack->installEventFilter(this);
    if (this->window())
        this->window()->installEventFilter(this);

    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, [this]
    {
        scheduleWasmMapLayerSync();
        scheduleWasmNetworkSymbologySync();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkGeometryChanged, this, [this](quint64)
    {
        scheduleWasmMapLayerSync();
        scheduleWasmNetworkSymbologySync();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this,
        [this](InfrastructureEntity, const QUuid &)
    {
        scheduleWasmNetworkSymbologySync();
    });
    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
        [this](InfrastructureEntity, const QUuid &)
    {
        scheduleWasmNetworkSymbologySync();
    });
    connect(this->hydraulic_data, &HydraulicData::signalSelectedTank, this,
        [this](const HydraulicNodeTank &tank)
    {
        syncWasmSelectedEntity(InfrastructureEntity::Tank, tank.uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalSelectedReservoir, this,
        [this](const HydraulicNodeReservoir &reservoir)
    {
        syncWasmSelectedEntity(InfrastructureEntity::Reservoir, reservoir.uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalSelectedJunction, this,
        [this](const HydraulicNodeJunction &junction)
    {
        syncWasmSelectedEntity(InfrastructureEntity::Junction, junction.uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalSelectedPipe, this,
        [this](const HydraulicLinkPipe &pipe)
    {
        syncWasmSelectedEntity(InfrastructureEntity::Pipe, pipe.uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalSelectedPump, this,
        [this](const HydraulicLinkPump &pump)
    {
        syncWasmSelectedEntity(InfrastructureEntity::Pump, pump.uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalSelectedValve, this,
        [this](const HydraulicLinkValve &valve)
    {
        syncWasmSelectedEntity(InfrastructureEntity::Valve, valve.uuid);
    });

    this->syncWasmNetworkBackground();
    this->scheduleWasmMapLayerSync();
#endif

    this->layout->addWidget(scroll_controls);
    this->layout->addWidget(this->map_stack);
    
    connect(this->map_menu, &MapMonitorMenuWidget::signalNodeVisualClicked, this,
        [this](VisualNode visual_node)
    {
        this->symbology_settings.visual_node = visual_node;
        emit signalShowMapLegendNode(visual_node);
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalNodeSizeChanged, this, [this](int size_percent)
    {
        this->symbology_settings.node_size_percent = size_percent;
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalLinkVisualClicked, this,
        [this](VisualLink visual_link)
    {
        this->symbology_settings.visual_link = visual_link;
        emit signalShowMapLegendLink(visual_link);
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalLinkThicknessChanged, this, [this](int thickness_px)
    {
        this->symbology_settings.link_thickness_px = thickness_px;
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalHeatmapVisualClicked, this, [this](VisualHeatmap visual_heatmap)
    {
        this->symbology_settings.visual_heatmap = visual_heatmap;
        emit signalShowMapLegendHeatmap(visual_heatmap);
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalHeatmapOpacityChanged, this, [this](int opacity)
    {
        this->symbology_settings.heatmap_opacity = opacity;
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalHeatmapRadiusChanged, this, [this](int radius)
    {
        this->symbology_settings.heatmap_radius_m = radius;
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalHeatmapSolidCenterChanged, this, [this](int percent)
    {
        this->symbology_settings.heatmap_solid_center_percent = percent;
        applySymbology();
    });
}

MapMonitorContainer::~MapMonitorContainer()
{
#ifdef Q_OS_WASM
    this->map->setBrowserMapLayerGeometry(QRect(), false);
#endif
}

bool MapMonitorContainer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this->map)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent *mouse_event = static_cast<QMouseEvent *>(event);
            if (mouse_event->button() == Qt::LeftButton)
            {
#ifdef Q_OS_WASM
                if (selectWasmNetworkEntityAt(mouse_event->position()))
                {
                    mouse_event->accept();
                    return true;
                }
#else
                const NetworkOverlayHit hit = this->desktop_network_overlay->hitTest(mouse_event->position());
                if (hit.isValid() && selectNetworkEntity(hit.render_id, hit.entity_type, hit.uuid))
                {
                    this->desktop_network_overlay->setSelectedEntity(hit);
                    mouse_event->accept();
                    return true;
                }
                this->desktop_network_overlay->clearSelectedEntity();
#endif
            }
        }
#ifndef Q_OS_WASM
        else if (event->type() == QEvent::MouseMove)
        {
            QMouseEvent *mouse_event = static_cast<QMouseEvent *>(event);
            updateDesktopNetworkHover(mouse_event->position(), mouse_event->buttons());
        }
        else if (event->type() == QEvent::Leave || event->type() == QEvent::Hide)
        {
            setDesktopNetworkHovered(false);
        }
#endif
    }

#ifdef Q_OS_WASM
    if (watched == this || watched == this->map || watched == this->map_stack || watched == this->window())
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
            scheduleWasmMapLayerSync();
            break;
        case QEvent::ApplicationPaletteChange:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
            this->syncWasmNetworkBackground();
            break;
        default:
            break;
        }
    }
#endif

    return QWidget::eventFilter(watched, event);
}

bool MapMonitorContainer::selectNetworkEntity(quint32 render_id, InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (this->hydraulic_data == nullptr || render_id == 0 || entity_type == InfrastructureEntity::Unknown)
        return false;

    if (!uuid.isNull())
    {
        this->hydraulic_data->setSelectedUuid(entity_type, uuid);
        return true;
    }

    const NetworkRenderSnapshot &snapshot = this->hydraulic_data->networkRenderSnapshot();
    if (entity_type == InfrastructureEntity::Junction ||
        entity_type == InfrastructureEntity::Reservoir ||
        entity_type == InfrastructureEntity::Tank)
    {
        for (const NetworkRenderNode &node : snapshot.nodes)
        {
            if (node.render_id != render_id || node.entity_type != entity_type)
                continue;

            this->hydraulic_data->setSelectedUuid(entity_type, node.uuid);
            return true;
        }
        return false;
    }

    if (entity_type == InfrastructureEntity::Pipe ||
        entity_type == InfrastructureEntity::Pump ||
        entity_type == InfrastructureEntity::Valve)
    {
        for (const NetworkRenderLink &link : snapshot.links)
        {
            if (link.render_id != render_id || link.entity_type != entity_type)
                continue;

            this->hydraulic_data->setSelectedUuid(entity_type, link.uuid);
            return true;
        }
    }

    return false;
}

#ifndef Q_OS_WASM
void MapMonitorContainer::updateDesktopNetworkHover(const QPointF &position, Qt::MouseButtons buttons)
{
    if (buttons != Qt::NoButton)
    {
        setDesktopNetworkHovered(false);
        return;
    }

    setDesktopNetworkHovered(this->desktop_network_overlay->hitTest(position).isValid());
}

void MapMonitorContainer::setDesktopNetworkHovered(bool hovered)
{
    if (this->desktop_network_hovered == hovered)
        return;

    this->desktop_network_hovered = hovered;
    if (hovered)
        this->map->setCursor(Qt::PointingHandCursor);
    else
        this->map->unsetCursor();
}
#endif

void MapMonitorContainer::applySymbology()
{
    this->symbology_settings = this->symbology_settings.bounded();

#ifndef Q_OS_WASM
    const NetworkSymbologyRanges ranges =
        this->hydraulic_data->symbologyRanges(this->symbology_settings);
    this->desktop_network_overlay->setSymbology(this->symbology_settings, ranges);
#else
    scheduleWasmNetworkSymbologySync();
#endif
}

void MapMonitorContainer::setNetworkBackgroundOpacity(int opacity)
{
    const int bounded_opacity = qBound(0, opacity, 100);
    if (this->network_background_opacity == bounded_opacity)
        return;

    this->network_background_opacity = bounded_opacity;
#ifndef Q_OS_WASM
    this->desktop_network_overlay->setBackgroundOpacity(bounded_opacity);
#else
    syncWasmNetworkBackground();
#endif
}

#ifdef Q_OS_WASM
bool MapMonitorContainer::selectWasmNetworkEntityAt(const QPointF &position)
{
    if (this->hydraulic_data == nullptr || !this->wasm_network_snapshot_sent)
        return false;

    const double packed_hit = aowisBrowserNetworkHitTest(position.x(), position.y());
    if (!std::isfinite(packed_hit) || packed_hit <= 0.0)
    {
        this->wasm_selected_entity_type = InfrastructureEntity::Unknown;
        this->wasm_selected_entity_uuid = QUuid();
        aowisBrowserNetworkSetSelectedEntity(0, 0);
        return false;
    }

    const quint64 packed = static_cast<quint64>(packed_hit);
    const int entity_type_value = static_cast<int>(packed >> 32);
    const quint32 render_id = static_cast<quint32>(packed & 0xffffffffULL);
    const InfrastructureEntity entity_type = static_cast<InfrastructureEntity>(entity_type_value);
    const bool selected = selectNetworkEntity(render_id, entity_type);
    if (selected)
        aowisBrowserNetworkSetSelectedEntity(entity_type_value, render_id);
    else
        aowisBrowserNetworkSetSelectedEntity(0, 0);
    return selected;
}

void MapMonitorContainer::syncWasmSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid)
{
    this->wasm_selected_entity_type = entity_type;
    this->wasm_selected_entity_uuid = uuid;

    if (this->hydraulic_data == nullptr || uuid.isNull())
    {
        aowisBrowserNetworkSetSelectedEntity(0, 0);
        return;
    }

    const NetworkRenderSnapshot &snapshot = this->hydraulic_data->networkRenderSnapshot();
    if (entity_type == InfrastructureEntity::Junction ||
        entity_type == InfrastructureEntity::Reservoir ||
        entity_type == InfrastructureEntity::Tank)
    {
        for (const NetworkRenderNode &node : snapshot.nodes)
        {
            if (node.uuid == uuid && node.entity_type == entity_type)
            {
                aowisBrowserNetworkSetSelectedEntity(static_cast<int>(entity_type), node.render_id);
                return;
            }
        }
    }
    else if (entity_type == InfrastructureEntity::Pipe ||
             entity_type == InfrastructureEntity::Pump ||
             entity_type == InfrastructureEntity::Valve)
    {
        for (const NetworkRenderLink &link : snapshot.links)
        {
            if (link.uuid == uuid && link.entity_type == entity_type)
            {
                aowisBrowserNetworkSetSelectedEntity(static_cast<int>(entity_type), link.render_id);
                return;
            }
        }
    }

    aowisBrowserNetworkSetSelectedEntity(0, 0);
}

void MapMonitorContainer::scheduleWasmMapLayerSync()
{
    if (!this->wasm_map_layer_sync_timer->isActive())
        this->wasm_map_layer_sync_timer->start(0);
}

void MapMonitorContainer::scheduleWasmNetworkSymbologySync()
{
    this->wasm_network_symbology_sync_retry_count = 0;
    if (!this->wasm_network_symbology_sync_timer->isActive())
        this->wasm_network_symbology_sync_timer->start(0);
}

void MapMonitorContainer::syncWasmMapLayer()
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
}

void MapMonitorContainer::syncWasmNetworkBackground()
{
    const QColor background = this->map->palette().color(QPalette::Window);
    aowisBrowserNetworkSetBackground(
        background.red(),
        background.green(),
        background.blue(),
        this->network_background_opacity);
}

void MapMonitorContainer::syncWasmNetworkSnapshot()
{
    if (this->hydraulic_data == nullptr)
        return;

    const quint64 geometry_revision = this->hydraulic_data->geometryRevision();
    if (this->wasm_network_snapshot_sent &&
        this->wasm_network_geometry_revision_sent == geometry_revision)
        return;

    const NetworkRenderSnapshot &snapshot = this->hydraulic_data->networkRenderSnapshot();
    const QByteArray json = BrowserNetworkSnapshotSerializer::serialize(snapshot);
    const int transferred = aowisBrowserNetworkSetSnapshot(
        json.constData(), static_cast<int>(json.size()));
    if (transferred == 0)
        return;

    this->wasm_network_geometry_revision_sent = snapshot.geometry_revision;
    this->wasm_network_snapshot_sent = true;
    if (!this->wasm_selected_entity_uuid.isNull())
        syncWasmSelectedEntity(this->wasm_selected_entity_type, this->wasm_selected_entity_uuid);
    scheduleWasmNetworkSymbologySync();
}

void MapMonitorContainer::syncWasmNetworkSymbology()
{
    if (this->hydraulic_data == nullptr)
        return;

    const NetworkSymbologyRanges ranges =
        this->hydraulic_data->symbologyRanges(this->symbology_settings);
    const QByteArray json = BrowserNetworkSnapshotSerializer::serializeSymbology(
        *this->hydraulic_data, this->symbology_settings, ranges);
    const int transferred = aowisBrowserNetworkSetSymbology(
        json.constData(), static_cast<int>(json.size()));
    if (transferred != 0)
    {
        this->wasm_network_symbology_sync_retry_count = 0;
        return;
    }

    if (this->wasm_network_symbology_sync_retry_count < 20)
    {
        ++this->wasm_network_symbology_sync_retry_count;
        this->wasm_network_symbology_sync_timer->start(100);
        return;
    }

    qWarning() << "Could not transfer WASM network symbology. "
                  "The browser network JavaScript is missing setSymbology(); "
                  "rebuild and reload the WASM distribution.";
}
#endif

MapWidget *MapMonitorContainer::getMap()
{
    return this->map;
}
MapNavigationWidget *MapMonitorContainer::mapNavigationWidget()
{
    return this->map_menu->mapNavigationWidget();
}





MapMonitorMenuWidget::MapMonitorMenuWidget(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) ),
    map( map ),
    map_nav( new MapNavigationWidget(this->map, CanvasMode::Monitor, this->map, this) )
{
    setContentsMargins(0, 0, 0, 0);
    setMinimumWidth(Sizes::SidebarLeftWidth);
    setMaximumWidth(Sizes::SidebarLeftWidth);
    
    this->layout->addWidget(this->map_nav);
    
    addGroupVisualSizes();
    addGroupNodeVisuals();
    addGroupLinkVisuals();
    addGroupHeatmapVisuals();
    
    this->layout->addStretch();
}

MapNavigationWidget *MapMonitorMenuWidget::mapNavigationWidget()
{
    return this->map_nav;
}

void MapMonitorMenuWidget::addGroupVisualSizes()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Visual Sizes", this);
    this->layout->addWidget(group);

    QVBoxLayout *vbox = new QVBoxLayout();
    group->setLayout(vbox);

    QLabel *label_node_section = new QLabel("<b>Node</b>");
    QLabel *label_node_size = new QLabel("Circle Size [%]");
    QSlider *slider_node_size = new SymbologySlider(50, 250, 100,
        QStringLiteral("Scales regular node circles only."), QStringLiteral(" %"), this);
    connect(slider_node_size, &QSlider::valueChanged, this, &MapMonitorMenuWidget::signalNodeSizeChanged);

    QLabel *label_link_section = new QLabel("<b>Link</b>");
    QLabel *label_link_thickness = new QLabel("Line Width [px]");
    QSlider *slider_link_thickness = new SymbologySlider(1, 12, 3,
        QStringLiteral("Sets the rendered link width and keeps link hit detection aligned with it."), QStringLiteral(" px"), this);
    connect(slider_link_thickness, &QSlider::valueChanged, this, &MapMonitorMenuWidget::signalLinkThicknessChanged);

    QLabel *label_heatmap_section = new QLabel("<b>Heatmap</b>");
    QLabel *label_heatmap_radius = new QLabel("Radius [m]");
    QSlider *slider_heatmap_radius = new SymbologySlider(10, 1000, 400,
        QStringLiteral("Sets each heatmap point's geographic influence radius. It remains consistent across zoom levels."), QStringLiteral(" m"), this);
    connect(slider_heatmap_radius, &QSlider::valueChanged, this, &MapMonitorMenuWidget::signalHeatmapRadiusChanged);

    QLabel *label_heatmap_solid_center = new QLabel("Solid Center [%]");
    QSlider *slider_heatmap_solid_center = new SymbologySlider(0, 100, 70,
        QStringLiteral("Sets how much of the heatmap radius keeps full local opacity before fading to transparency."), QStringLiteral(" %"), this);
    connect(slider_heatmap_solid_center, &QSlider::valueChanged, this, &MapMonitorMenuWidget::signalHeatmapSolidCenterChanged);

    QLabel *label_heatmap_opacity = new QLabel("Opacity [%]");
    QSlider *slider_heatmap_opacity = new SymbologySlider(0, 100, 75,
        QStringLiteral("Controls the opacity of the complete heatmap layer."), QStringLiteral(" %"), this);
    connect(slider_heatmap_opacity, &QSlider::valueChanged, this, &MapMonitorMenuWidget::signalHeatmapOpacityChanged);

    vbox->addWidget(label_node_section);
    vbox->addWidget(label_node_size);
    vbox->addWidget(slider_node_size);
    vbox->addSpacing(4);
    vbox->addWidget(label_link_section);
    vbox->addWidget(label_link_thickness);
    vbox->addWidget(slider_link_thickness);
    vbox->addSpacing(4);
    vbox->addWidget(label_heatmap_section);
    vbox->addWidget(label_heatmap_radius);
    vbox->addWidget(slider_heatmap_radius);
    vbox->addWidget(label_heatmap_solid_center);
    vbox->addWidget(slider_heatmap_solid_center);
    vbox->addWidget(label_heatmap_opacity);
    vbox->addWidget(slider_heatmap_opacity);
}

void MapMonitorMenuWidget::addGroupNodeVisuals()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Node Symbology", this);
    this->layout->addWidget(group);
    QVBoxLayout *vbox = new QVBoxLayout();
    group->setLayout(vbox);
    
    QRadioButton *radio_node_none = new QRadioButton("None");
    radio_node_none->setChecked(true);
    QRadioButton *radio_node_elevation = new QRadioButton("Elevation");
    QRadioButton *radio_node_basedemand = new QRadioButton("Base Demand");
    QRadioButton *radio_node_totaldemand = new QRadioButton("Total Demand");
    QRadioButton *radio_node_demanddeficit = new QRadioButton("Demand Deficit");
    QRadioButton *radio_node_emitterflow = new QRadioButton("Emitter Flow");
    QRadioButton *radio_node_leakage = new QRadioButton("Leakage");
    QRadioButton *radio_node_head = new QRadioButton("Head");
    QRadioButton *radio_node_pressure = new QRadioButton("Pressure");
    
    QLabel *label_multispecies = new QLabel(
        "EPANET Network 3 has<br>"
        "Multi-Species Water<br>"
        "Quality Analysis (MSX).<br>"
        "Modeled variables are:"
    );
    
    QRadioButton *radio_node_chlorine = new QRadioButton("Cl₂ [mg/L]");
    QRadioButton *radio_node_river = new QRadioButton("River Water [%]");
    QRadioButton *radio_node_lake = new QRadioButton("Lake Water [%]");
    
    QLabel *label_chlorine = new QLabel(
        "Chlorine decay constant<br>"
        "depends on the mix of<br>"
        "Lake and River water."
    );
    
    const std::function<void(QRadioButton *, VisualNode)> connect_node_visual =
        [this](QRadioButton *button, VisualNode visual)
    {
        connect(button, &QRadioButton::clicked, this, [this, visual]
        {
            emit signalNodeVisualClicked(visual);
        });
    };
    connect_node_visual(radio_node_none, VisualNode::None);
    connect_node_visual(radio_node_elevation, VisualNode::Elevation);
    connect_node_visual(radio_node_basedemand, VisualNode::BaseDemand);
    connect_node_visual(radio_node_totaldemand, VisualNode::TotalDemand);
    connect_node_visual(radio_node_demanddeficit, VisualNode::DemandDeficit);
    connect_node_visual(radio_node_emitterflow, VisualNode::EmitterFlow);
    connect_node_visual(radio_node_leakage, VisualNode::Leakage);
    connect_node_visual(radio_node_head, VisualNode::Head);
    connect_node_visual(radio_node_pressure, VisualNode::Pressure);
    connect_node_visual(radio_node_chlorine, VisualNode::Chlorine);
    connect_node_visual(radio_node_river, VisualNode::RiverWater);
    connect_node_visual(radio_node_lake, VisualNode::LakeWater);
    
    vbox->addWidget(radio_node_none);
    vbox->addWidget(radio_node_elevation);
    vbox->addWidget(radio_node_basedemand);
    vbox->addWidget(radio_node_totaldemand);
    vbox->addWidget(radio_node_demanddeficit);
    vbox->addWidget(radio_node_emitterflow);
    vbox->addWidget(radio_node_leakage);
    vbox->addWidget(radio_node_head);
    vbox->addWidget(radio_node_pressure);

    vbox->addWidget(label_multispecies);
    
    vbox->addWidget(radio_node_chlorine);
    vbox->addWidget(radio_node_river);
    vbox->addWidget(radio_node_lake);
    
    vbox->addWidget(label_chlorine);
}
void MapMonitorMenuWidget::addGroupLinkVisuals()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Link Symbology", this);
    this->layout->addWidget(group);
    QVBoxLayout *vbox = new QVBoxLayout();
    group->setLayout(vbox);
    
    QCheckBox *check_flow_direction = new QCheckBox("Show Flow Direction");
    check_flow_direction->setChecked(true);
    
    QRadioButton *radio_link_none = new QRadioButton("None");
    radio_link_none->setChecked(true);
    QRadioButton *radio_link_diameter = new QRadioButton("Diameter");
    QRadioButton *radio_link_length = new QRadioButton("Length");
    QRadioButton *radio_link_roughness = new QRadioButton("Roughness");
    QRadioButton *radio_link_flowrate = new QRadioButton("Flow Rate");
    QRadioButton *radio_link_velocity = new QRadioButton("Velocity");
    QRadioButton *radio_link_headloss = new QRadioButton("Head Loss");
    QRadioButton *radio_link_leakage = new QRadioButton("Leakage");
    QRadioButton *radio_link_chlorine = new QRadioButton("Cl₂ [mg/L]");
    QRadioButton *radio_link_river = new QRadioButton("River Water [%]");
    QRadioButton *radio_link_lake = new QRadioButton("Lake Water [%]");
    
    const std::function<void(QRadioButton *, VisualLink)> connect_link_visual =
        [this](QRadioButton *button, VisualLink visual)
    {
        connect(button, &QRadioButton::clicked, this, [this, visual]
        {
            emit signalLinkVisualClicked(visual);
        });
    };
    connect_link_visual(radio_link_none, VisualLink::None);
    connect_link_visual(radio_link_diameter, VisualLink::Diameter);
    connect_link_visual(radio_link_length, VisualLink::Length);
    connect_link_visual(radio_link_roughness, VisualLink::Roughness);
    connect_link_visual(radio_link_flowrate, VisualLink::FlowRate);
    connect_link_visual(radio_link_velocity, VisualLink::Velocity);
    connect_link_visual(radio_link_headloss, VisualLink::HeadLoss);
    connect_link_visual(radio_link_leakage, VisualLink::Leakage);
    connect_link_visual(radio_link_chlorine, VisualLink::Chlorine);
    connect_link_visual(radio_link_river, VisualLink::RiverWater);
    connect_link_visual(radio_link_lake, VisualLink::LakeWater);
    
    vbox->addWidget(check_flow_direction);
    
    vbox->addWidget(radio_link_none);
    vbox->addWidget(radio_link_diameter);
    vbox->addWidget(radio_link_length);
    vbox->addWidget(radio_link_roughness);
    vbox->addWidget(radio_link_flowrate);
    vbox->addWidget(radio_link_velocity);
    vbox->addWidget(radio_link_headloss);
    vbox->addWidget(radio_link_leakage);
    vbox->addWidget(radio_link_chlorine);
    vbox->addWidget(radio_link_river);
    vbox->addWidget(radio_link_lake);
}

void MapMonitorMenuWidget::addGroupHeatmapVisuals()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Heatmap", this);
    this->layout->addWidget(group);
    
    QVBoxLayout *vbox = new QVBoxLayout();
    group->setLayout(vbox);
    
    QRadioButton *radio_none = new QRadioButton("None");
    radio_none->setChecked(true);
    
    QRadioButton *radio_elevation = new QRadioButton("Elevation");
    QRadioButton *radio_total_demand = new QRadioButton("Total Demand");
    QRadioButton *radio_demand_deficit = new QRadioButton("Demand Deficit");
    QRadioButton *radio_leakage = new QRadioButton("Leakage");
    QRadioButton *radio_head = new QRadioButton("Head");
    QRadioButton *radio_pressure = new QRadioButton("Pressure");
    QRadioButton *radio_chlorine = new QRadioButton("Cl₂ [mg/L]");
    QRadioButton *radio_river = new QRadioButton("River Water [%]");
    QRadioButton *radio_lake = new QRadioButton("Lake Water [%]");
    
    const std::function<void(QRadioButton *, VisualHeatmap)> connect_heatmap_visual =
        [this](QRadioButton *button, VisualHeatmap visual)
    {
        connect(button, &QRadioButton::clicked, this, [this, visual]
        {
            emit signalHeatmapVisualClicked(visual);
        });
    };
    
    connect_heatmap_visual(radio_none, VisualHeatmap::None);
    connect_heatmap_visual(radio_elevation, VisualHeatmap::Elevation);
    connect_heatmap_visual(radio_total_demand, VisualHeatmap::TotalDemand);
    connect_heatmap_visual(radio_demand_deficit, VisualHeatmap::DemandDeficit);
    connect_heatmap_visual(radio_leakage, VisualHeatmap::Leakage);
    connect_heatmap_visual(radio_head, VisualHeatmap::Head);
    connect_heatmap_visual(radio_pressure, VisualHeatmap::Pressure);
    connect_heatmap_visual(radio_chlorine, VisualHeatmap::Chlorine);
    connect_heatmap_visual(radio_river, VisualHeatmap::RiverWater);
    connect_heatmap_visual(radio_lake, VisualHeatmap::LakeWater);

    vbox->addWidget(radio_none);
    vbox->addWidget(radio_elevation);
    vbox->addWidget(radio_total_demand);
    vbox->addWidget(radio_demand_deficit);
    vbox->addWidget(radio_leakage);
    vbox->addWidget(radio_head);
    vbox->addWidget(radio_pressure);
    vbox->addWidget(radio_chlorine);
    vbox->addWidget(radio_river);
    vbox->addWidget(radio_lake);
}
