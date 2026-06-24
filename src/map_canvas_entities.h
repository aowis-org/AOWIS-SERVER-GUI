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
    bool anchorMarkerTank(QMouseEvent *event);
    void scaleMarkersTank();
    //void updateMarkersTank(QPainter &paint);
    void positionMarkersTank();
    void paintMarkersTank(QPainter &paint);
    
private:
    MapModel *map_model = nullptr;
    // QPointer to avoid circular includes
    QPointer<MapCanvasWidget> map_canvas;
    
    QList<EntityTankMarker> list_tank_markers;
    
    QLabel *entity_floating = nullptr;
    
    MapEditTool tool_current;
    
    int calculateEntityWidth();
    
    QPoint entity_floating_hide_until;
    
signals:
};

#endif // MAP_CANVAS_ENTITIES_H
