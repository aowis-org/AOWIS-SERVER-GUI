#ifndef MAP_CANVAS_WIDGET_H
#define MAP_CANVAS_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QLabel>

#include <QPaintEvent>
#include <QPainter>
#include <QColor>
#include <QPalette>

#include <QKeyEvent>
#include <QRect>
#include <QList>

#include "map_model.h"
#include "map_widget.h"

#include "_enums_structs.h"
#include "map_network_structs.h"

class MapCanvasWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapCanvasWidget(MapModel *map_model, MapWidget *map, CanvasMode mode, QWidget *parent = nullptr);
    
    int backgroundOpacity() const;
    
    void startRectangleSelection();
    void cancelRectangleSelection();
    
    void startEntityPositioning(MapEditTool tool);
    
public slots:
    void setBackgroundOpacity(int opacity);
    
protected:
    void paintEvent(QPaintEvent *event) override;
    
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    
private:
    CanvasMode mode;
    MapModel *map_model = nullptr;
    MapWidget *map = nullptr;
    
    int wheel_accumulated = 0;
    
    // 0 = transparent, 100 = fully system bakground
    int map_background_opacity = 0;
    
    // rectangle selection variables and function
    bool rectangle_selection_active = false;
    bool rectangle_dragging = false;
    bool entity_positioning_active = false;
    QLabel *entity_floating = nullptr;
    
    QPoint rectangle_start_pos;
    QPoint rectangle_current_pos;
    
    QRect currentSelectionRect() const;
    void paintEventRectangle(QPainter &paint);
    
    bool key_space_pressed = false;
    
    QList<EntityTankMarker> list_tank_markers;
    
signals:
    void rectangleSelected(const QRect &rect);
    void rectangleSelectionCanceled();
    
    void signalEditToolChange(MapEditTool tool);
    void signalEditToolChangeSub(MapEditToolSub subtool);
    void signalMapProviderChange(MapProvider provider);
};

#endif // MAP_CANVAS_WIDGET_H
