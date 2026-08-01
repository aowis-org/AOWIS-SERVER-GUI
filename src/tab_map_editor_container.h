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
#include <QCheckBox>
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
    bool isEditNetworkSectionActive() const;
    
private:
    CanvasMode mode;
    QVBoxLayout *layout;
    
    MapWidget *map;
    MapNavigationWidget *map_nav;
    MapCanvasWidget *map_canvas;
    
    QSpinBox *spin_zoom_from;
    QSpinBox *spin_zoom_to;
    
    QToolBox *toolbox;
    int toolbox_edit_index = -1;
    int toolbox_cache_index = -1;
    void createToolboxCache(QToolBox *tbx);
    void createToolboxEdit(QToolBox *tbx);
    void setToolboxMode(int index);
    void updateToolboxHeight(int index);
    
    QButtonGroup *button_group_tools = nullptr;
    QRadioButton *button_radio_select = nullptr;
    QToolButton *button_tiles_delete = nullptr;
    QCheckBox *checkbox_map_editor_guide = nullptr;

public slots:
    void setMapEditorGuideChecked(bool checked);
    
signals:
    void signalSlideOpacityChanged(int opacity);
    void signalMapEditorGuideVisibilityChanged(bool visible);
    void signalEditNetworkSectionActive(bool active);
};



class MapEditorContainer : public QWidget
{
    Q_OBJECT
public:
    explicit MapEditorContainer(MapModel *map_model, MapTileRepository *tile_repository, HydraulicData *hydraulic_data, GpsProvider *gps, EntityInspectorDock *map_inspector, QWidget *parent = nullptr);
    
    MapWidget *getMap();
    
    MapNavigationWidget *mapNavigationWidget();
    bool isEditNetworkSectionActive() const;

public slots:
    void setMapEditorGuideChecked(bool checked);

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
    void signalMapEditorGuideVisibilityChanged(bool visible);
    void signalEditNetworkSectionActive(bool active);
};

#endif // TAB_MAP_EDITOR_CONTAINER_H
