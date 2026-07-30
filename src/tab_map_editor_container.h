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
#include <QPushButton>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QKeySequence>

#include "map/map_model.h"
#include "map/map_tile_repository.h"
#include "map/map_widget.h"
#include "map/map_navigation_widget.h"
#include "map/map_canvas_widget.h"
#include "entity_inspector/entity_inspector_dock.h"
#include "hydraulic_data.h"

#ifdef Q_OS_WASM
#include "gps_provider_dummy.h"
#else
#include "gps_provider.h"
#endif

#include "_sizes.h"
#include "_enums_structs.h"

#include <QDebug>

class MapEditorMenuWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapEditorMenuWidget(MapWidget *map, MapCanvasWidget *map_canvas, CanvasMode mode, QWidget *parent = nullptr);
    
    MapNavigationWidget *mapNavigationWidget();
    
private:
    CanvasMode mode;
    QVBoxLayout *layout;
    
    MapWidget *map;
    MapNavigationWidget *map_nav;
    MapCanvasWidget *map_canvas;
    
    QSpinBox *spin_zoom_from;
    QSpinBox *spin_zoom_to;
    
    QToolBox *toolbox;
    void createToolboxCache(QToolBox *tbx);
    void createToolboxEdit(QToolBox *tbx);
    
    QButtonGroup *button_group_tools = nullptr;
    
signals:
    void signalSlideOpacityChanged(int opacity);
};



class MapEditorContainer : public QWidget
{
    Q_OBJECT
public:
    explicit MapEditorContainer(MapModel *map_model, MapTileRepository *tile_repository, HydraulicData *hydraulic_data, GpsProvider *gps, EntityInspectorDock *map_inspector, QWidget *parent = nullptr);
    
    MapWidget *getMap();
    
    MapNavigationWidget *mapNavigationWidget();
    
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    
private:
    HydraulicData *hydraulic_data = nullptr;
    GpsProvider *gps;
    
    EntityInspectorDock *map_inspector;
    
    MapModel *map_model;
    MapTileRepository *tile_repository;
    MapWidget *map;
    MapCanvasWidget *map_canvas;
    MapEditorMenuWidget *map_menu;
    
    QHBoxLayout *layout;
    QWidget *map_stack;
    QStackedLayout *map_stack_layout;
    
signals:
    
};

#endif // TAB_MAP_EDITOR_CONTAINER_H
