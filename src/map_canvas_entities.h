#ifndef MAP_CANVAS_ENTITIES_H
#define MAP_CANVAS_ENTITIES_H

#include <QObject>
#include <QPointer>
#include <QPainter>
#include <QLabel>
#include <QPixmap>

#include <QMouseEvent>

#include "map_model.h"

#include "_enums_structs.h"
#include "map_network_structs.h"

// to avoid circular includes
class MapCanvasWidget;

class MapCanvasEntities : public QObject
{
    Q_OBJECT
public:
    explicit MapCanvasEntities(MapModel *map_model, MapCanvasWidget *map_canvas);
    
    void positionMarkerTank(QMouseEvent *event, QLabel *entity);
    void paintMarkersTank(QPainter &paint);
    
private:
    MapModel *map_model = nullptr;
    // QPointer to avoid circular includes
    QPointer<MapCanvasWidget> map_canvas;
    
    QList<EntityTankMarker> list_tank_markers;
    
signals:
};

#endif // MAP_CANVAS_ENTITIES_H
