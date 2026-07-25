#ifndef MAP_CANVAS_PIPES_H
#define MAP_CANVAS_PIPES_H

#include <QObject>

class MapCanvasPipes : public QObject
{
    Q_OBJECT
public:
    explicit MapCanvasPipes(QObject *parent = nullptr);

signals:
};

#endif // MAP_CANVAS_PIPES_H
