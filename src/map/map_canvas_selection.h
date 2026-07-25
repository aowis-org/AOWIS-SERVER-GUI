#ifndef MAP_CANVAS_SELECTION_H
#define MAP_CANVAS_SELECTION_H

#include <QObject>

class MapCanvasSelection : public QObject
{
    Q_OBJECT
public:
    explicit MapCanvasSelection(QObject *parent = nullptr);

signals:
};

#endif // MAP_CANVAS_SELECTION_H
