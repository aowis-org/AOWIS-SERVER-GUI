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
#include <QSlider>
#include <QLabel>

#include <QIcon>

#include "_enums_structs.h"
#include "map_model.h"
#include "map_widget.h"
#include "map_navigation_widget.h"
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
};





class MapMonitorContainer : public QWidget
{
    Q_OBJECT
public:
    explicit MapMonitorContainer(MapModel *map_model, GpsProvider *gps, QWidget *parent = nullptr);
    
    MapWidget *getMap();
    
private:
    QHBoxLayout *layout;
    MapModel *map_model;
    MapWidget *map;
    MapMonitorMenuWidget *controls;
    
    GpsProvider *gps = nullptr;
    
signals:
    
};



#endif // TAB_MAP_MONITOR_CONTAINER_H
