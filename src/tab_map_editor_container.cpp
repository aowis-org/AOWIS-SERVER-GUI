#include "tab_map_editor_container.h"

MapEditorContainer::MapEditorContainer(MapModel *map_model, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    map_model( map_model ),
    map( new MapWidget(this->map_model, this) ),
    map_canvas( new MapNetworkCanvasWidget(this->map_model, this->map, CanvasMode::Edit, this) ),
    map_menu( new MapEditorMenuWidget(this->map, this->map_canvas, CanvasMode::Edit, this) ),
    map_stack( new QWidget(this) ),
    map_stack_layout( new QStackedLayout(this->map_stack) )
{
    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    scroll_controls->setMinimumWidth(Sizes::SidebarLeftWidthBase);
    scroll_controls->setMaximumWidth(Sizes::SidebarLeftWidthBase);
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
    setMinimumWidth(Sizes::SidebarLeftWidthBase);
    setMaximumWidth(Sizes::SidebarLeftWidthBase);
    
    this->toolbox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    createToolboxCache(this->toolbox);
    createToolboxEdit(this->toolbox);
    
    this->layout->addWidget(this->map_nav);
    this->layout->addWidget(this->toolbox);
    this->layout->addStretch();
    
    this->toolbox->setCurrentIndex(1);
    
    connect(this->map_nav, &MapNavigationWidget::signalSlideOpacityChanged, this, &MapEditorMenuWidget::signalSlideOpacityChanged);
}

void MapEditorMenuWidget::createToolboxCache(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QVBoxLayout *lay = new QVBoxLayout(wgt);
    
    QLabel *label_explanation_rectangle = new QLabel("<b>Select Area</b> on<br>the map first:", this);
    
    QToolButton *btn_select_rectangle = new QToolButton(wgt);
    btn_select_rectangle->setText("Select Area");
    btn_select_rectangle->setCheckable(true);
    btn_select_rectangle->setAutoRaise(false);
    connect(btn_select_rectangle, &QToolButton::clicked, this, [this, btn_select_rectangle]
    {
        this->map_canvas->startRectangleSelection();
    });
    
    QLabel *label_explanation_actions = new QLabel("Than for the area you<br>have selected, you can<br>choose one of the<br>following actions:", this);
    
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
    
    lay->addWidget(label_explanation_rectangle);
    lay->addWidget(btn_select_rectangle);
    lay->addWidget(label_explanation_actions);
    lay->addWidget(btn_tiles_delete);
    lay->addWidget(btn_tiles_update);
    
    tbx->addItem(wgt, "Tile Cache");
}
void MapEditorMenuWidget::createToolboxEdit(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QVBoxLayout *lay = new QVBoxLayout(wgt);
    
    QStringList labels = {
        "Select", "Delete Selected", "Add Note", "Add Reservoir", "Add Tank", "Add Pump",
        "Add Valve", "Add Junction", "Add Pipe", "Add Customer Point"
    };
    for (int i=0; i < labels.length(); i++)
    {
        if (i == 1)
        {
            QToolButton *btn = new QToolButton(wgt);
            btn->setText(labels[i]);
            lay->addWidget(btn);
        }
        else
        {
            QRadioButton *btn = new QRadioButton(labels[i], wgt);
            lay->addWidget(btn);
        }
    }
    
    tbx->addItem(wgt, "Edit Network");
}

