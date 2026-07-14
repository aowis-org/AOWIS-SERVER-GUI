#ifndef TAB_MAP_MONITOR_CONTAINER_H
#define TAB_MAP_MONITOR_CONTAINER_H

#include <QObject>
#include <QWidget>
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
#include "map/map_widget.h"
#include "map/map_navigation_widget.h"
#include "widgets/group_box_collapsible.h"

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
    
private:
    QVBoxLayout *layout;
    
    MapWidget *map;
    
    MapNavigationWidget *map_nav;
    void addGroupNodeVisuals();
    void addGroupLinkVisuals();
    
signals:
    void mapZoomIn();
    void mapZoomOut();
    
    void signalNodeVisualClicked(VisualNode visual_node);
    void signalLinkVisualClicked(VisualLink visual_link);
};





class MapMonitorContainer : public QWidget
{
    Q_OBJECT
public:
    explicit MapMonitorContainer(MapModel *map_model, GpsProvider *gps, QWidget *parent = nullptr);
    
    MapWidget *getMap();
    
private:
    QHBoxLayout *layout = nullptr;
    MapModel *map_model = nullptr;
    MapWidget *map = nullptr;
    MapMonitorMenuWidget *controls = nullptr;
    
    GpsProvider *gps = nullptr;
    
signals:
    void signalShowMapLegendNode(VisualNode visual_node);
    void signalShowMapLegendLink(VisualLink visual_link);
};



#endif // TAB_MAP_MONITOR_CONTAINER_H
