#ifndef MAP_NETWORK_CANVAS_WIDGET_H
#define MAP_NETWORK_CANVAS_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QColor>
#include <QPalette>

#include "map_model.h"

class MapNetworkCanvasWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapNetworkCanvasWidget(MapModel *map_model, QWidget *parent = nullptr);
    
    int backgroundOpacity() const;
    
public slots:
    void setBackgroundOpacity(int opacity);
    
protected:
    void paintEvent(QPaintEvent *event) override;
    
private:
    MapModel *map_model;
    
    // 0 = transparent, 100 = fully system bakground
    int map_background_opacity = 0;
    
signals:
};

#endif // MAP_NETWORK_CANVAS_WIDGET_H
