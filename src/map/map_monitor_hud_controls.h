#ifndef MAP_MONITOR_HUD_CONTROLS_H
#define MAP_MONITOR_HUD_CONTROLS_H

#include <QFrame>
#include <QWidget>

#include "../_enums_structs.h"

class MapModel;
class QComboBox;
class QLabel;
class QPaintEvent;
class QProgressBar;
class QPushButton;
class QTimer;
class MapTileRepository;
class MapTerrainRepository;
class MapRhiWidget;
class QSlider;


class MapMonitorDownloadActivityHudWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit MapMonitorDownloadActivityHudWidget(
        MapTileRepository *tile_repository, MapTerrainRepository *terrain_repository,
        QWidget *parent = nullptr);
    void setHudActive(bool active);

private:
    void setMapTileActivity(int active, int queued);
    void setTerrainActivity(int active, int queued);
    void updatePanel(QFrame *panel, QLabel *label, QPushButton *cancel_button,
                     const QString &name, int active, int queued);
    void refreshActivity();

    MapTileRepository *tile_repository = nullptr;
    MapTerrainRepository *terrain_repository = nullptr;
    QFrame *map_tiles_panel = nullptr;
    QLabel *map_tiles_label = nullptr;
    QPushButton *map_tiles_cancel = nullptr;
    QFrame *terrain_panel = nullptr;
    QLabel *terrain_label = nullptr;
    QPushButton *terrain_cancel = nullptr;
    QTimer *poll_timer = nullptr;
    bool hud_active = false;
};

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

class MapMonitorScaleHudWidget final : public QFrame
{
    Q_OBJECT

public:
    explicit MapMonitorScaleHudWidget(MapModel *map_model, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    MapModel *map_model = nullptr;
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

class MapMonitorNetworkGroundOffsetHudWidget final : public QFrame
{
    Q_OBJECT

public:
    explicit MapMonitorNetworkGroundOffsetHudWidget(
        MapModel *map_model, QWidget *parent = nullptr);

private:
    MapModel *map_model = nullptr;
    QSlider *offset_slider = nullptr;
    QLabel *offset_value_label = nullptr;
};


class MapMonitorVerticalExaggerationHudWidget final : public QFrame
{
    Q_OBJECT

public:
    explicit MapMonitorVerticalExaggerationHudWidget(
        MapModel *map_model, QWidget *parent = nullptr);

private:
    MapModel *map_model = nullptr;
    QSlider *exaggeration_slider = nullptr;
    QLabel *exaggeration_value_label = nullptr;
};


class MapMonitorUndergroundHudWidget final : public QFrame
{
    Q_OBJECT

public:
    explicit MapMonitorUndergroundHudWidget(
        MapRhiWidget *rhi_widget, QWidget *parent = nullptr);

private:
    MapRhiWidget *rhi_widget = nullptr;
    QComboBox *underground_combo = nullptr;
};

class MapMonitorVerticalControlsHudWidget final : public QFrame
{
    Q_OBJECT

public:
    explicit MapMonitorVerticalControlsHudWidget(
        MapModel *map_model, MapRhiWidget *rhi_widget, QWidget *parent = nullptr);
};

#endif // MAP_MONITOR_HUD_CONTROLS_H
