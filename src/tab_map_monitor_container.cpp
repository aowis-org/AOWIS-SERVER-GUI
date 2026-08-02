#include "tab_map_monitor_container.h"

#include "hydraulic_data.h"

#include <cmath>

#ifdef Q_OS_WASM
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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

namespace
{
QJsonArray coordinateToJson(const CoordinateWGS84 &coordinate)
{
    QJsonArray result;
    result.append(coordinate.longitude_deg);
    result.append(coordinate.latitude_deg);
    return result;
}

QByteArray serializeNetworkRenderSnapshot(const NetworkRenderSnapshot &snapshot)
{
    QJsonArray nodes;
    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        QJsonArray node_json;
        node_json.append(static_cast<double>(node.render_id));
        node_json.append(static_cast<int>(node.entity_type));
        node_json.append(node.uuid.toString(QUuid::WithoutBraces));
        node_json.append(node.id);
        node_json.append(node.coordinate_wgs84.longitude_deg);
        node_json.append(node.coordinate_wgs84.latitude_deg);
        nodes.append(node_json);
    }

    QJsonArray links;
    for (const NetworkRenderLink &link : snapshot.links)
    {
        QJsonArray vertices;
        for (const CoordinateWGS84 &coordinate : link.vertices_wgs84)
            vertices.append(coordinateToJson(coordinate));

        QJsonArray link_json;
        link_json.append(static_cast<double>(link.render_id));
        link_json.append(static_cast<int>(link.entity_type));
        link_json.append(link.uuid.toString(QUuid::WithoutBraces));
        link_json.append(link.id);
        link_json.append(static_cast<double>(link.start_node_render_id));
        link_json.append(static_cast<double>(link.end_node_render_id));
        link_json.append(vertices);
        links.append(link_json);
    }

    QJsonObject root;
    root.insert(QStringLiteral("geometryRevision"),
                QString::number(snapshot.geometry_revision));
    root.insert(QStringLiteral("visualRevision"),
                QString::number(snapshot.visual_revision));
    root.insert(QStringLiteral("nodes"), nodes);
    root.insert(QStringLiteral("links"), links);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
}
#endif

MapMonitorContainer::MapMonitorContainer(MapModel *map_model, MapTileRepository *tile_repository, HydraulicData *hydraulic_data, GpsProvider *gps, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    gps( gps ),
    map_model( map_model ),
    tile_repository( tile_repository ),
    hydraulic_data( hydraulic_data ),
    map( new MapWidget(this->map_model, this->tile_repository, this->gps, this) ),
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
        auto sb = scroll_controls->verticalScrollBar();
        bool should_show = sb->maximum() > sb->minimum();
        
        int width_base = Sizes::SidebarLeftWidth;
        int width_sb = sb->sizeHint().width();
        int w = should_show ? width_base + width_sb : width_base;
        
        scroll_controls->setMinimumWidth(w);
        scroll_controls->setMaximumWidth(w);
    });
    
    scroll_controls->setWidget(this->map_menu);

#ifdef Q_OS_WASM
    this->map->setBrowserMapLayerEnabled(true);
    this->map->setBrowserMapLayerTopmost(true);

    this->wasm_map_layer_sync_timer = new QTimer(this);
    this->wasm_map_layer_sync_timer->setSingleShot(true);
    connect(this->wasm_map_layer_sync_timer, &QTimer::timeout, this, &MapMonitorContainer::syncWasmMapLayer);

    this->installEventFilter(this);
    this->map->installEventFilter(this);
    if (this->window())
        this->window()->installEventFilter(this);

    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded,
        this, &MapMonitorContainer::scheduleWasmMapLayerSync);

    this->scheduleWasmMapLayerSync();
#endif

    this->layout->addWidget(scroll_controls);
    this->layout->addWidget(this->map);
    
    connect(this->map_menu, &MapMonitorMenuWidget::signalNodeVisualClicked,
        this, &MapMonitorContainer::signalShowMapLegendNode);
    connect(this->map_menu, &MapMonitorMenuWidget::signalLinkVisualClicked,
        this, &MapMonitorContainer::signalShowMapLegendLink);
    connect(this->map_menu, &MapMonitorMenuWidget::signalHeatmapVisualClicked,
        this, &MapMonitorContainer::signalShowMapLegendHeatmap);
}

MapMonitorContainer::~MapMonitorContainer()
{
#ifdef Q_OS_WASM
    this->map->setBrowserMapLayerGeometry(QRect(), false);
#endif
}

