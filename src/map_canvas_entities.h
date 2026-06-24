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
    
    void startEntityPositioning(MapEditTool tool);
    void floatEntity(QMouseEvent *event);
    bool positionMarkerTank(QMouseEvent *event);
    void paintMarkersTank(QPainter &paint);
    
private:
    MapModel *map_model = nullptr;
    // QPointer to avoid circular includes
    QPointer<MapCanvasWidget> map_canvas;
    
    QList<EntityTankMarker> list_tank_markers;
    
    QLabel *entity_floating = nullptr;
    bool is_entity_floating = false;
    
    MapEditTool tool_current;
    
signals:
};

#endif // MAP_CANVAS_ENTITIES_H
