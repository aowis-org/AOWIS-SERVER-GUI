#ifndef MAP_NETWORK_CANVAS_WIDGET_H
#define MAP_NETWORK_CANVAS_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QColor>
#include <QPalette>

#include <QKeyEvent>

#include "map_model.h"
#include "map_widget.h"

class MapNetworkCanvasWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapNetworkCanvasWidget(MapModel *map_model, MapWidget *map, QWidget *parent = nullptr);
    
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
    
private:
    MapModel *map_model = nullptr;
    MapWidget *map = nullptr;
    
    // 0 = transparent, 100 = fully system bakground
    int map_background_opacity = 0;
    
    bool rectangle_selection_active = false;
    bool rectangle_dragging = false;
    
    QPoint rectangle_start_pos;
    QPoint rectangle_current_pos;
    
    QRect currentSelectionRect() const;
    
signals:
};

#endif // MAP_NETWORK_CANVAS_WIDGET_H
