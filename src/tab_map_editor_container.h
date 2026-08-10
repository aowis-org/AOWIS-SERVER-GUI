#ifndef TAB_MAP_EDITOR_CONTAINER_H
#define TAB_MAP_EDITOR_CONTAINER_H

#include <QObject>
#include <QWidget>

#ifdef Q_OS_WASM
#include <QByteArray>
#include <QEvent>
#include <QSet>
#include <QTimer>
#endif

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
#include "map/map_editor_controller.h"
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
    explicit MapEditorMenuWidget(MapWidget *map, MapCanvasWidget *map_canvas,
                                 MapEditorController *editor_controller, CanvasMode mode,
                                 QWidget *parent = nullptr);
    
    MapNavigationWidget *mapNavigationWidget();
    bool isEditNetworkSectionActive() const;
    
private:
    CanvasMode mode;
    QVBoxLayout *layout;
    
    MapWidget *map;
    MapNavigationWidget *map_nav;
    MapCanvasWidget *map_canvas;
    MapEditorController *editor_controller;
    
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
    ~MapEditorContainer() override;
    
    MapWidget *getMap();
    
    MapNavigationWidget *mapNavigationWidget();
    bool isEditNetworkSectionActive() const;

public slots:
    void setMapEditorGuideChecked(bool checked);

#ifdef Q_OS_WASM
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
#endif

private:
    HydraulicData *hydraulic_data = nullptr;
    GpsProvider *gps;
    
    EntityInspectorDock *map_inspector;
    
    MapModel *map_model;
    MapTileRepository *tile_repository;
    QWidget *map_stack;
    QStackedLayout *map_stack_layout;
    MapWidget *map;
    MapCanvasWidget *map_canvas;
    MapEditorController *editor_controller;
    MapEditorMenuWidget *map_menu;
    
    QHBoxLayout *layout;

#ifdef Q_OS_WASM
    QTimer *wasm_map_layer_sync_timer = nullptr;
    quint64 wasm_network_geometry_revision_sent = 0;
    quint64 wasm_visual_state_revision_sent = 0;
    QByteArray wasm_viewport_state_sent;
    QSet<QUuid> wasm_network_node_uuids_sent;
    QSet<QUuid> wasm_network_link_uuids_sent;
    QSet<QUuid> wasm_dirty_node_uuids;
    QSet<QUuid> wasm_dirty_link_uuids;
    bool wasm_network_snapshot_sent = false;
    bool wasm_visual_state_sent = false;

    void scheduleWasmMapLayerSync();
    void syncWasmMapLayers();
    void syncWasmNetworkSnapshot();
    void syncWasmVisualState();
    void syncWasmViewportState();
    void syncWasmBackground();
#endif
    
signals:
    void signalMapEditorGuideVisibilityChanged(bool visible);
    void signalEditNetworkSectionActive(bool active);
};

#endif // TAB_MAP_EDITOR_CONTAINER_H
