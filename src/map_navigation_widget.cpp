#include "map_navigation_widget.h"

MapNavigationWidget::MapNavigationWidget(MapWidget *map, CanvasMode mode, QWidget *parent)
    : QWidget{parent},
    grid( new QGridLayout(this) ),
    map( map ),
    mode( mode )
{
    this->button_zoom_in = new QPushButton();
    this->button_zoom_in->setIcon(QIcon(":/icon/zoom_in.png"));
    this->button_zoom_in->setIconSize(QSize(30, 30));
    this->button_zoom_in->setToolTip("Hotkey: e");
    
    this->button_zoom_out = new QPushButton();
    this->button_zoom_out->setIcon(QIcon(":/icon/zoom_out.png"));
    this->button_zoom_out->setIconSize(QSize(30, 30));
    this->button_zoom_out->setToolTip("Hotkey: q");
    
    this->button_up = new QPushButton();
    this->button_up->setIcon(QIcon(":/icon/arrow_up"));
    this->button_up->setIconSize(QSize(25, 25));
    this->button_up->setToolTip("Hotkey: w");
    
    this->button_down = new QPushButton();
    this->button_down->setIcon(QIcon(":/icon/arrow_down"));
    this->button_down->setIconSize(QSize(25, 25));
    this->button_down->setToolTip("Hotkey: s");
    
    this->button_left = new QPushButton();
    this->button_left->setIcon(QIcon(":/icon/arrow_left"));
    this->button_left->setIconSize(QSize(25, 25));
    this->button_left->setToolTip("Hotkey: a");
    
    this->button_right = new QPushButton();
    this->button_right->setIcon(QIcon(":/icon/arrow_right"));
    this->button_right->setIconSize(QSize(25, 25));
    this->button_right->setToolTip("Hotkey: d");
    
    connect(button_zoom_in, &QPushButton::clicked, this->map, &MapWidget::zoomIn);
    connect(button_zoom_out, &QPushButton::clicked, this->map, &MapWidget::zoomOut);
    
    connect(button_up, &QPushButton::clicked, this->map, &MapWidget::panUp);
    connect(button_down, &QPushButton::clicked, this->map, &MapWidget::panDown);
    connect(button_left, &QPushButton::clicked, this->map, &MapWidget::panLeft);
    connect(button_right, &QPushButton::clicked, this->map, &MapWidget::panRight);
    
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
    
    this->grid->addWidget(button_zoom_out, 0, 0);
    this->grid->addWidget(button_up, 0, 1);
    this->grid->addWidget(button_zoom_in, 0, 2);
    this->grid->addWidget(button_left, 1, 0);
    this->grid->addWidget(button_down, 1, 1);
    this->grid->addWidget(button_right, 1, 2);
    
    this->grid->addWidget(map_arcgissat, 2, 0, 1, 3);
    this->grid->addWidget(map_opentopomap, 3, 0, 1, 3);
    this->grid->addWidget(map_openstreetmap, 4, 0, 1, 3);
    this->grid->addWidget(map_osmcyclo, 5, 0, 1, 3);
    this->grid->addWidget(label_slider_map_visibility, 6, 0, 1, 3);
    this->grid->addWidget(slider_map_visibility, 7, 0, 1, 3);
    
    // setting it to 50 on init
    if (this->mode == CanvasMode::Edit)
    {
        slider_map_visibility->setValue(50);
        QTimer::singleShot(0, this, [this, slider_map_visibility]()
        {
            emit signalSlideOpacityChanged(slider_map_visibility->value());
        });
    }
}

