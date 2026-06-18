#ifndef TAB_MAP_EDITOR_CONTAINER_H
#define TAB_MAP_EDITOR_CONTAINER_H

#include <QObject>
#include <QWidget>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QScrollArea>
#include <QToolBox>
#include <QToolButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QLabel>

#include "map_model.h"
#include "map_widget.h"
#include "map_navigation_widget.h"
#include "map_network_canvas_widget.h"

#include "_sizes.h"
#include "_enums_structs.h"

#include <QDebug>

class MapEditorMenuWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapEditorMenuWidget(MapWidget *map, MapNetworkCanvasWidget *map_canvas, CanvasMode mode, QWidget *parent = nullptr);
    
private:
    CanvasMode mode;
    QVBoxLayout *layout;
    
    MapWidget *map;
    MapNavigationWidget *map_nav;
    MapNetworkCanvasWidget *map_canvas;
    
    QSpinBox *spin_zoom_from;
    QSpinBox *spin_zoom_to;
    
    QToolBox *toolbox;
    void createToolboxCache(QToolBox *tbx);
    void createToolboxEdit(QToolBox *tbx);
    
signals:
    void signalSlideOpacityChanged(int opacity);
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
    MapNetworkCanvasWidget *map_canvas;
    MapEditorMenuWidget *map_menu;
    
    QHBoxLayout *layout;
    QWidget *map_stack;
    QStackedLayout *map_stack_layout;
    
signals:
    
};

#endif // TAB_MAP_EDITOR_CONTAINER_H
