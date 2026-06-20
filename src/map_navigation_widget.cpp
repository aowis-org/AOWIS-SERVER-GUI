#include "map_navigation_widget.h"

MapNavigationWidget::MapNavigationWidget(MapWidget *map, CanvasMode mode, QWidget *parent)
    : QWidget{parent},
    grid( new QGridLayout(this) ),
    map( map ),
    mode( mode )
{
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
    QRadioButton* map_osmcyclo = new QRadioButton("CycloOSM");
    
    map_arcgissat->setChecked(true);
    
    connect(map_arcgissat, &QRadioButton::clicked, this, [this]
            { this->map->changeMapProvider(MapProvider::ArcGISSat); });
    connect(map_opentopomap, &QRadioButton::clicked, this, [this]
            { this->map->changeMapProvider(MapProvider::OpenTopoMap); });
    connect(map_openstreetmap, &QRadioButton::clicked, this, [this]
            { this->map->changeMapProvider(MapProvider::OpenStreetMap); });
    connect(map_osmcyclo, &QRadioButton::clicked, this, [this]
            { this->map->changeMapProvider(MapProvider::OSMCyclo); });
    
    QLabel *label_slider_map_visibility = new QLabel("Opacity");
    QSlider *slider_map_visibility = new QSlider(Qt::Horizontal);
    connect(slider_map_visibility, &QSlider::valueChanged, this, &MapNavigationWidget::signalSlideOpacityChanged);
    
    this->grid->addWidget(button_zoom_in, 0, 0);
    this->grid->addWidget(button_zoom_out, 0, 1);
    
    this->grid->addWidget(map_arcgissat, 1, 0, 1, 2);
    this->grid->addWidget(map_opentopomap, 2, 0, 1, 2);
    this->grid->addWidget(map_openstreetmap, 3, 0, 1, 2);
    this->grid->addWidget(map_osmcyclo, 4, 0, 1, 2);
    this->grid->addWidget(label_slider_map_visibility, 5, 0, 1, 2);
    this->grid->addWidget(slider_map_visibility, 6, 0, 1, 2);
    
    // setting it to 50 on init
    if (this->mode == CanvasMode::Edit)
    {
        slider_map_visibility->setValue(60);
        QTimer::singleShot(0, this, [this, slider_map_visibility]()
        {
            emit signalSlideOpacityChanged(slider_map_visibility->value());
        });
    }
}
