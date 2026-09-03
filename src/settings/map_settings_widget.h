#ifndef MAP_SETTINGS_WIDGET_H
#define MAP_SETTINGS_WIDGET_H

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class SliderNumberControl;

// Settings > Map Settings: performance knobs for the RHI 3D renderer
// (view distance, terrain level-of-detail, draw-call batching), and the
// map server connection details normally only found in the config file.
class MapSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MapSettingsWidget(QWidget *parent = nullptr);

private:
    // Performance section.
    SliderNumberControl *view_distance_control = nullptr;
    SliderNumberControl *terrain_lod_target_control = nullptr;
    SliderNumberControl *terrain_max_detail_zoom_control = nullptr;
    SliderNumberControl *terrain_full_detail_zoom_control = nullptr;
    QCheckBox *array_batching_checkbox = nullptr;
    QLabel *performance_status = nullptr;
    QTimer *performance_save_debounce = nullptr;

    // Server section.
    QLineEdit *server_base_url_edit = nullptr;
    QLineEdit *server_api_key_edit = nullptr;
    QLineEdit *server_delete_api_key_edit = nullptr;
    QPushButton *server_save_button = nullptr;
    QLabel *server_status = nullptr;

    void buildPerformanceSection(QWidget *parent_widget);
    void buildServerSection(QWidget *parent_widget);
    void schedulePerformanceSave();
    void savePerformanceSettingsNow();
    void saveServerSettings();
    void showPerformanceStatus(const QString &message, bool error);
    void showServerStatus(const QString &message, bool error);
};

#endif // MAP_SETTINGS_WIDGET_H
