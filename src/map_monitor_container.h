#ifndef MAP_MONITOR_CONTAINER_H
#define MAP_MONITOR_CONTAINER_H

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

#include "enums_structs.h"
#include "map_widget.h"
#include "map_navigation_widget.h"

class MapNavigation : public QWidget
{
    Q_OBJECT
public:
    explicit MapNavigation(MapWidget *map, QWidget *parent = nullptr);
    
private:
    QVBoxLayout *layout;
    
    MapWidget *map;
    
    void addGroupMapNavigation();
    void addGroupNodeVisuals();
    void addGroupLinkVisuals();
    
    void makeGroupCollapsable(QGroupBox *group);    
    
signals:
    void mapZoomIn();
    void mapZoomOut();
};





class MapMonitorContainer : public QWidget
{
    Q_OBJECT
public:
    explicit MapMonitorContainer(MapWidget *map, QWidget *parent = nullptr);
    
private:
    QHBoxLayout *layout;
    MapWidget *map;
    MapNavigation *controls;
    
signals:
    
};



#endif // MAP_MONITOR_CONTAINER_H
