#include "map_navigation_widget.h"

MapNavigationWidget::MapNavigationWidget(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    grid( new QGridLayout(this) )
{
    this->map = map;
    
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
    
    this->grid->addWidget(button_zoom_in, 0, 0);
    this->grid->addWidget(button_zoom_out, 0, 1);
    
    this->grid->addWidget(map_arcgissat, 1, 0, 1, 2);
    this->grid->addWidget(map_opentopomap, 2, 0, 1, 2);
    this->grid->addWidget(map_openstreetmap, 3, 0, 1, 2);
    this->grid->addWidget(label_slider_map_visibility, 4, 0, 1, 2);
    this->grid->addWidget(slider_map_visibility, 5, 0, 1, 2);
}
