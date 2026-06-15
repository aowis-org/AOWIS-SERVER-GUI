#ifndef MAP_EDITOR_CONTAINER_H
#define MAP_EDITOR_CONTAINER_H

#include <QObject>
#include <QWidget>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>

#include "map_model.h"
#include "map_widget.h"
#include "map_navigation_widget.h"

class MapMenuWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapMenuWidget(MapWidget *map, QWidget *parent = nullptr);
    
private:
    QVBoxLayout *layout;
    
    MapWidget *map;
    MapNavigationWidget *map_nav;
    
signals:
    
};

class MapEditorContainer : public QWidget
{
    Q_OBJECT
public:
    explicit MapEditorContainer(MapModel *map_model, QWidget *parent = nullptr);
    
    MapWidget *getMap();
    
private:
    MapModel *map_model;
    MapWidget *map;
    MapMenuWidget *map_menu;
    
    QHBoxLayout *layout;
    
signals:
};

#endif // MAP_EDITOR_CONTAINER_H
