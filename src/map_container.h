#ifndef MAP_CONTAINER_H
#define MAP_CONTAINER_H

#include <QObject>
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>

#include <QGroupBox>
#include <QButtonGroup>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QLabel>

#include <QIcon>

#include "enums_structs.h"
#include "map_widget.h"

class MapControls : public QWidget
{
    Q_OBJECT
public:
    explicit MapControls(MapWidget *map, QWidget *parent = nullptr);
    
private:
    QVBoxLayout *layout;
    
    MapWidget *map;
    
    void addGroupMapControls();
    
signals:
    void mapZoomIn();
    void mapZoomOut();
};





class MapContainer : public QWidget
{
    Q_OBJECT
public:
    explicit MapContainer(QWidget *parent = nullptr);
    
    MapWidget* mapWidget();
    
private:
    QHBoxLayout *layout;
    MapWidget *map;
    MapControls *controls;
    
signals:
    
};



#endif // MAP_CONTAINER_H