#ifdef Q_OS_WASM
bool MapMonitorContainer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this->map)
    {
        if (event->type() == QEvent::MouseMove)
        {
            QMouseEvent *mouse_event = static_cast<QMouseEvent *>(event);
            updateWasmNetworkHoverCursor(mouse_event->position(), mouse_event->buttons());
        }
        else if (event->type() == QEvent::Leave || event->type() == QEvent::Hide)
        {
            clearWasmNetworkHoverCursor();
        }
        else if (event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent *mouse_event = static_cast<QMouseEvent *>(event);
            clearWasmNetworkHoverCursor();
            if (mouse_event->button() == Qt::LeftButton && selectWasmNetworkEntityAt(mouse_event->position()))
            {
                mouse_event->accept();
                return true;
            }
        }
    }

    if (watched == this || watched == this->map || watched == this->window())
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
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}


void MapMonitorContainer::updateWasmNetworkHoverCursor(const QPointF &position, Qt::MouseButtons buttons)
{
    const bool entity_hovered = buttons == Qt::NoButton && this->wasm_network_snapshot_sent
        && aowisBrowserNetworkHitTest(position.x(), position.y()) > 0.0;
    if (entity_hovered == this->wasm_network_hover_cursor_active)
        return;

    this->wasm_network_hover_cursor_active = entity_hovered;
    if (entity_hovered)
        this->map->setCursor(Qt::PointingHandCursor);
    else
        this->map->unsetCursor();
}

void MapMonitorContainer::clearWasmNetworkHoverCursor()
{
    if (!this->wasm_network_hover_cursor_active)
        return;

    this->wasm_network_hover_cursor_active = false;
    this->map->unsetCursor();
}

bool MapMonitorContainer::selectWasmNetworkEntityAt(const QPointF &position)
{
    if (this->hydraulic_data == nullptr || !this->wasm_network_snapshot_sent)
        return false;

    const double packed_hit = aowisBrowserNetworkHitTest(position.x(), position.y());
    if (!std::isfinite(packed_hit) || packed_hit <= 0.0)
        return false;

    const quint64 packed = static_cast<quint64>(packed_hit);
    const int entity_type_value = static_cast<int>(packed >> 32);
    const quint32 render_id = static_cast<quint32>(packed & 0xffffffffULL);
    const InfrastructureEntity entity_type = static_cast<InfrastructureEntity>(entity_type_value);
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

void MapMonitorContainer::scheduleWasmMapLayerSync()
{
    if (!this->wasm_map_layer_sync_timer->isActive())
        this->wasm_map_layer_sync_timer->start(0);
}

void MapMonitorContainer::syncWasmMapLayer()
{
    const bool visible = this->isVisible() && this->map->isVisible()
        && this->map->width() > 0 && this->map->height() > 0;

    if (!visible)
    {
        clearWasmNetworkHoverCursor();
        this->map->setBrowserMapLayerGeometry(QRect(), false);
        return;
    }

    const QRect map_geometry(this->map->mapToGlobal(QPoint(0, 0)), this->map->size());
    this->map->setBrowserMapLayerGeometry(map_geometry, true);
    this->syncWasmNetworkSnapshot();
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
    const QByteArray json = serializeNetworkRenderSnapshot(snapshot);
    const int transferred = aowisBrowserNetworkSetSnapshot(
        json.constData(), static_cast<int>(json.size()));
    if (transferred == 0)
        return;

    clearWasmNetworkHoverCursor();
    this->wasm_network_geometry_revision_sent = snapshot.geometry_revision;
    this->wasm_network_snapshot_sent = true;
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
    
    addGroupNodeVisuals();
    addGroupLinkVisuals();
    addGroupHeatmapVisuals();
    
    this->layout->addStretch();
}

MapNavigationWidget *MapMonitorMenuWidget::mapNavigationWidget()
{
    return this->map_nav;
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
    
    const auto connect_node_visual = [this](QRadioButton *button, VisualNode visual)
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
    
    const auto connect_link_visual = [this](QRadioButton *button, VisualLink visual)
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
    
    QLabel *label_slider_opacity = new QLabel("Opacity");
    QSlider *slider_opacity = new QSlider(Qt::Horizontal);
    slider_opacity->setRange(0, 100);
    slider_opacity->setValue(55);
    
    QLabel *label_slider_radius = new QLabel("Radius");
    QSlider *slider_radius = new QSlider(Qt::Horizontal);
    slider_radius->setRange(10, 500);
    slider_radius->setValue(100);
    
    const auto connect_heatmap_visual =
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
    
    vbox->addWidget(label_slider_opacity);
    vbox->addWidget(slider_opacity);
    vbox->addWidget(label_slider_radius);
    vbox->addWidget(slider_radius);
}
