#include "tab_map_monitor_container.h"

MapMonitorContainer::MapMonitorContainer(MapModel *map_model, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    map_model( map_model ),
    map( new MapWidget(this->map_model, this) ),
    controls( new MapMonitorMenuWidget(this->map, this) )
{
    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    scroll_controls->setMinimumWidth(Sizes::SidebarLeftWidthBase);
    scroll_controls->setMaximumWidth(Sizes::SidebarLeftWidthBase);
    scroll_controls->setWidgetResizable(true);
    scroll_controls->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_controls->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    // unfortunately we need to fix widget width depending on scrollbar visible with some black magic
    connect(scroll_controls->verticalScrollBar(), &QScrollBar::rangeChanged, this, [scroll_controls]
    {
        auto sb = scroll_controls->verticalScrollBar();
        bool should_show = sb->maximum() > sb->minimum();
        
        int width_base = Sizes::SidebarLeftWidthBase;
        int width_sb = sb->sizeHint().width();
        int w = should_show ? width_base + width_sb : width_base;
        
        scroll_controls->setMinimumWidth(w);
        scroll_controls->setMaximumWidth(w);
    });
    
    scroll_controls->setWidget(this->controls);
    
    this->layout->addWidget(scroll_controls);
    this->layout->addWidget(map);
}
MapWidget *MapMonitorContainer::getMap()
{
    return this->map;
}





MapMonitorMenuWidget::MapMonitorMenuWidget(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) )
{
    this->map = map;
    
    setContentsMargins(0, 0, 0, 0);
    setMinimumWidth(Sizes::SidebarLeftWidthBase);
    setMaximumWidth(Sizes::SidebarLeftWidthBase);
    
    this->map_nav = new MapNavigationWidget(this->map);
    
    this->layout->addWidget(this->map_nav);
    
    addGroupLinkVisuals();
    addGroupNodeVisuals();
    
    this->layout->addStretch();
}


void MapMonitorMenuWidget::addGroupNodeVisuals()
{
    QGroupBox *group = new QGroupBox("Node Symbology", this);
    group->setCheckable(true);
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
    
    makeGroupCollapsable(group);
}
void MapMonitorMenuWidget::addGroupLinkVisuals()
{
    QGroupBox *group = new QGroupBox("Link Symbology", this);
    group->setCheckable(true);
    this->layout->addWidget(group);
    QVBoxLayout *vbox = new QVBoxLayout();
    group->setLayout(vbox);
    
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
    
    makeGroupCollapsable(group);
}

void MapMonitorMenuWidget::makeGroupCollapsable(QGroupBox *group)
{
    connect(group, &QGroupBox::toggled, group, [group](bool expanded){
        for (QObject *obj : group->children()) {
            
            // Only affect actual widgets, not layouts
            QWidget *w = qobject_cast<QWidget*>(obj);
            if (!w)
                continue;
            
            // Don't hide the group box itself
            if (w == group)
                continue;
            
            w->setVisible(expanded);
        }
    });
}

