#ifndef MAP_NAVIGATION_WIDGET_H
#define MAP_NAVIGATION_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QGridLayout>

#include <QPushButton>
#include <QRadioButton>
#include <QCheckBox>
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
    explicit MapNavigationWidget(MapWidget *map, CanvasMode mode, QWidget *keyboard_focus_target, QWidget *parent = nullptr);
    
    void mapProviderChange(MapProvider provider);
    
private:
    CanvasMode mode;
    QGridLayout *grid;
    MapWidget *map;
    QWidget *keyboard_focus_target;
    
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
    
    QCheckBox* check_map_sync = nullptr;
    QSlider *slider_icon_size = nullptr;
    int icon_size_2d_percent = 100;
    int icon_size_3d_percent = 100;

    void activateMapProvider(MapProvider provider);
    void syncIconSizeSliderForViewMode(MapViewMode view_mode);
    void refreshShortcutPresentation();
    
signals:
    void signalSlideOpacityChanged(int opacity);
    void signalIconSizeChanged(int size_percent);
    void signalSyncMapMovementStateChanged(bool sync);
    
public slots:
    void mapMovementSyncStateChange(bool sync);
};

#endif // MAP_NAVIGATION_WIDGET_H
