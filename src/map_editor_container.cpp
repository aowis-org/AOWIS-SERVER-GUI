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
    map( map )
{
    setMinimumWidth(160);
    setMaximumWidth(180);
    
    this->map_nav = new MapNavigationWidget(this->map, this);
    this->layout->addWidget(this->map_nav);
    
    this->toolbox_cache = createToolboxCache();
    this->layout->addWidget(this->toolbox_cache);
    
    this->layout->addStretch();
}

QToolBox *MapEditorMenuWidget::createToolboxCache()
{
    QToolBox *tbx = new QToolBox(this);
    QWidget *wgt = new QWidget(tbx);
    QVBoxLayout *lay = new QVBoxLayout(wgt);
    
    QToolButton *btn_tiles_delete = new QToolButton(wgt);
    btn_tiles_delete->setText("Delete Tiles");
    btn_tiles_delete->setCheckable(true);
    btn_tiles_delete->setToolButtonStyle(Qt::ToolButtonTextOnly);
    
    lay->addWidget(btn_tiles_delete);
    
    tbx->addItem(wgt, "Cache");
    
    return tbx;
}
