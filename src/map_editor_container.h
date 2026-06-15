#ifndef MAP_EDITOR_CONTAINER_H
#define MAP_EDITOR_CONTAINER_H

#include <QObject>
#include <QWidget>

#include <QHBoxLayout>
#include <QScrollArea>

#include "map_model.h"
#include "map_widget.h"
#include "map_navigation_widget.h"

class MapEditorContainer : public QWidget
{
    Q_OBJECT
public:
    explicit MapEditorContainer(MapModel *map_model, QWidget *parent = nullptr);
    
    MapWidget *getMap();
    
private:
    MapModel *map_model;
    MapWidget *map;
    QHBoxLayout *layout;
    
    MapNavigationWidget *map_nav;
    
signals:
};

#endif // MAP_EDITOR_CONTAINER_H
