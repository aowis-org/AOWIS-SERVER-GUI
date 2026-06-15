#include "map_monitor_container.h"

MapMonitorContainer::MapMonitorContainer(MapModel *map_model, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) )
{
    this->map_model = map_model;
    this->map = new MapWidget(this->map_model, this);
    
    this->controls = new MapNavigation(this->map, this);
    
    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    scroll_controls->setMinimumWidth(180);
    scroll_controls->setMaximumWidth(200);
    scroll_controls->setWidgetResizable(true);
    scroll_controls->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_controls->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    // unfortunately we need to fix widget width depending on scrollbar visible with some black magic
    connect(scroll_controls->verticalScrollBar(), &QScrollBar::rangeChanged, this, [scroll_controls]
    {
        auto sb = scroll_controls->verticalScrollBar();
        bool should_show = sb->maximum() > sb->minimum();
        
        int width_base = 180;
        int width_sb = sb->sizeHint().width();
        int w = should_show ? width_base + width_sb : width_base;
        
        scroll_controls->setMinimumWidth(w);
        scroll_controls->setMaximumWidth(w);
    });
    
    scroll_controls->setWidget(this->controls);
    
    this->layout->addWidget(scroll_controls);
    this->layout->addWidget(map);
}





MapNavigation::MapNavigation(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) )
{
    this->map = map;
    
    //this->layout->setContentsMargins(0, 0, 0, 0);
    setContentsMargins(0, 0, 0, 0);
    setMinimumWidth(180);
    setMaximumWidth(200);
    
    addGroupMapNavigation();
    addGroupNodeVisuals();
    addGroupLinkVisuals();
    
    this->layout->addStretch();
}

void MapNavigation::addGroupMapNavigation()
{
    QGroupBox *group = new QGroupBox("Map Controls", this);
    this->layout->addWidget(group);
    QGridLayout *grid = new QGridLayout();
    group->setLayout(grid);
    //group->setContentsMargins(0, 10, 0, 0);
    
    QPushButton *button_zoom_in = new QPushButton();
    button_zoom_in->setIcon(QIcon(":/icons/zoom-in.svg"));
    button_zoom_in->setIconSize(QSize(30, 30));
    
    QPushButton *button_zoom_out = new QPushButton();
    button_zoom_out->setIcon(QIcon(":/icons/zoom-out.svg"));
    button_zoom_out->setIconSize(QSize(30, 30));
    
    connect(button_zoom_in, &QPushButton::clicked, this->map, &MapWidget::zoomIn);
    connect(button_zoom_out, &QPushButton::clicked, this->map, &MapWidget::zoomOut);
    
    QRadioButton* map_arcgissat = new QRadioButton("ArcGIS SAT");
    QRadioButton* map_opentopomap = new QRadioButton("OpenTopoMap");
    QRadioButton* map_openstreetmap = new QRadioButton("OpenStreetMap");
    
    map_arcgissat->setChecked(true);
    
    connect(map_arcgissat, &QRadioButton::clicked, this, [this]
            { this->map->changeMapProvider(MapProvider::ArcGISSat); });
    connect(map_opentopomap, &QRadioButton::clicked, this, [this]
            { this->map->changeMapProvider(MapProvider::OpenTopoMap); });
    connect(map_openstreetmap, &QRadioButton::clicked, this, [this]
            { this->map->changeMapProvider(MapProvider::OpenStreetMap); });
    
    QLabel *label_slider_map_visibility = new QLabel("Opacity");
    QSlider *slider_map_visibility = new QSlider(Qt::Horizontal);
    
    grid->addWidget(button_zoom_in, 0, 0);
    grid->addWidget(button_zoom_out, 0, 1);
    
    grid->addWidget(map_arcgissat, 1, 0, 1, 2);
    grid->addWidget(map_opentopomap, 2, 0, 1, 2);
    grid->addWidget(map_openstreetmap, 3, 0, 1, 2);
    grid->addWidget(label_slider_map_visibility, 4, 0, 1, 2);
    grid->addWidget(slider_map_visibility, 5, 0, 1, 2);
}
void MapNavigation::addGroupNodeVisuals()
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
    QRadioButton *radio_node_chlorine = new QRadioButton("Cl₂");
    QRadioButton *radio_node_river = new QRadioButton("River");
    QRadioButton *radio_node_lake = new QRadioButton("Lake");
    
    vbox->addWidget(radio_node_none);
    vbox->addWidget(radio_node_elevation);
    vbox->addWidget(radio_node_basedemand);
    vbox->addWidget(radio_node_totaldemand);
    vbox->addWidget(radio_node_demanddeficit);
    vbox->addWidget(radio_node_emitterflow);
    vbox->addWidget(radio_node_leakage);
    vbox->addWidget(radio_node_head);
    vbox->addWidget(radio_node_pressure);
    vbox->addWidget(radio_node_chlorine);
    vbox->addWidget(radio_node_river);
    vbox->addWidget(radio_node_lake);
    
    makeGroupCollapsable(group);
}
void MapNavigation::addGroupLinkVisuals()
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
    QRadioButton *radio_link_chlorine = new QRadioButton("Cl₂");
    QRadioButton *radio_link_river = new QRadioButton("River");
    QRadioButton *radio_link_lake = new QRadioButton("Lake");
    
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

void MapNavigation::makeGroupCollapsable(QGroupBox *group)
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

