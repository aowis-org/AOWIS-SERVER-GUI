#include "map_navigation_widget.h"

#ifndef Q_OS_WASM
#include "../gui_configuration.h"
#endif

MapNavigationWidget::MapNavigationWidget(MapWidget *map, CanvasMode mode, QWidget *keyboard_focus_target, QWidget *parent)
    : QWidget{parent},
    mode( mode ),
    grid( new QGridLayout(this) ),
    map( map ),
    keyboard_focus_target( keyboard_focus_target != nullptr ? keyboard_focus_target : map )
{
    this->button_zoom_in = new QPushButton();
    this->button_zoom_in->setIcon(QIcon(":/icon/zoom_in.png"));
    this->button_zoom_in->setIconSize(QSize(35, 35));
    this->button_zoom_in->setToolTip("Shortcut: [E]");
    
    this->button_zoom_out = new QPushButton();
    this->button_zoom_out->setIcon(QIcon(":/icon/zoom_out.png"));
    this->button_zoom_out->setIconSize(QSize(35, 35));
    this->button_zoom_out->setToolTip("Shortcut: [Q]");
    
    this->button_up = new QPushButton();
    this->button_up->setIcon(QIcon(":/icon/arrow_up"));
    this->button_up->setIconSize(QSize(25, 25));
    this->button_up->setToolTip("Shortcut: [W]");
    
    this->button_down = new QPushButton();
    this->button_down->setIcon(QIcon(":/icon/arrow_down"));
    this->button_down->setIconSize(QSize(25, 25));
    this->button_down->setToolTip("Shortcut: [S]");
    
    this->button_left = new QPushButton();
    this->button_left->setIcon(QIcon(":/icon/arrow_left"));
    this->button_left->setIconSize(QSize(25, 25));
    this->button_left->setToolTip("Shortcut: [A]");
    
    this->button_right = new QPushButton();
    this->button_right->setIcon(QIcon(":/icon/arrow_right"));
    this->button_right->setIconSize(QSize(25, 25));
    this->button_right->setToolTip("Shortcut: [D]");
    
    connect(button_zoom_in, &QPushButton::clicked, this->map, &MapWidget::zoomIn);
    connect(button_zoom_out, &QPushButton::clicked, this->map, &MapWidget::zoomOut);
    
    connect(button_up, &QPushButton::clicked, this->map, &MapWidget::panUp);
    connect(button_down, &QPushButton::clicked, this->map, &MapWidget::panDown);
    connect(button_left, &QPushButton::clicked, this->map, &MapWidget::panLeft);
    connect(button_right, &QPushButton::clicked, this->map, &MapWidget::panRight);
    
    this->map_arcgissat = new QRadioButton("[F1] ArcGIS SAT");
    this->map_arcgissat->setShortcut(Qt::Key_F1);
    
    this->map_openstreetmap = new QRadioButton("[F2] OpenStreetMap");
    this->map_openstreetmap->setShortcut(Qt::Key_F2);
    
    this->map_opentopomap = new QRadioButton("[F3] OpenTopoMap");
    this->map_opentopomap->setShortcut(Qt::Key_F3);
    
    this->map_osmcyclo = new QRadioButton("[F4] CycloOSM");
    this->map_osmcyclo->setShortcut(Qt::Key_F4);
    
    map_arcgissat->setChecked(true);
    
    connect(this->map_arcgissat, &QRadioButton::clicked, this, [this]
    {
        this->activateMapProvider(MapProvider::ArcGISSat);
    });
    connect(this->map_openstreetmap, &QRadioButton::clicked, this, [this]
    {
        this->activateMapProvider(MapProvider::OpenStreetMap);
    });
    connect(this->map_opentopomap, &QRadioButton::clicked, this, [this]
    {
        this->activateMapProvider(MapProvider::OpenTopoMap);
    });
    connect(this->map_osmcyclo, &QRadioButton::clicked, this, [this]
    {
        this->activateMapProvider(MapProvider::OSMCyclo);
    });
    
    QLabel *label_map_view_mode = new QLabel("View Mode");
    this->combo_map_view_mode = new QComboBox();
    this->combo_map_view_mode->addItem("2D", int(MapViewMode::TwoD));
    this->combo_map_view_mode->addItem("3D", int(MapViewMode::ThreeD));
    this->combo_map_view_mode->setToolTip("Switch between the top-down 2D map and the 3D map view.");

    const MapViewMode initial_view_mode = this->map->model()->viewMode();
    const int initial_view_index = this->combo_map_view_mode->findData(int(initial_view_mode));
    if (initial_view_index >= 0)
        this->combo_map_view_mode->setCurrentIndex(initial_view_index);

    connect(this->combo_map_view_mode, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        const QVariant view_mode_data = this->combo_map_view_mode->itemData(index);
        if (!view_mode_data.isValid())
            return;

        this->map->model()->setViewMode(static_cast<MapViewMode>(view_mode_data.toInt()));
    });
    connect(this->map->model(), &MapModel::viewModeChanged, this, [this](MapViewMode view_mode)
    {
        const int index = this->combo_map_view_mode->findData(int(view_mode));
        if (index < 0 || index == this->combo_map_view_mode->currentIndex())
            return;

        const QSignalBlocker blocker(this->combo_map_view_mode);
        this->combo_map_view_mode->setCurrentIndex(index);
    });

