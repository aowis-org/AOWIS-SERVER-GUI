#ifndef MAP_NAVIGATION_WIDGET_H
#define MAP_NAVIGATION_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QGridLayout>

#include <QPushButton>
#include <QRadioButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QIcon>
#include <QButtonGroup>
#include <QAbstractButton>

#include <QTimer>

#include "map/core/map_widget.h"
#include "common/_enums_structs.h"
#include "network/network_symbology.h"

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
    QComboBox *combo_icon_size_unit = nullptr;
    int icon_size_2d_percent = 100;
    int icon_size_3d_percent = 100;
    NetworkSymbologySizeUnit icon_size_2d_unit = NetworkSymbologySizeUnit::Pixels;
    int icon_size_2d_px = NetworkSymbologyDefaultIconSizePx;
    double icon_size_2d_m = NetworkSymbologyDefaultIconSizeM;
    NetworkSymbologySizeUnit icon_size_3d_unit = NetworkSymbologySizeUnit::Pixels;
    int icon_size_3d_px = NetworkSymbologyDefaultIconSizePx;
    double icon_size_3d_m = NetworkSymbologyDefaultIconSizeM;

    void activateMapProvider(MapProvider provider);
    void syncIconSizeSliderForViewMode(MapViewMode view_mode);
    void refreshShortcutPresentation();
    
signals:
    void signalSlideOpacityChanged(int opacity);
    void signalIconSizeChanged(int size_percent);
    void signalMonitorIconSizeUnitChanged(NetworkSymbologySizeUnit unit);
    void signalMonitorIconSizeChanged(NetworkSymbologySizeUnit unit, double size);
    void signalSyncMapMovementStateChanged(bool sync);
    
public slots:
    void mapMovementSyncStateChange(bool sync);
};

#endif // MAP_NAVIGATION_WIDGET_H
