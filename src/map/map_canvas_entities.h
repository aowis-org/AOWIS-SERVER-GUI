#ifndef MAP_CANVAS_ENTITIES_H
#define MAP_CANVAS_ENTITIES_H

#include <QObject>
#include <QPointer>
#include <QPainter>
#include <QLabel>
#include <QPixmap>

#include <QMouseEvent>

#include "map_model.h"
#include "map_entity_marker_label.h"

#include "../_enums_structs.h"
#include "map_network_structs.h"

// to avoid circular includes
class MapCanvasWidget;

class MapCanvasEntities : public QObject
{
    Q_OBJECT
public:
    explicit MapCanvasEntities(MapModel *map_model, MapCanvasWidget *map_canvas);
    
    void startEntityPositioning(MapEditTool tool);
    void startEntityPositioningInternal();
    void stopEntityPositioning();
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
    
    MapEntityPlacementMode entity_placement_mode = MapEntityPlacementMode::None;
    MapEntityMarkerLabel *entity_floating = nullptr;
    
    MapEditTool tool_current;
    
    int calculateEntityWidth();
    
    QPoint entity_floating_hide_until;
    // on tool change, the rearming should not be active
    bool entity_draw_immediately = true;
    QPointF mouse_pos_last;
    
private slots:
    void onTankMarkerDeleteRequested(MapEntityMarkerLabel *label);
    void onMarkerMoveRequested(MapEntityMarkerLabel *label);
    
signals:
};

#endif // MAP_CANVAS_ENTITIES_H
