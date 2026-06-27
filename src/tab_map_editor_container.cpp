#include "tab_map_editor_container.h"

MapEditorContainer::MapEditorContainer(MapModel *map_model, GpsProvider *gps, EntityInspectorDock *map_inspector, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    gps( gps ),
    map_model( map_model ),
    map( new MapWidget(this->map_model, gps, this) ),
    map_canvas( new MapCanvasWidget(this->map_model, this->map, CanvasMode::Edit, this) ),
    map_menu( new MapEditorMenuWidget(this->map, this->map_canvas, CanvasMode::Edit, this) ),
    map_inspector( map_inspector ),
    map_stack( new QWidget(this) ),
    map_stack_layout( new QStackedLayout(this->map_stack) )
{
    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    
    this->installEventFilter(this);
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    scroll_controls->setMinimumWidth(Sizes::SidebarMapEditLeftWidthBase);
    scroll_controls->setMaximumWidth(Sizes::SidebarMapEditLeftWidthBase);
    scroll_controls->setWidgetResizable(true);
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

bool MapEditorContainer::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *key_event = static_cast<QKeyEvent *>(event);
        
        if (map_canvas->onKeyPressEvent(key_event))
        {
            event->accept();
            return true;
        }
    }
    
    return QWidget::eventFilter(obj, event);
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
    setMinimumWidth(Sizes::SidebarMapEditLeftWidthBase);
    setMaximumWidth(Sizes::SidebarMapEditLeftWidthBase);
    
    this->toolbox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    createToolboxEdit(this->toolbox);
    createToolboxCache(this->toolbox);
    
    this->layout->addWidget(this->map_nav);
    this->layout->addWidget(this->toolbox);
    this->layout->addStretch();
    
    //this->toolbox->setCurrentIndex(0);
    
    connect(this->map_nav, &MapNavigationWidget::signalSlideOpacityChanged, this, &MapEditorMenuWidget::signalSlideOpacityChanged);
    
}

