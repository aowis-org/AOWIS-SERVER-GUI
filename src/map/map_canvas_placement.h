#ifndef MAP_CANVAS_PLACEMENT_H
#define MAP_CANVAS_PLACEMENT_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QString>

#include "map_models.h"

class MapCanvasWidget;
class MapEntityMarkerLabel;

class MapCanvasPlacement : public QObject
{
    Q_OBJECT
    
public:
    explicit MapCanvasPlacement(MapCanvasWidget *map_canvas, QObject *parent = nullptr);
    
    void startCreate(InfrastructureEntity entity, const QString &pixmap_path, int width);
    bool rearmCreate(const QString &pixmap_path, int width);
    bool startMove(InfrastructureEntity entity, MapEntityMarkerLabel *label, const QPointF &mouse_position);
    void startVirtualMove(InfrastructureEntity entity, const QPointF &mouse_position);
    void stop();
    
    MapEntityPlacementMode mode() const;
    InfrastructureEntity entity() const;
    bool isIdle() const;
    bool isCreating() const;
    bool isMoving() const;
    
    MapEntityMarkerLabel *floatingLabel() const;
    MapEntityMarkerLabel *takeCreatedLabel();
    void completeMove();
    
    void updateMousePosition(const QPointF &position);
    QPointF mousePosition() const;
    QPointF previousMousePosition() const;
    bool revealFloatingLabelIfReady();
    void moveFloatingLabelTopLeft(const QPointF &position);
    void setFloatingHiddenUntil(const QPoint &position);
    void scaleFloatingLabel(const QString &pixmap_path, int width);
    
    void setConnectionTarget(MapEntityMarkerLabel *label);
    MapEntityMarkerLabel *connectionTarget() const;
    void clearConnectionTarget();
    
    void setMovingSelected(bool moving_selected);
    bool movingSelected() const;
    void setMoveCursor(bool enabled);
    
private:
    void createFloatingLabel(const QString &pixmap_path, int width);
    void prepareFloatingLabel();
    void focusCanvas();
    
    QPointer<MapCanvasWidget> map_canvas;
    QPointer<MapEntityMarkerLabel> floating_label;
    QPointer<MapEntityMarkerLabel> connection_target_label;
    MapEntityPlacementMode placement_mode = MapEntityPlacementMode::None;
    InfrastructureEntity current_entity = InfrastructureEntity::Unknown;
    QPoint floating_hide_until;
    QPointF mouse_position;
    QPointF previous_mouse_position;
    bool draw_immediately = true;
    bool move_cursor_active = false;
    bool moving_selected = false;
};

#endif // MAP_CANVAS_PLACEMENT_H
