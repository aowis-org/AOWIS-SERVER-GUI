#include "tab_map_editor_container.h"

#include <QMessageBox>

MapEditorContainer::MapEditorContainer(MapModel *map_model, MapTileRepository *tile_repository, HydraulicData *hydraulic_data, GpsProvider *gps, EntityInspectorDock *map_inspector, QWidget *parent)
    : QWidget{parent},
    hydraulic_data( hydraulic_data ),
    gps( gps ),
    map_inspector( map_inspector ),
    map_model( map_model ),
    tile_repository( tile_repository ),
    map( new MapWidget(this->map_model, this->tile_repository, this->gps, this) ),
    map_canvas( new MapCanvasWidget(this->map_model, this->map, this->hydraulic_data, this) ),
    map_menu( new MapEditorMenuWidget(this->map, this->map_canvas, CanvasMode::Edit, this) ),
    layout( new QHBoxLayout(this) ),
    map_stack( new QWidget(this) ),
    map_stack_layout( new QStackedLayout(this->map_stack) )
{
    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    scroll_controls->setMinimumWidth(Sizes::SidebarMapEditLeftWidth);
    scroll_controls->setMaximumWidth(Sizes::SidebarMapEditLeftWidth);
    scroll_controls->setWidgetResizable(true);
    scroll_controls->setFrameShape(QFrame::NoFrame);
    scroll_controls->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_controls->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    scroll_controls->setWidget(this->map_menu);
    
    this->map_stack_layout->setContentsMargins(0, 0, 0, 0);
    this->map_stack_layout->setSpacing(0);
    this->map_stack_layout->setStackingMode(QStackedLayout::StackAll);
    
    this->map_stack_layout->addWidget(this->map);
    this->map_stack_layout->addWidget(this->map_canvas);
    
    this->map_canvas->raise();
    
    this->layout->addWidget(scroll_controls);
    this->layout->addWidget(this->map_stack);
    
    connect(this->map_menu, &MapEditorMenuWidget::signalSlideOpacityChanged, this->map_canvas, &MapCanvasWidget::setBackgroundOpacity);
    connect(this->map_menu, &MapEditorMenuWidget::signalMapEditorGuideVisibilityChanged, this, &MapEditorContainer::signalMapEditorGuideVisibilityChanged);
    connect(this->map_menu, &MapEditorMenuWidget::signalEditNetworkSectionActive, this, &MapEditorContainer::signalEditNetworkSectionActive);
    
    this->map_canvas->setFocusPolicy(Qt::StrongFocus);
    QTimer::singleShot(0, this->map_canvas, [this]()
    {
        this->map_canvas->setFocus(Qt::OtherFocusReason);
    });
}
MapWidget *MapEditorContainer::getMap()
{
    return this->map;
}
MapNavigationWidget *MapEditorContainer::mapNavigationWidget()
{
    return this->map_menu->mapNavigationWidget();
}

bool MapEditorContainer::isEditNetworkSectionActive() const
{
    return this->map_menu->isEditNetworkSectionActive();
}

void MapEditorContainer::setMapEditorGuideChecked(bool checked)
{
    this->map_menu->setMapEditorGuideChecked(checked);
}

MapEditorMenuWidget::MapEditorMenuWidget(MapWidget *map, MapCanvasWidget *map_canvas, CanvasMode mode, QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) ),
    mode( mode ),
    map( map ),
    map_nav( new MapNavigationWidget(this->map, this->mode, this) ),
    map_canvas( map_canvas ),
    toolbox( new QToolBox(this) )
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    
    this->toolbox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    createToolboxEdit(this->toolbox);
    createToolboxCache(this->toolbox);
    
    this->layout->addWidget(this->map_nav);
    this->layout->addWidget(this->toolbox);
    this->layout->addStretch();
    
    connect(this->toolbox, &QToolBox::currentChanged, this, &MapEditorMenuWidget::setToolboxMode);
    connect(this->map_nav, &MapNavigationWidget::signalSlideOpacityChanged, this, &MapEditorMenuWidget::signalSlideOpacityChanged);
    
    setToolboxMode(this->toolbox->currentIndex());
}

