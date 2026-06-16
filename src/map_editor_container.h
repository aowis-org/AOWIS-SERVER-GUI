#ifndef MAP_EDITOR_CONTAINER_H
#define MAP_EDITOR_CONTAINER_H

#include <QObject>
#include <QWidget>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QToolBox>
#include <QToolButton>

#include "map_model.h"
#include "map_widget.h"
#include "map_navigation_widget.h"

class MapEditorMenuWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapEditorMenuWidget(MapWidget *map, QWidget *parent = nullptr);
    
private:
    QVBoxLayout *layout;
    
    MapWidget *map;
    MapNavigationWidget *map_nav;
    
    QToolBox *toolbox_cache;
    QToolBox *createToolboxCache();
    
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
    MapEditorMenuWidget *map_menu;
    
    QHBoxLayout *layout;
    
signals:
};

#endif // MAP_EDITOR_CONTAINER_H
