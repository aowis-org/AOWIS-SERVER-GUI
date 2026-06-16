#include "map_editor_container.h"

MapEditorContainer::MapEditorContainer(MapModel *map_model, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) )
{
    this->map_model = map_model;
    this->map = new MapWidget(this->map_model, this);
    
    this->map_menu = new MapEditorMenuWidget(this->map, this);
    
    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    scroll_controls->setMinimumWidth(160);
    scroll_controls->setMaximumWidth(180);
    scroll_controls->setWidgetResizable(true);
    scroll_controls->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_controls->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    scroll_controls->setWidget(this->map_menu);
    
    this->layout->addWidget(scroll_controls);
    this->layout->addWidget(this->map);
}
MapWidget *MapEditorContainer::getMap()
{
    return this->map;
}





MapEditorMenuWidget::MapEditorMenuWidget(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) ),
    map( map ),
    map_nav( new MapNavigationWidget(this->map, this) ),
    toolbox( new QToolBox(this) )
{
    setMinimumWidth(160);
    setMaximumWidth(180);
    
    this->toolbox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    createToolboxCache(this->toolbox);
    createToolboxEdit(this->toolbox);
    
    this->layout->addWidget(this->map_nav);
    this->layout->addWidget(this->toolbox);
    this->layout->addStretch();
}

void MapEditorMenuWidget::createToolboxCache(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QVBoxLayout *lay = new QVBoxLayout(wgt);
    
    QLabel *label_explanation = new QLabel("Draw a rectangle on<br>the map first.<br>Than you can choose<br>one of the following<br>actions:", this);
    
    QToolButton *btn_tiles_delete = new QToolButton(wgt);
    btn_tiles_delete->setText("Delete Tiles");
    btn_tiles_delete->setCheckable(true);
    btn_tiles_delete->setAutoRaise(true);
    btn_tiles_delete->setToolButtonStyle(Qt::ToolButtonTextOnly);
    
    QToolButton *btn_tiles_update = new QToolButton(wgt);
    btn_tiles_update->setText("Update");
    btn_tiles_update->setCheckable(true);
    btn_tiles_update->setAutoRaise(true);
    btn_tiles_update->setToolButtonStyle(Qt::ToolButtonTextOnly);
    
    lay->addWidget(label_explanation);
    lay->addWidget(btn_tiles_delete);
    lay->addWidget(btn_tiles_update);
    
    tbx->addItem(wgt, "Tile Cache");
}
void MapEditorMenuWidget::createToolboxEdit(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QVBoxLayout *lay = new QVBoxLayout(wgt);
    
    
    
    tbx->addItem(wgt, "Edit Network");
}