void MapEditorMenuWidget::updateToolboxHeight(int index)
{
    QTimer::singleShot(0, this, [this, index]
    {
        if (index != this->toolbox->currentIndex())
            return;

        QWidget *page = this->toolbox->widget(index);
        if (page == nullptr)
            return;

        const int page_width = qMax(1, this->toolbox->contentsRect().width());
        int page_height = page->sizeHint().height();
        if (page->layout() != nullptr)
        {
            page->layout()->invalidate();
            page->layout()->activate();
            if (page->layout()->hasHeightForWidth())
                page_height = page->layout()->totalHeightForWidth(page_width);
        }
        else if (page->hasHeightForWidth())
        {
            page_height = page->heightForWidth(page_width);
        }

        int tab_height = 0;
        const QList<QAbstractButton *> tab_buttons = this->toolbox->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly);
        for (QAbstractButton *button : tab_buttons)
            tab_height += button->sizeHint().height();

        if (tab_height == 0)
            tab_height = qMax(0, this->toolbox->height() - page->height());

        const QMargins margins = this->toolbox->contentsMargins();
        this->toolbox->setFixedHeight(margins.top() + tab_height + page_height + margins.bottom() + 16);
        this->toolbox->updateGeometry();
        updateGeometry();
    });
}

MapNavigationWidget *MapEditorMenuWidget::mapNavigationWidget()
{
    return this->map_nav;
}

bool MapEditorMenuWidget::isEditNetworkSectionActive() const
{
    return this->toolbox->currentIndex() == this->toolbox_edit_index;
}

void MapEditorMenuWidget::setMapEditorGuideChecked(bool checked)
{
    this->checkbox_map_editor_guide->setChecked(checked);
}

