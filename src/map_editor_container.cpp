#include "map_editor_container.h"

MapEditorContainer::MapEditorContainer(MapModel *map_model, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) )
{
    this->map_model = map_model;
    this->map = new MapWidget(this->map_model, this);
    
    this->map_nav = new MapNavigationWidget(this->map);
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    scroll_controls->setMinimumWidth(180);
    scroll_controls->setMaximumWidth(200);
    scroll_controls->setWidgetResizable(true);
    scroll_controls->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_controls->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    this->layout->addWidget(scroll_controls);
    this->layout->addWidget(this->map);
}


