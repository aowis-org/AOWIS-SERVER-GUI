#ifndef MAP_CANVAS_DEVICELINKS_H
#define MAP_CANVAS_DEVICELINKS_H

#include <QObject>

class MapCanvasDeviceLinks : public QObject
{
    Q_OBJECT
public:
    explicit MapCanvasDeviceLinks(QObject *parent = nullptr);

signals:
};

#endif // MAP_CANVAS_DEVICELINKS_H
