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
#include <QButtonGroup>
#include <QAbstractButton>

#include <QTimer>

#include "map_widget.h"
#include "../_enums_structs.h"

class MapNavigationWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapNavigationWidget(MapWidget *map, CanvasMode mode, QWidget *parent = nullptr);
    
    void mapProviderChange(MapProvider provider);
    
private:
    CanvasMode mode;
    QGridLayout *grid;
    MapWidget *map;
    
    QPushButton *button_zoom_in;
    QPushButton *button_zoom_out;
    QPushButton *button_up;
    QPushButton *button_down;
    QPushButton *button_left;
    QPushButton *button_right;
    
    QButtonGroup *button_group_map_select;
    QRadioButton* map_arcgissat = nullptr;
    QRadioButton* map_opentopomap = nullptr;
    QRadioButton* map_openstreetmap = nullptr;
    QRadioButton* map_osmcyclo = nullptr;
    
signals:
    void signalSlideOpacityChanged(int opacity);
};

#endif // MAP_NAVIGATION_WIDGET_H
