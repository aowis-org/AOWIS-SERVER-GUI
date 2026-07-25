#ifndef MAP_CANVAS_MARKERS_H
#define MAP_CANVAS_MARKERS_H

#include <QObject>

class MapCanvasMarkers : public QObject
{
    Q_OBJECT
public:
    explicit MapCanvasMarkers(QObject *parent = nullptr);

signals:
};

#endif // MAP_CANVAS_MARKERS_H
