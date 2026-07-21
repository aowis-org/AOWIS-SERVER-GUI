#ifndef MAP_CANVAS_WIDGET_H
#define MAP_CANVAS_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QLabel>

#include <QResizeEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QColor>
#include <QPalette>

#include <QKeyEvent>
#include <QRect>
#include <QList>

#include "map_model.h"
#include "map_widget.h"
#include "map_canvas_entities.h"
#include "map_models.h"

#include "../hydraulic_data.h"
#include "../_enums_structs.h"

class MapCanvasWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapCanvasWidget(MapModel *map_model, MapWidget *map, HydraulicData *hydraulic_data, CanvasMode mode, QWidget *parent = nullptr);
    
    int backgroundOpacity() const;
    
    void startRectangleSelection();
    void cancelRectangleSelection();
    
    void startEntityPositioning(InfrastructureEntity tool);
    void stopEntityPositioning();
    
    bool onKeyPressEvent(QKeyEvent *event);
    
public slots:
    void setBackgroundOpacity(int opacity);
    
protected:
    void paintEvent(QPaintEvent *event) override;
    
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    
    void focusOutEvent(QFocusEvent *event) override;
    
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    
    void resizeEvent(QResizeEvent *event) override;
    
private:
    CanvasMode mode;
    MapModel *map_model = nullptr;
    MapWidget *map = nullptr;
    HydraulicData *hydraulic_data = nullptr;
    MapCanvasEntities *map_canvas_entities = nullptr;
    
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
    
    bool key_space_pressed = false;
    
signals:
    void rectangleSelected(const QRect &rect);
    void rectangleSelectionCanceled();
    
};

#endif // MAP_CANVAS_WIDGET_H
