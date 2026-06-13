#include "map_container.h"

MapContainer::MapContainer(QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    map( new MapWidget(this) )
{
    this->controls = new MapControls(this->map, this);
    
    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    
    this->layout->addWidget(controls);
    this->layout->addWidget(map);
}

MapWidget* MapContainer::mapWidget()
{
    return this->map;
}





MapControls::MapControls(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) )
{
    this->map = map;
    
    //this->layout->setContentsMargins(0, 0, 0, 0);
    setContentsMargins(0, 0, 0, 0);
    setMinimumWidth(160);
    setMaximumWidth(160);
    
    addGroupMapControls();
    
    this->layout->addStretch();
}

void MapControls::addGroupMapControls()
{
    QGroupBox *group = new QGroupBox("Map Controls", this);
    this->layout->addWidget(group);
    QGridLayout *grid = new QGridLayout(this);
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
    
    
    QButtonGroup* group_button = new QButtonGroup(this);
    group_button->setExclusive(true);
    
    QRadioButton* map_arcgissat = new QRadioButton("ArcGIS SAT");
    QRadioButton* map_opentopomap = new QRadioButton("OpenTopoMap");
    QRadioButton* map_openstreetmap = new QRadioButton("OpenStreetMap");
    
    group_button->addButton(map_arcgissat);
    group_button->addButton(map_opentopomap);
    group_button->addButton(map_openstreetmap);
    
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

