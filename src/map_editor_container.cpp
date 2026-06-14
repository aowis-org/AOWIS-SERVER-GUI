#include "map_editor_container.h"

MapEditorContainer::MapEditorContainer(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    map_nav( new MapNavigationWidget(map) )
{
    this->map = map;
    
    this->layout->addWidget(this->map_nav);
    //this->layout->addWidget(this->map);
}