void MapEditorMenuWidget::createToolboxCache(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QGridLayout *grid = new QGridLayout(wgt);
    
    QLabel *label_explanation_rectangle = new QLabel("<b>Select a region first:</b> right-click and drag on the map.", this);
    label_explanation_rectangle->setWordWrap(true);
    
    QLabel *label_explanation_spinners = new QLabel("<b>Select the zoom level range</b> (from - to) for the tiles you want to delete:");
    label_explanation_spinners->setWordWrap(true);
    QLabel *label_explanation_actions = new QLabel("Then delete the cached tiles for the selected area and zoom levels:", this);
    label_explanation_actions->setWordWrap(true);
    
    this->spin_zoom_from = new QSpinBox();
    this->spin_zoom_from->setRange(1, 19);
    
    this->spin_zoom_to = new QSpinBox();
    this->spin_zoom_to->setRange(2, 19);
    this->spin_zoom_to->setValue(19);
    connect(this->spin_zoom_from, &QSpinBox::valueChanged, this, [this]
    {
        int zoom_from = this->spin_zoom_from->value();
        this->spin_zoom_to->setRange(zoom_from, 19);
    });
    connect(this->map, &MapWidget::signalZoomChanged, this->spin_zoom_from, &QSpinBox::setValue);
    
    this->button_tiles_delete = new QToolButton(wgt);
    this->button_tiles_delete->setText("Delete Tiles");
    this->button_tiles_delete->setCheckable(true);
    this->button_tiles_delete->setEnabled(false);
    
    connect(this->map_canvas, &MapCanvasWidget::signalRectangleSelectionCanceled, this, [this]
    {
        if (this->toolbox->currentIndex() != this->toolbox_cache_index)
            return;

        this->button_tiles_delete->setChecked(false);
        this->button_tiles_delete->setEnabled(false);
    });
    connect(this->map_canvas, &MapCanvasWidget::signalRectangleSelected, this, [this]
    {
        if (this->toolbox->currentIndex() != this->toolbox_cache_index)
            return;

        this->button_tiles_delete->setEnabled(true);
    });
    connect(this->button_tiles_delete, &QToolButton::clicked, this, [this]
    {
        this->button_tiles_delete->setChecked(false);

        QMessageBox *box = new QMessageBox(this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setWindowTitle("Delete selected map tiles?");
        box->setIcon(QMessageBox::Warning);
        box->setText(QString("This will permanently delete all cached map tiles in the selected area for zoom levels %1 through %2.")
            .arg(this->spin_zoom_from->value())
            .arg(this->spin_zoom_to->value()));
        box->setInformativeText("Only continue when you have an internet connection. Deleted tiles may need to be downloaded again; without internet access, the affected map area can remain blank.");

        QPushButton *delete_button = box->addButton("Delete Tiles", QMessageBox::DestructiveRole);
        QPushButton *cancel_button = box->addButton(QMessageBox::Cancel);
        box->setDefaultButton(cancel_button);
        box->setEscapeButton(cancel_button);

        connect(delete_button, &QPushButton::clicked, this, [this]
        {
            for (int zoom = this->spin_zoom_from->value(); zoom <= this->spin_zoom_to->value(); ++zoom)
            {
                const MapCanvasWidget::TileSelectionRange range = this->map_canvas->tileSelectionRange(zoom);
                if (!range.valid)
                    continue;

                this->map->deleteCachedTiles(
                    zoom,
                    range.tile_x_min,
                    range.tile_x_max,
                    range.tile_y_min,
                    range.tile_y_max);
            }

            this->map->repaint();
            this->map_canvas->clearTileSelectionOverlay();
            this->button_tiles_delete->setEnabled(false);
        });
        box->open();
    });
    
    grid->addWidget(label_explanation_rectangle, 0, 0, 1, 2);
    grid->addWidget(label_explanation_spinners, 1, 0, 1, 2);
    grid->addWidget(this->spin_zoom_from, 2, 0);
    grid->addWidget(this->spin_zoom_to, 2, 1);
    grid->addWidget(label_explanation_actions, 3, 0, 1, 2);
    grid->addWidget(this->button_tiles_delete, 4, 0, 1, 2);

    this->toolbox_cache_index = tbx->addItem(wgt, "Tile Cache");
}
void MapEditorMenuWidget::createToolboxEdit(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QVBoxLayout *lay = new QVBoxLayout(wgt);
    
    this->button_group_tools = new QButtonGroup(this);
    
    this->button_radio_select = new QRadioButton("[Esc] Select", wgt);
    this->button_radio_select->setToolTip("Cancel placement and return to selection mode");
    this->button_radio_select->setShortcut(Qt::Key_Escape);
    lay->addWidget(this->button_radio_select);
    this->button_group_tools->addButton(this->button_radio_select, 100);
    connect(this->button_radio_select, &QRadioButton::clicked, this, [this]
    {
        if (this->toolbox->currentIndex() != this->toolbox_edit_index)
            return;

        this->map_canvas->startRectangleSelection(false, true);
    });
    
    QToolButton *button_delete = new QToolButton(wgt);
    button_delete->setText("[Del] Delete Selected");
    button_delete->setShortcut(Qt::Key_Delete);
    button_delete->setEnabled(false);
    lay->addWidget(button_delete);
    MapCanvasEntities *entities = this->map_canvas->mapCanvasEntities();
    connect(entities, &MapCanvasEntities::signalEntityMarkerSelected, this,
    [this, button_delete](bool status)
    {
        if (status)
            button_delete->setEnabled(true);
        else
            button_delete->setEnabled(false);
    });
    connect(button_delete, &QPushButton::clicked, this, [this, entities]
    {
        entities->onMarkerSelectedDeleteRequested();
    });
    
    QLabel *label_add = new QLabel("Add:", this);
    lay->addWidget(label_add);
    
    QRadioButton *button_radio_pipe = new QRadioButton("[1] Pipe / Cable", wgt);
    button_radio_pipe->setShortcut(Qt::Key_1);
    lay->addWidget(button_radio_pipe);
    this->button_group_tools->addButton(button_radio_pipe, 1);
    connect(button_radio_pipe, &QRadioButton::clicked, this, [this]
    {
        this->map_canvas->startEntityPositioning(InfrastructureEntity::Pipe);
    });
    
    QRadioButton *button_radio_junction = new QRadioButton("[2] Junction", wgt);
    button_radio_junction->setShortcut(Qt::Key_2);
    lay->addWidget(button_radio_junction);
    this->button_group_tools->addButton(button_radio_junction, 2);
    connect(button_radio_junction, &QRadioButton::clicked, this, [this]
    {
        this->map_canvas->startEntityPositioning(InfrastructureEntity::Junction);
    });
    
    QRadioButton *button_radio_valve = new QRadioButton("[3] Valve / Switch", wgt);
    button_radio_valve->setShortcut(Qt::Key_3);
    lay->addWidget(button_radio_valve);
    this->button_group_tools->addButton(button_radio_valve, 3);
    connect(button_radio_valve, &QRadioButton::clicked, this, [this] {
        this->map_canvas->startEntityPositioning(InfrastructureEntity::Valve);
    });
    
    QRadioButton *button_radio_customer = new QRadioButton("[4] Customer Point", wgt);
    button_radio_customer->setShortcut(Qt::Key_4);
    lay->addWidget(button_radio_customer);
    this->button_group_tools->addButton(button_radio_customer, 4);
    button_radio_customer->setEnabled(false);
    
    QRadioButton *button_radio_pump = new QRadioButton("[5] Pump", wgt);
    button_radio_pump->setShortcut(Qt::Key_5);
    lay->addWidget(button_radio_pump);
    this->button_group_tools->addButton(button_radio_pump, 5);
    connect(button_radio_pump, &QRadioButton::clicked, this, [this] {
        this->map_canvas->startEntityPositioning(InfrastructureEntity::Pump);
    });
    
    QRadioButton *button_radio_tank = new QRadioButton("[6] Tank", wgt);
    button_radio_tank->setShortcut(Qt::Key_6);
    lay->addWidget(button_radio_tank);
    this->button_group_tools->addButton(button_radio_tank, 6);
    connect(button_radio_tank, &QRadioButton::clicked, this, [this]
    {
        this->map_canvas->startEntityPositioning(InfrastructureEntity::Tank);
    });
    
    QRadioButton *button_radio_power = new QRadioButton("[7] Power Source", wgt);
    button_radio_power->setShortcut(Qt::Key_7);
    lay->addWidget(button_radio_power);
    this->button_group_tools->addButton(button_radio_power, 7);
    button_radio_power->setEnabled(false);
    
    QRadioButton *button_radio_reservoir = new QRadioButton("[8] Reservoir", wgt);
    button_radio_reservoir->setShortcut(Qt::Key_8);
    lay->addWidget(button_radio_reservoir);
    this->button_group_tools->addButton(button_radio_reservoir, 8);
    connect(button_radio_reservoir, &QRadioButton::clicked, this, [this]
    {
        this->map_canvas->startEntityPositioning(InfrastructureEntity::Reservoir);
    });
    
    QRadioButton *button_radio_note = new QRadioButton("[9] Note", wgt);
    button_radio_note->setShortcut(Qt::Key_9);
    lay->addWidget(button_radio_note);
    this->button_group_tools->addButton(button_radio_note, 9);
    button_radio_note->setEnabled(false);
    
    
    
    this->checkbox_map_editor_guide = new QCheckBox("Map Editor Guide", wgt);
    this->checkbox_map_editor_guide->setChecked(true);
    this->checkbox_map_editor_guide->setToolTip("Show or hide the Map Editor Guide");
    lay->addWidget(this->checkbox_map_editor_guide);
    connect(this->checkbox_map_editor_guide, &QCheckBox::toggled, this, &MapEditorMenuWidget::signalMapEditorGuideVisibilityChanged);
    
    connect(this->button_group_tools, &QButtonGroup::idToggled, this, [this]
    {
        this->map_canvas->stopEntityPositioning();
    });
    
    this->toolbox_edit_index = tbx->addItem(wgt, "Edit Network");
}

void MapEditorMenuWidget::setToolboxMode(int index)
{
    emit signalEditNetworkSectionActive(index == this->toolbox_edit_index);

    this->map_canvas->stopEntityPositioning();
    this->map_canvas->clearTileSelectionOverlay();
    this->button_tiles_delete->setChecked(false);
    this->button_tiles_delete->setEnabled(false);

    if (index == this->toolbox_cache_index)
    {
        this->map_canvas->startRectangleSelection(false, false);
    }
    else if (index == this->toolbox_edit_index)
    {
        this->button_radio_select->setChecked(true);
        this->map_canvas->startRectangleSelection(false, true);
    }

    updateToolboxHeight(index);
}


