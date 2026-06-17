#ifndef MAP_NETWORK_CANVAS_H
#define MAP_NETWORK_CANVAS_H

#include <QObject>
#include <QWidget>

class MapNetworkCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit MapNetworkCanvas(QWidget *parent = nullptr);

signals:
};

#endif // MAP_NETWORK_CANVAS_H
