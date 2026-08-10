#ifndef TAB_MAP_MONITOR_CONTAINER_H
#define TAB_MAP_MONITOR_CONTAINER_H

#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QWidget>
#include <QUuid>

#ifdef Q_OS_WASM
#include <QTimer>
#endif
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QStackedLayout>

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
#include "network_symbology.h"
#include "map/map_model.h"
#include "map/map_tile_repository.h"
#include "map/map_widget.h"
#include "map/map_navigation_widget.h"
#include "widgets/group_box_collapsible.h"

class HydraulicData;
class MapNetworkOverlayWidget;

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
    void addGroupVisualSizes();
    void addGroupNodeVisuals();
    void addGroupLinkVisuals();
    void addGroupHeatmapVisuals();
    
signals:
    void mapZoomIn();
    void mapZoomOut();
    
    void signalNodeVisualClicked(VisualNode visual_node);
    void signalNodeSizeChanged(int size_percent);
    void signalLinkVisualClicked(VisualLink visual_link);
    void signalLinkThicknessChanged(int thickness_px);
    void signalHeatmapVisualClicked(VisualHeatmap visual_heatmap);
    void signalHeatmapOpacityChanged(int opacity);
    void signalHeatmapRadiusChanged(int radius);
    void signalHeatmapSolidCenterChanged(int percent);
};





class MapMonitorContainer : public QWidget
{
    Q_OBJECT
public:
    explicit MapMonitorContainer(MapModel *map_model, MapTileRepository *tile_repository, HydraulicData *hydraulic_data, GpsProvider *gps, QWidget *parent = nullptr);
    ~MapMonitorContainer() override;
    
    MapWidget *getMap();
    
    MapNavigationWidget *mapNavigationWidget();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QHBoxLayout *layout = nullptr;
    GpsProvider *gps = nullptr;
    MapModel *map_model = nullptr;
    MapTileRepository *tile_repository = nullptr;
    HydraulicData *hydraulic_data = nullptr;
    QWidget *map_stack = nullptr;
    QStackedLayout *map_stack_layout = nullptr;
    MapWidget *map = nullptr;
#ifndef Q_OS_WASM
    MapNetworkOverlayWidget *desktop_network_overlay = nullptr;
#endif
    MapMonitorMenuWidget *map_menu = nullptr;
    int network_background_opacity = 0;
    NetworkSymbologySettings symbology_settings;

    bool selectNetworkEntity(quint32 render_id, InfrastructureEntity entity_type, const QUuid &uuid = QUuid());
    void applySymbology();
    void setNetworkBackgroundOpacity(int opacity);

#ifndef Q_OS_WASM
    bool desktop_network_hovered = false;

    void updateDesktopNetworkHover(const QPointF &position, Qt::MouseButtons buttons);
    void setDesktopNetworkHovered(bool hovered);
#endif

#ifdef Q_OS_WASM
    QTimer *wasm_map_layer_sync_timer = nullptr;
    QTimer *wasm_network_symbology_sync_timer = nullptr;
    quint64 wasm_network_geometry_revision_sent = 0;
    bool wasm_network_snapshot_sent = false;
    int wasm_network_symbology_sync_retry_count = 0;
    InfrastructureEntity wasm_selected_entity_type = InfrastructureEntity::Unknown;
    QUuid wasm_selected_entity_uuid;

    bool selectWasmNetworkEntityAt(const QPointF &position);
    void syncWasmSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid);
    void scheduleWasmMapLayerSync();
    void scheduleWasmNetworkSymbologySync();
    void syncWasmMapLayer();
    void syncWasmNetworkBackground();
    void syncWasmNetworkSnapshot();
    void syncWasmNetworkSymbology();
#endif

signals:
    void signalShowMapLegendNode(VisualNode visual_node);
    void signalShowMapLegendLink(VisualLink visual_link);
    void signalShowMapLegendHeatmap(VisualHeatmap visual_heatmap);
};



#endif // TAB_MAP_MONITOR_CONTAINER_H
