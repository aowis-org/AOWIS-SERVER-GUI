#ifndef MAP_NETWORK_CANVAS_WIDGET_H
#define MAP_NETWORK_CANVAS_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QColor>
#include <QPalette>

#include <QKeyEvent>
#include <QRect>

#include "map_model.h"
#include "map_widget.h"

#include "_enums_structs.h"

class MapNetworkCanvasWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapNetworkCanvasWidget(MapModel *map_model, MapWidget *map, CanvasMode mode, QWidget *parent = nullptr);
    
    int backgroundOpacity() const;
    
    void startRectangleSelection();
    void cancelRectangleSelection();
    
public slots:
    void setBackgroundOpacity(int opacity);
    
signals:
    void rectangleSelected(const QRect &rect);
    void rectangleSelectionCanceled();
    
protected:
    void paintEvent(QPaintEvent *event) override;
    
    void keyPressEvent(QKeyEvent *event) override;
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
    
    QPoint rectangle_start_pos;
    QPoint rectangle_current_pos;
    
    QRect currentSelectionRect() const;
    void paintEventRectangle(QPainter &paint);
    
signals:
};

#endif // MAP_NETWORK_CANVAS_WIDGET_H
