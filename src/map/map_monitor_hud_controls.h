#ifndef MAP_MONITOR_HUD_CONTROLS_H
#define MAP_MONITOR_HUD_CONTROLS_H

#include <QFrame>

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

class MapMonitorCameraHudWidget final : public QFrame
{
    Q_OBJECT

public:
    explicit MapMonitorCameraHudWidget(MapModel *map_model, QWidget *parent = nullptr);

private:
    MapModel *map_model = nullptr;
    QSlider *tilt_slider = nullptr;
    QLabel *tilt_value_label = nullptr;
};

#endif // MAP_MONITOR_HUD_CONTROLS_H
