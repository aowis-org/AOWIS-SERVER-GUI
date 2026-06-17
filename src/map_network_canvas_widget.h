#ifndef MAP_NETWORK_CANVAS_WIDGET_H
#define MAP_NETWORK_CANVAS_WIDGET_H

#include <QObject>
#include <QWidget>

#include "map_model.h"

class MapNetworkCanvasWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapNetworkCanvasWidget(MapModel *map_model, QWidget *parent = nullptr);
    
private:
    MapModel *map_model;
    
signals:
};

#endif // MAP_NETWORK_CANVAS_WIDGET_H
