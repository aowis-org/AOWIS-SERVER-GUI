#include "map_navigation_widget.h"

#include "../gui_configuration.h"

MapNavigationWidget::MapNavigationWidget(MapWidget *map, CanvasMode mode, QWidget *keyboard_focus_target, QWidget *parent)
    : QWidget{parent},
    mode( mode ),
    grid( new QGridLayout(this) ),
    map( map ),
    keyboard_focus_target( keyboard_focus_target != nullptr ? keyboard_focus_target : map )
{
    const GuiShortcutConfiguration &shortcuts = guiConfiguration().shortcuts;

    this->button_zoom_in = new QPushButton();
    this->button_zoom_in->setIcon(QIcon(":/icon/zoom_in.png"));
    this->button_zoom_in->setIconSize(QSize(35, 35));
    this->button_zoom_in->setToolTip(QStringLiteral("Shortcut: [%1]").arg(shortcuts.map_zoom_in));
    
    this->button_zoom_out = new QPushButton();
    this->button_zoom_out->setIcon(QIcon(":/icon/zoom_out.png"));
    this->button_zoom_out->setIconSize(QSize(35, 35));
    this->button_zoom_out->setToolTip(QStringLiteral("Shortcut: [%1]").arg(shortcuts.map_zoom_out));
    
    this->button_up = new QPushButton();
    this->button_up->setIcon(QIcon(":/icon/arrow_up"));
    this->button_up->setIconSize(QSize(25, 25));
    this->button_up->setToolTip(QStringLiteral("Shortcut: [%1]").arg(shortcuts.map_pan_up));
    
    this->button_down = new QPushButton();
    this->button_down->setIcon(QIcon(":/icon/arrow_down"));
    this->button_down->setIconSize(QSize(25, 25));
    this->button_down->setToolTip(QStringLiteral("Shortcut: [%1]").arg(shortcuts.map_pan_down));
    
    this->button_left = new QPushButton();
    this->button_left->setIcon(QIcon(":/icon/arrow_left"));
    this->button_left->setIconSize(QSize(25, 25));
    this->button_left->setToolTip(QStringLiteral("Shortcut: [%1]").arg(shortcuts.map_pan_left));
    
    this->button_right = new QPushButton();
    this->button_right->setIcon(QIcon(":/icon/arrow_right"));
    this->button_right->setIconSize(QSize(25, 25));
    this->button_right->setToolTip(QStringLiteral("Shortcut: [%1]").arg(shortcuts.map_pan_right));
    
    connect(button_zoom_in, &QPushButton::clicked, this->map, &MapWidget::zoomIn);
    connect(button_zoom_out, &QPushButton::clicked, this->map, &MapWidget::zoomOut);
    
    connect(button_up, &QPushButton::clicked, this->map, &MapWidget::panUp);
    connect(button_down, &QPushButton::clicked, this->map, &MapWidget::panDown);
    connect(button_left, &QPushButton::clicked, this->map, &MapWidget::panLeft);
    connect(button_right, &QPushButton::clicked, this->map, &MapWidget::panRight);
    
    this->map_arcgissat = new QRadioButton(QStringLiteral("[%1] ArcGIS SAT").arg(shortcuts.map_provider_arcgis_sat));
    this->map_arcgissat->setShortcut(guiShortcutKeySequence(shortcuts.map_provider_arcgis_sat));
    
    this->map_openstreetmap = new QRadioButton(QStringLiteral("[%1] OpenStreetMap").arg(shortcuts.map_provider_openstreetmap));
    this->map_openstreetmap->setShortcut(guiShortcutKeySequence(shortcuts.map_provider_openstreetmap));
    
    this->map_opentopomap = new QRadioButton(QStringLiteral("[%1] OpenTopoMap").arg(shortcuts.map_provider_opentopomap));
    this->map_opentopomap->setShortcut(guiShortcutKeySequence(shortcuts.map_provider_opentopomap));
    
    this->map_osmcyclo = new QRadioButton(QStringLiteral("[%1] CycloOSM").arg(shortcuts.map_provider_cycloosm));
    this->map_osmcyclo->setShortcut(guiShortcutKeySequence(shortcuts.map_provider_cycloosm));
    
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
    
    const MapViewMode initial_view_mode = this->map->model()->viewMode();
    connect(this->map->model(), &MapModel::viewModeChanged, this, [this](MapViewMode view_mode)
    {
        syncIconSizeSliderForViewMode(view_mode);
    });

    QLabel *label_slider_map_visibility = new QLabel("Opacity");
    QSlider *slider_map_visibility = new QSlider(Qt::Horizontal);
    slider_map_visibility->setRange(0, 100);
    connect(slider_map_visibility, &QSlider::valueChanged, this, &MapNavigationWidget::signalSlideOpacityChanged);

    QLabel *label_slider_icon_size = new QLabel("Icon Size [%]");
    this->slider_icon_size = new QSlider(Qt::Horizontal);
    this->slider_icon_size->setToolTip("Scales entity icons and 3D entity models.");
    syncIconSizeSliderForViewMode(initial_view_mode);
    connect(this->slider_icon_size, &QSlider::valueChanged, this, [this](int size_percent)
    {
        if (this->map->model()->viewMode() == MapViewMode::ThreeD)
            this->icon_size_3d_percent = size_percent;
        else
            this->icon_size_2d_percent = size_percent;

        emit signalIconSizeChanged(size_percent);
    });
    
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
    this->grid->addWidget(label_slider_map_visibility, 6, 0, 1, 3);
    this->grid->addWidget(slider_map_visibility, 7, 0, 1, 3);
    this->grid->addWidget(label_slider_icon_size, 8, 0, 1, 3);
    this->grid->addWidget(this->slider_icon_size, 9, 0, 1, 3);
    this->grid->addWidget(check_map_sync, 10, 0, 1, 3);
    
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

void MapNavigationWidget::syncIconSizeSliderForViewMode(MapViewMode view_mode)
{
    if (this->slider_icon_size == nullptr)
        return;

    const bool is_3d = view_mode == MapViewMode::ThreeD;
    const int maximum = is_3d ? 200 : 250;
    const int remembered_value = is_3d
        ? this->icon_size_3d_percent
        : this->icon_size_2d_percent;
    const int bounded_value = qBound(50, remembered_value, maximum);

    {
        const QSignalBlocker blocker(this->slider_icon_size);
        this->slider_icon_size->setRange(50, maximum);
        this->slider_icon_size->setValue(bounded_value);
    }

    if (is_3d)
        this->icon_size_3d_percent = bounded_value;
    else
        this->icon_size_2d_percent = bounded_value;

    emit signalIconSizeChanged(bounded_value);
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
