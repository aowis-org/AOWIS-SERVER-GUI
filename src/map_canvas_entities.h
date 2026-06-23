#ifndef MAP_CANVAS_ENTITIES_H
#define MAP_CANVAS_ENTITIES_H

#include <QObject>

class MapCanvasEntities : public QObject
{
    Q_OBJECT
public:
    explicit MapCanvasEntities(QObject *parent = nullptr);

signals:
};

#endif // MAP_CANVAS_ENTITIES_H
