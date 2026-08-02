#ifndef TAB_MAP_MONITOR_CONTAINER_H
#define TAB_MAP_MONITOR_CONTAINER_H

#include <QObject>
#include <QWidget>

#ifdef Q_OS_WASM
#include <QEvent>
#include <QTimer>
#endif
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>

#include <QScrollArea>
#include <QScrollBar>
#include <QGroupBox>
#include <QButtonGroup>
#include <QPushButton>
#include <QRadioButton>
#include <QCheckBox>
#include <QSlider>
#include <QLabel>

#include <QIcon>

#include "_enums_structs.h"
#include "map/map_model.h"
#include "map/map_tile_repository.h"
#include "map/map_widget.h"
#include "map/map_navigation_widget.h"
#include "widgets/group_box_collapsible.h"

class HydraulicData;

#ifdef Q_OS_WASM
#include "gps_provider_dummy.h"
#else
#include "gps_provider.h"
#endif

#include "_sizes.h"

class MapMonitorMenuWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapMonitorMenuWidget(MapWidget *map, QWidget *parent = nullptr);
    
    MapNavigationWidget *mapNavigationWidget();

private:
    QVBoxLayout *layout;
    
    MapWidget *map;
    
    MapNavigationWidget *map_nav;
    void addGroupNodeVisuals();
    void addGroupLinkVisuals();
    void addGroupHeatmapVisuals();
    
signals:
    void mapZoomIn();
    void mapZoomOut();
    
    void signalNodeVisualClicked(VisualNode visual_node);
    void signalLinkVisualClicked(VisualLink visual_link);
    void signalHeatmapVisualClicked(VisualHeatmap visual_heatmap);
};





class MapMonitorContainer : public QWidget
{
    Q_OBJECT
public:
    explicit MapMonitorContainer(MapModel *map_model, MapTileRepository *tile_repository, HydraulicData *hydraulic_data, GpsProvider *gps, QWidget *parent = nullptr);
    ~MapMonitorContainer() override;
    
    MapWidget *getMap();
    
    MapNavigationWidget *mapNavigationWidget();

#ifdef Q_OS_WASM
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
#endif

private:
    QHBoxLayout *layout = nullptr;
    GpsProvider *gps = nullptr;
    MapModel *map_model = nullptr;
    MapTileRepository *tile_repository = nullptr;
    HydraulicData *hydraulic_data = nullptr;
    MapWidget *map = nullptr;
    MapMonitorMenuWidget *map_menu = nullptr;

#ifdef Q_OS_WASM
    QTimer *wasm_map_layer_sync_timer = nullptr;
    quint64 wasm_network_geometry_revision_sent = 0;
    bool wasm_network_snapshot_sent = false;

    void scheduleWasmMapLayerSync();
    void syncWasmMapLayer();
    void syncWasmNetworkSnapshot();
#endif

signals:
    void signalShowMapLegendNode(VisualNode visual_node);
    void signalShowMapLegendLink(VisualLink visual_link);
    void signalShowMapLegendHeatmap(VisualHeatmap visual_heatmap);
};



#endif // TAB_MAP_MONITOR_CONTAINER_H