#ifndef Q_OS_WASM
    const bool view_mode_selector_visible = desktopMapRenderer() == DesktopMapRenderer::Rhi;
#else
    const bool view_mode_selector_visible = false;
#endif
    label_map_view_mode->setVisible(view_mode_selector_visible);
    this->combo_map_view_mode->setVisible(view_mode_selector_visible);

    QLabel *label_slider_map_visibility = new QLabel("Opacity");
    QSlider *slider_map_visibility = new QSlider(Qt::Horizontal);
    slider_map_visibility->setRange(0, 100);
    connect(slider_map_visibility, &QSlider::valueChanged, this, &MapNavigationWidget::signalSlideOpacityChanged);

    QLabel *label_slider_icon_size = new QLabel("Icon Size [%]");
    this->slider_icon_size = new QSlider(Qt::Horizontal);
    this->slider_icon_size->setRange(50, 250);
    this->slider_icon_size->setValue(100);
    this->slider_icon_size->setToolTip("Scales pixmap/SVG icons only.");
    connect(this->slider_icon_size, &QSlider::valueChanged, this, &MapNavigationWidget::signalIconSizeChanged);
    
    this->check_map_sync = new QCheckBox("Sync Map Movement");
    this->check_map_sync->setToolTip("Synchronize Map movement between Editor and Monitor");
    this->check_map_sync->setChecked(true);
    connect(this->check_map_sync, &QCheckBox::checkStateChanged, this, [this]
    {
        emit signalSyncMapMovementStateChanged(this->check_map_sync->isChecked());
    });
    
    this->grid->addWidget(button_zoom_out, 0, 0);
    this->grid->addWidget(button_up, 0, 1, Qt::AlignBottom);
    this->grid->addWidget(button_zoom_in, 0, 2);
    this->grid->addWidget(button_left, 1, 0);
    this->grid->addWidget(button_down, 1, 1);
    this->grid->addWidget(button_right, 1, 2);
    
    this->grid->addWidget(map_arcgissat, 2, 0, 1, 3);
    this->grid->addWidget(map_openstreetmap, 3, 0, 1, 3);
    this->grid->addWidget(map_opentopomap, 4, 0, 1, 3);
    this->grid->addWidget(map_osmcyclo, 5, 0, 1, 3);
    this->grid->addWidget(label_map_view_mode, 6, 0, 1, 3);
    this->grid->addWidget(this->combo_map_view_mode, 7, 0, 1, 3);
    this->grid->addWidget(label_slider_map_visibility, 8, 0, 1, 3);
    this->grid->addWidget(slider_map_visibility, 9, 0, 1, 3);
    this->grid->addWidget(label_slider_icon_size, 10, 0, 1, 3);
    this->grid->addWidget(this->slider_icon_size, 11, 0, 1, 3);
    this->grid->addWidget(check_map_sync, 12, 0, 1, 3);
    
    this->button_group_map_select = new QButtonGroup(this);
    this->button_group_map_select->addButton(this->map_arcgissat, 1);
    this->button_group_map_select->addButton(this->map_openstreetmap, 2);
    this->button_group_map_select->addButton(this->map_opentopomap, 3);
    this->button_group_map_select->addButton(this->map_osmcyclo, 4);
    
    // setting canvas opacity to 50 on init
    if (this->mode == CanvasMode::Edit || this->mode == CanvasMode::Monitor)
    {
        slider_map_visibility->setValue(50);
        QTimer::singleShot(0, this, [this, slider_map_visibility]()
        {
            emit signalSlideOpacityChanged(slider_map_visibility->value());
        });
    }
    
    
}

void MapNavigationWidget::activateMapProvider(MapProvider provider)
{
    this->map->changeMapProvider(provider);

    QWidget *focus_target = this->keyboard_focus_target;
    QTimer::singleShot(0, focus_target, [focus_target]
    {
        if (focus_target->isVisible())
            focus_target->setFocus(Qt::ShortcutFocusReason);
    });
}

void MapNavigationWidget::mapProviderChange(MapProvider provider)
{
    QAbstractButton *button = nullptr;
    
    switch (provider)
    {
    case MapProvider::ArcGISSat:
        button = this->map_arcgissat;
        break;
    case MapProvider::OpenStreetMap:
        button = this->map_openstreetmap;
        break;
    case MapProvider::OpenTopoMap:
        button = this->map_opentopomap;
        break;
    case MapProvider::OSMCyclo:
        button = this->map_osmcyclo;
        break;
    }
    
    if (!button)
        return;
    
    button->setChecked(true);
}

void MapNavigationWidget::mapMovementSyncStateChange(bool sync)
{
    const QSignalBlocker blocker(this->check_map_sync);
    this->check_map_sync->setChecked(sync);
}
