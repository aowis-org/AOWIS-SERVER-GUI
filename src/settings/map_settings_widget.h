#ifndef MAP_SETTINGS_WIDGET_H
#define MAP_SETTINGS_WIDGET_H

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class SliderNumberControl;

// Settings > Map Settings: navigation sensitivity, performance knobs for
// the RHI 3D renderer, and map server connection details.
class MapSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MapSettingsWidget(QWidget *parent = nullptr);

private:
    // Navigation section.
    SliderNumberControl *scroll_zoom_sensitivity_control = nullptr;
    SliderNumberControl *mouse_3d_pan_sensitivity_control = nullptr;
    SliderNumberControl *orbit_3d_sensitivity_control = nullptr;
    QLabel *navigation_status = nullptr;
    QTimer *navigation_save_debounce = nullptr;

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

    void buildNavigationSection(QWidget *parent_widget);
    void buildPerformanceSection(QWidget *parent_widget);
    void buildServerSection(QWidget *parent_widget);
    void scheduleNavigationSave();
    void saveNavigationSettingsNow();
    void schedulePerformanceSave();
    void savePerformanceSettingsNow();
    void saveServerSettings();
    void showNavigationStatus(const QString &message, bool error);
    void showPerformanceStatus(const QString &message, bool error);
    void showServerStatus(const QString &message, bool error);
};

#endif // MAP_SETTINGS_WIDGET_H
