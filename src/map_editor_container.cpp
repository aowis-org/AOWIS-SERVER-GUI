#include "map_editor_container.h"

MapEditorContainer::MapEditorContainer(MapModel *map_model, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) )
{
    this->map_model = map_model;
    this->map = new MapWidget(this->map_model, this);
    
    this->map_menu = new MapMenuWidget(this->map, this);
    
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





MapMenuWidget::MapMenuWidget(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) )
{
    this->map = map;
    setLayout(this->layout);
    
    setMinimumWidth(160);
    setMaximumWidth(180);
    
    this->map_nav = new MapNavigationWidget(this->map);
    
    this->layout->addWidget(this->map_nav);
    
    this->layout->addStretch();
}
