#include "tab_map_monitor_container.h"

MapMonitorContainer::MapMonitorContainer(MapModel *map_model, GpsProvider *gps, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    gps( gps ),
    map_model( map_model ),
    map( new MapWidget(this->map_model, gps, this) ),
    controls( new MapMonitorMenuWidget(this->map, this) )
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
    
    scroll_controls->setWidget(this->controls);
    
    this->layout->addWidget(scroll_controls);
    this->layout->addWidget(map);
    
    connect(this->controls, &MapMonitorMenuWidget::signalNodeVisualClicked,
        this, &MapMonitorContainer::signalShowMapLegendNode);
    connect(this->controls, &MapMonitorMenuWidget::signalLinkVisualClicked,
        this, &MapMonitorContainer::signalShowMapLegendLink);
    connect(this->controls, &MapMonitorMenuWidget::signalHeatmapVisualClicked,
        this, &MapMonitorContainer::signalShowMapLegendHeatmap);
}
MapWidget *MapMonitorContainer::getMap()
{
    return this->map;
}





MapMonitorMenuWidget::MapMonitorMenuWidget(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) ),
    map( map ),
    map_nav( new MapNavigationWidget(this->map, CanvasMode::Monitor) )
{
    setContentsMargins(0, 0, 0, 0);
    setMinimumWidth(Sizes::SidebarLeftWidth);
    setMaximumWidth(Sizes::SidebarLeftWidth);
    
    this->layout->addWidget(this->map_nav);
    
    addGroupLinkVisuals();
    addGroupNodeVisuals();
    addGroupHeatmapVisuals();
    
    this->layout->addStretch();
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