void MapEditorMenuWidget::createToolboxCache(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QGridLayout *grid = new QGridLayout(wgt);
    
    QLabel *label_explanation_rectangle = new QLabel("<b>Select Area</b> on the map first:", this);
    label_explanation_rectangle->setWordWrap(true);
    
    QToolButton *btn_select_rectangle = new QToolButton(wgt);
    btn_select_rectangle->setText("Select Area");
    btn_select_rectangle->setCheckable(true);
    btn_select_rectangle->setAutoRaise(false);
    connect(btn_select_rectangle, &QToolButton::clicked, this, [this, btn_select_rectangle]
    {
        this->map_canvas->startRectangleSelection();
    });
    
    QLabel *label_explanation_spinners = new QLabel("<b>Select the zoom level range</b> (from - to) for the tiles you want to delete/reload:");
    label_explanation_spinners->setWordWrap(true);
    QLabel *label_explanation_actions = new QLabel("Than for the selected area and zoom levels, you can <b>choose</b> one of the following <b>actions</b>:", this);
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
    
    QToolButton *btn_tiles_delete = new QToolButton(wgt);
    btn_tiles_delete->setText("Delete Tiles");
    btn_tiles_delete->setCheckable(true);
    btn_tiles_delete->setEnabled(false);
    
    QToolButton *btn_tiles_update = new QToolButton(wgt);
    btn_tiles_update->setText("Update Tiles");
    btn_tiles_update->setCheckable(true);
    btn_tiles_update->setEnabled(false);
    
    connect(this->map_canvas, &MapCanvasWidget::rectangleSelectionCanceled, this, [this, btn_select_rectangle, btn_tiles_delete, btn_tiles_update]
    {
        btn_select_rectangle->setChecked(false);
        btn_tiles_delete->setEnabled(false);
        btn_tiles_update->setEnabled(false);
    });
    connect(this->map_canvas, &MapCanvasWidget::rectangleSelected, this, [this, btn_select_rectangle, btn_tiles_delete, btn_tiles_update]
    {
        btn_select_rectangle->setChecked(false);
        btn_tiles_delete->setEnabled(true);
        btn_tiles_update->setEnabled(true);
    });
    
    grid->addWidget(label_explanation_rectangle, 0, 0, 1, 2);
    grid->addWidget(btn_select_rectangle, 1, 0, 1, 2);
    
    grid->addWidget(label_explanation_spinners, 2, 0, 1, 2);
    
    grid->addWidget(spin_zoom_from, 3, 0);
    grid->addWidget(spin_zoom_to, 3, 1);
    
    grid->addWidget(label_explanation_actions, 4, 0, 1, 2);
    
    grid->addWidget(btn_tiles_delete, 5, 0, 1, 2);
    grid->addWidget(btn_tiles_update, 6, 0, 1, 2);
    
    tbx->addItem(wgt, "Tile Cache");
}
void MapEditorMenuWidget::createToolboxEdit(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QVBoxLayout *lay = new QVBoxLayout(wgt);
    
    this->button_group_tools = new QButtonGroup(this);
    
    QRadioButton *button_radio_select = new QRadioButton("[Esc] Select", wgt);
    button_radio_select->setToolTip("Cancel placement and return to selection mode");
    button_radio_select->setShortcut(Qt::Key_Escape);
    lay->addWidget(button_radio_select);
    this->button_group_tools->addButton(button_radio_select, 100);
    
    QToolButton *button_delete = new QToolButton(wgt);
    button_delete->setText("[Del] Delete Selected");
    button_delete->setEnabled(false);
    lay->addWidget(button_delete);
    
    QLabel *label_add = new QLabel("Add:", this);
    lay->addWidget(label_add);
    
    QRadioButton *button_radio_pipe = new QRadioButton("[1] Pipe / Cable", wgt);
    button_radio_pipe->setShortcut(Qt::Key_1);
    lay->addWidget(button_radio_pipe);
    this->button_group_tools->addButton(button_radio_pipe, 1);
    
    QRadioButton *button_radio_junction = new QRadioButton("[2] Junction", wgt);
    button_radio_junction->setShortcut(Qt::Key_2);
    lay->addWidget(button_radio_junction);
    this->button_group_tools->addButton(button_radio_junction, 2);
    
    QRadioButton *button_radio_valve = new QRadioButton("[3] Valve / Switch", wgt);
    button_radio_valve->setShortcut(Qt::Key_3);
    lay->addWidget(button_radio_valve);
    this->button_group_tools->addButton(button_radio_valve, 3);
    
    QRadioButton *button_radio_customer = new QRadioButton("[4] Customer Point", wgt);
    button_radio_customer->setShortcut(Qt::Key_4);
    lay->addWidget(button_radio_customer);
    this->button_group_tools->addButton(button_radio_customer, 4);
    
    QRadioButton *button_radio_pump = new QRadioButton("[5] Pump", wgt);
    button_radio_pump->setShortcut(Qt::Key_5);
    lay->addWidget(button_radio_pump);
    this->button_group_tools->addButton(button_radio_pump, 5);
    
    QRadioButton *button_radio_tank = new QRadioButton("[6] Tank", wgt);
    button_radio_tank->setShortcut(Qt::Key_6);
    lay->addWidget(button_radio_tank);
    this->button_group_tools->addButton(button_radio_tank, 6);
    connect(button_radio_tank, &QRadioButton::clicked, this, [this]
    {
        this->map_canvas->startEntityPositioning(MapEditTool::Tank);
    });
    
    QRadioButton *button_radio_power = new QRadioButton("[7] Power Source", wgt);
    button_radio_power->setShortcut(Qt::Key_7);
    lay->addWidget(button_radio_power);
    this->button_group_tools->addButton(button_radio_power, 7);
    
    QRadioButton *button_radio_reservoir = new QRadioButton("[8] Reservoir", wgt);
    button_radio_reservoir->setShortcut(Qt::Key_8);
    lay->addWidget(button_radio_reservoir);
    this->button_group_tools->addButton(button_radio_reservoir, 8);
    
    QRadioButton *button_radio_note = new QRadioButton("[9] Note", wgt);
    button_radio_note->setShortcut(Qt::Key_9);
    lay->addWidget(button_radio_note);
    this->button_group_tools->addButton(button_radio_note, 9);
    
    QLabel *label_instruction = new QLabel(
        "<b>Right Click</b> to place Entity",
        this
    );
    label_instruction->setWordWrap(true);
    lay->addWidget(label_instruction);
    
    connect(this->button_group_tools, &QButtonGroup::idToggled, this, [this]
    {
        this->map_canvas->stopEntityPositioning();
    });
    
    tbx->addItem(wgt, "Edit Network");
}


