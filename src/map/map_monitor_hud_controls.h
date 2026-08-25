#ifndef MAP_MONITOR_HUD_CONTROLS_H
#define MAP_MONITOR_HUD_CONTROLS_H

#include <QFrame>
#include <QWidget>

class MapModel;
class QComboBox;
class QLabel;
class QSlider;

class MapMonitorViewModeHudWidget final : public QFrame
{
    Q_OBJECT

public:
    explicit MapMonitorViewModeHudWidget(MapModel *map_model, QWidget *parent = nullptr);

private:
    MapModel *map_model = nullptr;
    QComboBox *view_mode_combo = nullptr;
};

class MapMonitorCompassHudWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit MapMonitorCompassHudWidget(MapModel *map_model, QWidget *parent = nullptr);
};

class MapMonitorCameraDistanceHudWidget final : public QFrame
{
    Q_OBJECT

public:
    explicit MapMonitorCameraDistanceHudWidget(MapModel *map_model, QWidget *parent = nullptr);

private:
    MapModel *map_model = nullptr;
    QSlider *distance_slider = nullptr;
    QLabel *distance_maximum_label = nullptr;
    QLabel *distance_value_label = nullptr;
};

class MapMonitorTiltHudWidget final : public QFrame
{
    Q_OBJECT

public:
    explicit MapMonitorTiltHudWidget(MapModel *map_model, QWidget *parent = nullptr);

private:
    MapModel *map_model = nullptr;
    QSlider *tilt_slider = nullptr;
    QLabel *tilt_value_label = nullptr;
};

#endif // MAP_MONITOR_HUD_CONTROLS_H
