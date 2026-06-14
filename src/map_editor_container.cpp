#include "map_editor_container.h"

MapEditorContainer::MapEditorContainer(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    map_nav( new MapNavigationWidget(map) )
{
    this->map = map;
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    scroll_controls->setMinimumWidth(180);
    scroll_controls->setMaximumWidth(200);
    scroll_controls->setWidgetResizable(true);
    scroll_controls->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_controls->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    this->layout->addWidget(this->map_nav);
    //this->layout->addWidget(this->map);
}


