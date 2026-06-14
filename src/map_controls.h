#ifndef MAP_CONTROLS_H
#define MAP_CONTROLS_H

#include <QObject>

class MapControls : public QObject
{
    Q_OBJECT
public:
    explicit MapControls(QObject *parent = nullptr);

signals:
};

#endif // MAP_CONTROLS_H
