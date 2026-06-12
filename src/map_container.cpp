#include "map_container.h"

MapContainer::MapContainer(QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    map( new MapWidget(this) )
{
    this->controls = new MapControls(this->map, this);
    
    setContentsMargins(0, 0, 0, 0);
    
    this->layout->addWidget(controls);
    this->layout->addWidget(map);
}

MapWidget* MapContainer::mapWidget()
{
    return this->map;
}





MapControls::MapControls(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    layout( new QGridLayout(this) )
{
    this->map = map;
    
    setContentsMargins(0, 0, 0, 0);
    setMinimumWidth(180);
    setMaximumWidth(180);
    
    this->button_zoom_in = new QPushButton();
    this->button_zoom_in->setIcon(QIcon(":/icons/zoom-in.svg"));
    this->button_zoom_in->setIconSize(QSize(30, 30));
    
    this->button_zoom_out = new QPushButton();
    this->button_zoom_out->setIcon(QIcon(":/icons/zoom-out.svg"));
    this->button_zoom_out->setIconSize(QSize(30, 30));
    
    connect(this->button_zoom_in, &QPushButton::clicked, this->map, &MapWidget::zoomIn);
    connect(this->button_zoom_out, &QPushButton::clicked, this->map, &MapWidget::zoomOut);
    
    
    QButtonGroup* group = new QButtonGroup(this);
    group->setExclusive(true);
    
    QRadioButton* map_arcgissat = new QRadioButton("ArcGIS SAT");
    QRadioButton* map_opentopomap = new QRadioButton("OpenTopoMap");
    QRadioButton* map_openstreetmap = new QRadioButton("OpenStreetMap");
    
    group->addButton(map_arcgissat);
    group->addButton(map_opentopomap);
    group->addButton(map_openstreetmap);
    
    map_arcgissat->setChecked(true);
    
    connect(map_arcgissat, &QRadioButton::clicked, this, [this]
            { this->map->changeMapProvider(MapProvider::ArcGISSat); });
    connect(map_opentopomap, &QRadioButton::clicked, this, [this]
            { this->map->changeMapProvider(MapProvider::OpenTopoMap); });
    connect(map_openstreetmap, &QRadioButton::clicked, this, [this]
            { this->map->changeMapProvider(MapProvider::OpenStreetMap); });
    
    
    this->layout->addWidget(this->button_zoom_in, 0, 0);
    this->layout->addWidget(this->button_zoom_out, 0, 1);
    
    this->layout->addWidget(map_arcgissat, 1, 0, 1, 2);
    this->layout->addWidget(map_opentopomap, 2, 0, 1, 2);
    this->layout->addWidget(map_openstreetmap, 3, 0, 1, 2);
    
    this->layout->setRowStretch(100, 1);
}



