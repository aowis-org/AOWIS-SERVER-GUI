#ifndef MAP_CANVAS_PLACEMENT_H
#define MAP_CANVAS_PLACEMENT_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QUuid>

#include "map_models.h"

class MapCanvasWidget;

class MapCanvasPlacement : public QObject
{
    Q_OBJECT

public:
    explicit MapCanvasPlacement(MapCanvasWidget *map_canvas, QObject *parent = nullptr);

    void startCreate(InfrastructureEntity entity, const QString &pixmap_path, int width);
    bool rearmCreate(const QString &pixmap_path, int width);
    bool startMove(InfrastructureEntity entity, const QUuid &uuid,
                   const QString &pixmap_path, int width,
                   const QPointF &mouse_position);
    void startVirtualMove(InfrastructureEntity entity, const QPointF &mouse_position);
    void stop();

    MapEntityPlacementMode mode() const;
    InfrastructureEntity entity() const;
    bool isIdle() const;
    bool isCreating() const;
    bool isMoving() const;

    QUuid floatingUuid() const;
    QString floatingPixmapPath() const;
    int floatingWidth() const;
    bool hasFloatingMarker() const;
    bool floatingMarkerVisible() const;
    void consumeCreatedMarker();
    void completeMove();

    void updateMousePosition(const QPointF &position);
    QPointF mousePosition() const;
    QPointF previousMousePosition() const;
    bool revealFloatingMarkerIfReady();
    void setFloatingHiddenUntil(const QPoint &position);
    void scaleFloatingMarker(const QString &pixmap_path, int width);

    void setConnectionTarget(const QUuid &uuid);
    QUuid connectionTargetUuid() const;
    void clearConnectionTarget();

    void setMovingSelected(bool moving_selected);
    bool movingSelected() const;
    void setMoveCursor(bool enabled);

private:
    void prepareFloatingMarker();
    void focusCanvas();

    QPointer<MapCanvasWidget> map_canvas;
    QUuid floating_uuid;
    QUuid connection_target_uuid;
    QString floating_pixmap_path;
    int floating_width = 0;
    MapEntityPlacementMode placement_mode = MapEntityPlacementMode::None;
    InfrastructureEntity current_entity = InfrastructureEntity::Unknown;
    QPoint floating_hide_until;
    QPointF mouse_position;
    QPointF previous_mouse_position;
    bool draw_immediately = true;
    bool floating_visible = false;
    bool move_cursor_active = false;
    bool moving_selected = false;
};

#endif // MAP_CANVAS_PLACEMENT_H
