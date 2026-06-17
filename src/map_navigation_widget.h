#ifndef MAP_NAVIGATION_WIDGET_H
#define MAP_NAVIGATION_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QGridLayout>

#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QLabel>
#include <QIcon>

#include "map_widget.h"

class MapNavigationWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapNavigationWidget(MapWidget *map, QWidget *parent = nullptr);
    
private:
    QGridLayout *grid;
    MapWidget *map;
    
signals:
    void signalSlideOpacityChanged(int opacity);
};

#endif // MAP_NAVIGATION_WIDGET_H
