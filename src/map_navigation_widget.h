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

#include <QTimer>

#include "map_widget.h"
#include "_enums_structs.h"

class MapNavigationWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapNavigationWidget(MapWidget *map, CanvasMode mode, QWidget *parent = nullptr);
    
private:
    CanvasMode mode;
    QGridLayout *grid;
    MapWidget *map;
    
signals:
    void signalSlideOpacityChanged(int opacity);
};

#endif // MAP_NAVIGATION_WIDGET_H
