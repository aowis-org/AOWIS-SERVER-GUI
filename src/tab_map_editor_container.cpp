#include "tab_map_editor_container.h"

MapEditorContainer::MapEditorContainer(MapModel *map_model, GpsProvider *gps, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    gps( gps ),
    map_model( map_model ),
    map( new MapWidget(this->map_model, gps, this) ),
    map_canvas( new MapNetworkCanvasWidget(this->map_model, this->map, CanvasMode::Edit, this) ),
    map_menu( new MapEditorMenuWidget(this->map, this->map_canvas, CanvasMode::Edit, this) ),
    map_stack( new QWidget(this) ),
    map_stack_layout( new QStackedLayout(this->map_stack) )
{
    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    
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
    
    connect(this->map_menu, &MapEditorMenuWidget::signalSlideOpacityChanged, this->map_canvas, &MapNetworkCanvasWidget::setBackgroundOpacity);
    
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





MapEditorMenuWidget::MapEditorMenuWidget(MapWidget *map, MapNetworkCanvasWidget *map_canvas, CanvasMode mode, QWidget *parent)
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
    
    createToolboxCache(this->toolbox);
    createToolboxEdit(this->toolbox);
    
    this->layout->addWidget(this->map_nav);
    this->layout->addWidget(this->toolbox);
    this->layout->addStretch();
    
    this->toolbox->setCurrentIndex(1);
    
    connect(this->map_nav, &MapNavigationWidget::signalSlideOpacityChanged, this, &MapEditorMenuWidget::signalSlideOpacityChanged);
    
    connect(this->map_canvas, &MapNetworkCanvasWidget::signalMapProviderChange, this->map_nav, &MapNavigationWidget::mapProviderChange);
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
    
    connect(this->map_canvas, &MapNetworkCanvasWidget::rectangleSelectionCanceled, this, [this, btn_select_rectangle, btn_tiles_delete, btn_tiles_update]
    {
        btn_select_rectangle->setChecked(false);
        btn_tiles_delete->setEnabled(false);
        btn_tiles_update->setEnabled(false);
    });
    connect(this->map_canvas, &MapNetworkCanvasWidget::rectangleSelected, this, [this, btn_select_rectangle, btn_tiles_delete, btn_tiles_update]
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
    
    QRadioButton *button_radio_select = new QRadioButton("Select", wgt);
    button_radio_select->setToolTip("Shortcut: [1]");
    button_radio_select->setShortcut(QKeySequence(Qt::Key_1));
    lay->addWidget(button_radio_select);
    this->button_group_tools->addButton(button_radio_select, 1);
    
    QToolButton *button_delete = new QToolButton(wgt);
    button_delete->setText("Delete Selected");
    button_delete->setToolTip("Shortcut: [del]");
    button_delete->setShortcut(QKeySequence(Qt::Key_Delete));
    button_delete->setEnabled(false);
    lay->addWidget(button_delete);
    
    QRadioButton *button_radio_reservoir = new QRadioButton("Add Reservoir", wgt);
    button_radio_reservoir->setToolTip("Shortcut: [2]");
    button_radio_reservoir->setShortcut(QKeySequence(Qt::Key_2));
    lay->addWidget(button_radio_reservoir);
    this->button_group_tools->addButton(button_radio_reservoir, 2);
    
    QRadioButton *button_radio_tank = new QRadioButton("Add Tank", wgt);
    button_radio_tank->setToolTip("Shortcut: [3]");
    button_radio_tank->setShortcut(QKeySequence(Qt::Key_3));
    lay->addWidget(button_radio_tank);
    this->button_group_tools->addButton(button_radio_tank, 3);
    connect(button_radio_tank, &QRadioButton::clicked, this, [this]
    {
        this->map_canvas->startEntityPositioning(MapEditTool::Tank);
    });
    
    QRadioButton *button_radio_pump = new QRadioButton("Add Pump", wgt);
    button_radio_pump->setToolTip("Shortcut: [4]");
    button_radio_pump->setShortcut(QKeySequence(Qt::Key_4));
    lay->addWidget(button_radio_pump);
    this->button_group_tools->addButton(button_radio_pump, 4);
    
    QRadioButton *button_radio_valve = new QRadioButton("Add Valve", wgt);
    button_radio_valve->setToolTip("Shortcut: [5]");
    button_radio_valve->setShortcut(QKeySequence(Qt::Key_5));
    lay->addWidget(button_radio_valve);
    this->button_group_tools->addButton(button_radio_valve, 5);
    
    QRadioButton *button_radio_junction = new QRadioButton("Add Junction", wgt);
    button_radio_junction->setToolTip("Shortcut: [6]");
    button_radio_junction->setShortcut(QKeySequence(Qt::Key_6));
    lay->addWidget(button_radio_junction);
    this->button_group_tools->addButton(button_radio_junction, 6);
    
    QRadioButton *button_radio_pipe = new QRadioButton("Add Pipe", wgt);
    button_radio_pipe->setToolTip("Shortcut: [7]");
    button_radio_pipe->setShortcut(QKeySequence(Qt::Key_7));
    lay->addWidget(button_radio_pipe);
    this->button_group_tools->addButton(button_radio_pipe, 7);
    
    QRadioButton *button_radio_customer = new QRadioButton("Add Customer Point", wgt);
    button_radio_customer->setToolTip("Shortcut: [8]");
    button_radio_customer->setShortcut(QKeySequence(Qt::Key_8));
    lay->addWidget(button_radio_customer);
    this->button_group_tools->addButton(button_radio_customer, 8);
    
    QRadioButton *button_radio_power = new QRadioButton("Add Power Source", wgt);
    button_radio_power->setToolTip("Shortcut: [9]");
    button_radio_power->setShortcut(QKeySequence(Qt::Key_9));
    lay->addWidget(button_radio_power);
    this->button_group_tools->addButton(button_radio_power, 9);
    
    QRadioButton *button_radio_note = new QRadioButton("Add Note", wgt);
    button_radio_note->setToolTip("Shortcut: [0]");
    button_radio_note->setShortcut(QKeySequence(Qt::Key_0));
    lay->addWidget(button_radio_note);
    this->button_group_tools->addButton(button_radio_note, 0);
    
    tbx->addItem(wgt, "Edit Network");
    
    // not needed if we stick with setToolTip istead
    connect(this->map_canvas, &MapNetworkCanvasWidget::signalEditToolChange, this, [this](MapEditTool tool)
    {
        QAbstractButton *abs = this->button_group_tools->button(tool);
        abs->click();
    });
    
}

