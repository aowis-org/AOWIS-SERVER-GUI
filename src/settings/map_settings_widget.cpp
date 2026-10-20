#include "settings/map_settings_widget.h"

#include "config/gui_configuration.h"
#include "config/map_server_client_configuration.h"
#include "map/render/map_globe_terrain_levels.h"
#include "widgets/slider_number_control.h"

#include <QColor>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
// Debounce persistent writes while a slider is actively being dragged.
constexpr int SettingsSaveDebounceMs = 400;
constexpr double NavigationSensitivityMinimum = 0.1;
constexpr double NavigationSensitivityMaximum = 1.5;
constexpr double NavigationSensitivityStep = 0.05;


QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPointSize(font.pointSize() + 2);
    font.setBold(true);
    label->setFont(font);
    return label;
}

QLabel *helpLabel(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setWordWrap(true);
    QFont font = label->font();
    font.setItalic(true);
    label->setFont(font);
    return label;
}
}

MapSettingsWidget::MapSettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    QScrollArea *scroll_area = new QScrollArea(this);
    scroll_area->setWidgetResizable(true);
    scroll_area->setFrameShape(QFrame::NoFrame);

    QWidget *content = new QWidget(scroll_area);
    QVBoxLayout *content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(24, 20, 24, 20);
    content_layout->setSpacing(20);

    QLabel *title = sectionTitle(QStringLiteral("Map Settings"), content);
    content_layout->addWidget(title);

    buildNavigationSection(content);
    buildPerformanceSection(content);
    buildServerSection(content);
    content_layout->addStretch(1);

    scroll_area->setWidget(content);

    QVBoxLayout *outer_layout = new QVBoxLayout(this);
    outer_layout->setContentsMargins(0, 0, 0, 0);
    outer_layout->addWidget(scroll_area);
}

void MapSettingsWidget::buildNavigationSection(QWidget *parent_widget)
{
    QGroupBox *group = new QGroupBox(QStringLiteral("Map Controls"), parent_widget);
    QVBoxLayout *group_layout = new QVBoxLayout(group);

    group_layout->addWidget(helpLabel(
        QStringLiteral("Adjust map mouse controls. Mouse wheel zoom applies to both 2D and 3D (globe). "
                       "Defaults: wheel zoom 0.50x, 3D pan 0.80x, 3D orbit 0.80x."),
        group));

    QFormLayout *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    const GuiMapNavigationConfiguration &current = guiConfiguration().map_navigation;

    this->scroll_zoom_sensitivity_control = new SliderNumberControl(group);
    this->scroll_zoom_sensitivity_control->setRange(
        NavigationSensitivityMinimum, NavigationSensitivityMaximum);
    this->scroll_zoom_sensitivity_control->setSingleStep(NavigationSensitivityStep);
    this->scroll_zoom_sensitivity_control->setDecimals(2);
    this->scroll_zoom_sensitivity_control->setSliderStep(0.01);
    this->scroll_zoom_sensitivity_control->setSuffix(QStringLiteral("x"));
    this->scroll_zoom_sensitivity_control->setValue(current.scroll_zoom_sensitivity);
    form->addRow(QStringLiteral("Mouse wheel zoom speed"),
                 this->scroll_zoom_sensitivity_control);

    this->mouse_3d_pan_sensitivity_control = new SliderNumberControl(group);
    this->mouse_3d_pan_sensitivity_control->setRange(
        NavigationSensitivityMinimum, NavigationSensitivityMaximum);
    this->mouse_3d_pan_sensitivity_control->setSingleStep(NavigationSensitivityStep);
    this->mouse_3d_pan_sensitivity_control->setDecimals(2);
    this->mouse_3d_pan_sensitivity_control->setSliderStep(0.01);
    this->mouse_3d_pan_sensitivity_control->setSuffix(QStringLiteral("x"));
    this->mouse_3d_pan_sensitivity_control->setValue(current.mouse_3d_pan_sensitivity);
    form->addRow(QStringLiteral("3D mouse pan speed"),
                 this->mouse_3d_pan_sensitivity_control);

    this->orbit_3d_sensitivity_control = new SliderNumberControl(group);
    this->orbit_3d_sensitivity_control->setRange(
        NavigationSensitivityMinimum, NavigationSensitivityMaximum);
    this->orbit_3d_sensitivity_control->setSingleStep(NavigationSensitivityStep);
    this->orbit_3d_sensitivity_control->setDecimals(2);
    this->orbit_3d_sensitivity_control->setSliderStep(0.01);
    this->orbit_3d_sensitivity_control->setSuffix(QStringLiteral("x"));
    this->orbit_3d_sensitivity_control->setValue(current.orbit_3d_sensitivity);
    form->addRow(QStringLiteral("3D orbit speed"),
                 this->orbit_3d_sensitivity_control);

    group_layout->addLayout(form);

    this->navigation_status = new QLabel(group);
    this->navigation_status->setWordWrap(true);
    group_layout->addWidget(this->navigation_status);

    QPushButton *restore_defaults = new QPushButton(QStringLiteral("Restore Defaults"), group);
    group_layout->addWidget(restore_defaults, 0, Qt::AlignLeft);

    this->navigation_save_debounce = new QTimer(this);
    this->navigation_save_debounce->setSingleShot(true);
    this->navigation_save_debounce->setInterval(SettingsSaveDebounceMs);
    connect(this->navigation_save_debounce, &QTimer::timeout,
            this, &MapSettingsWidget::saveNavigationSettingsNow);

    connect(this->scroll_zoom_sensitivity_control, &SliderNumberControl::valueChanged,
            this, [this](double) { scheduleNavigationSave(); });
    connect(this->mouse_3d_pan_sensitivity_control, &SliderNumberControl::valueChanged,
            this, [this](double) { scheduleNavigationSave(); });
    connect(this->orbit_3d_sensitivity_control, &SliderNumberControl::valueChanged,
            this, [this](double) { scheduleNavigationSave(); });

    connect(restore_defaults, &QPushButton::clicked, this, [this]
    {
        const GuiMapNavigationConfiguration defaults;
        this->scroll_zoom_sensitivity_control->setValue(defaults.scroll_zoom_sensitivity);
        this->mouse_3d_pan_sensitivity_control->setValue(defaults.mouse_3d_pan_sensitivity);
        this->orbit_3d_sensitivity_control->setValue(defaults.orbit_3d_sensitivity);
        saveNavigationSettingsNow();
    });

    static_cast<QVBoxLayout *>(parent_widget->layout())->addWidget(group);
}

void MapSettingsWidget::buildPerformanceSection(QWidget *parent_widget)
{
    QGroupBox *group = new QGroupBox(QStringLiteral("Map Performance"), parent_widget);
    QVBoxLayout *group_layout = new QVBoxLayout(group);

    group_layout->addWidget(helpLabel(
        QStringLiteral("These apply immediately to the 3D map renderer and are saved as you change them."),
        group));

    QFormLayout *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    const GuiMapPerformanceConfiguration &current = guiConfiguration().map_performance;

    this->terrain_detail_reduction_control = new SliderNumberControl(group);
    this->terrain_detail_reduction_control->setRange(
        0.0, double(MapGlobeTerrainMaximumDetailReduction));
    this->terrain_detail_reduction_control->setSingleStep(1.0);
    this->terrain_detail_reduction_control->setDecimals(0);
    this->terrain_detail_reduction_control->setSliderStep(1.0);
    this->terrain_detail_reduction_control->setValue(double(current.terrain_detail_reduction));
    form->addRow(QStringLiteral("Terrain detail reduction"), this->terrain_detail_reduction_control);
    form->addRow(QString(), helpLabel(
        QStringLiteral("0 draws every elevation sample the terrain source provides at the map's "
                       "level of detail: one terrain cell per sample spacing, about 4 px on "
                       "screen. Each step halves the terrain resolution (about 8, 16 and 32 px "
                       "per cell) and needs a quarter of the triangles. Zoomed in past the "
                       "source's own resolution, every setting shows the full source detail, "
                       "never more."),
        group));

    group_layout->addLayout(form);

    this->performance_status = new QLabel(group);
    this->performance_status->setWordWrap(true);
    group_layout->addWidget(this->performance_status);

    QPushButton *restore_defaults = new QPushButton(QStringLiteral("Restore Defaults"), group);
    group_layout->addWidget(restore_defaults, 0, Qt::AlignLeft);

    this->performance_save_debounce = new QTimer(this);
    this->performance_save_debounce->setSingleShot(true);
    this->performance_save_debounce->setInterval(SettingsSaveDebounceMs);
    connect(this->performance_save_debounce, &QTimer::timeout,
            this, &MapSettingsWidget::savePerformanceSettingsNow);

    connect(this->terrain_detail_reduction_control, &SliderNumberControl::valueChanged,
            this, [this](double) { schedulePerformanceSave(); });

    connect(restore_defaults, &QPushButton::clicked, this, [this]
    {
        const GuiMapPerformanceConfiguration defaults;
        this->terrain_detail_reduction_control->setValue(double(defaults.terrain_detail_reduction));
        savePerformanceSettingsNow();
    });

    static_cast<QVBoxLayout *>(parent_widget->layout())->addWidget(group);
}

void MapSettingsWidget::buildServerSection(QWidget *parent_widget)
{
    QGroupBox *group = new QGroupBox(QStringLiteral("Map Server Settings"), parent_widget);
    QVBoxLayout *group_layout = new QVBoxLayout(group);

#ifdef __EMSCRIPTEN__
    group_layout->addWidget(helpLabel(
        QStringLiteral("In this build, the map server connection is set by the hosting page and "
                       "cannot be changed here."),
        group));
#else
    group_layout->addWidget(helpLabel(
        QStringLiteral("Stored in %1. Changing these requires restarting the application to take "
                       "effect.").arg(guiConfigurationFilePath()),
        group));
#endif

    QFormLayout *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    const MapServerClientConfiguration &current = mapServerClientConfiguration();

    this->server_base_url_edit = new QLineEdit(current.base_url, group);
    this->server_base_url_edit->setPlaceholderText(QStringLiteral("http://aowis-server-map.localhost:80"));
    form->addRow(QStringLiteral("Base URL"), this->server_base_url_edit);

    this->server_api_key_edit = new QLineEdit(QString::fromUtf8(current.api_key), group);
    this->server_api_key_edit->setPlaceholderText(QStringLiteral("(none)"));
    form->addRow(QStringLiteral("API key"), this->server_api_key_edit);

    this->server_delete_api_key_edit = new QLineEdit(
        QString::fromUtf8(current.delete_api_key), group);
    this->server_delete_api_key_edit->setPlaceholderText(QStringLiteral("(none)"));
    form->addRow(QStringLiteral("Delete API key"), this->server_delete_api_key_edit);

    group_layout->addLayout(form);

    this->server_status = new QLabel(group);
    this->server_status->setWordWrap(true);
    group_layout->addWidget(this->server_status);

    this->server_save_button = new QPushButton(QStringLiteral("Save"), group);
    group_layout->addWidget(this->server_save_button, 0, Qt::AlignLeft);

    connect(this->server_save_button, &QPushButton::clicked,
            this, &MapSettingsWidget::saveServerSettings);

#ifdef __EMSCRIPTEN__
    this->server_base_url_edit->setEnabled(false);
    this->server_api_key_edit->setEnabled(false);
    this->server_delete_api_key_edit->setEnabled(false);
    this->server_save_button->setEnabled(false);
#endif

    static_cast<QVBoxLayout *>(parent_widget->layout())->addWidget(group);
}

void MapSettingsWidget::scheduleNavigationSave()
{
    GuiMapNavigationConfiguration configuration;
    configuration.scroll_zoom_sensitivity = this->scroll_zoom_sensitivity_control->value();
    configuration.mouse_3d_pan_sensitivity = this->mouse_3d_pan_sensitivity_control->value();
    configuration.orbit_3d_sensitivity = this->orbit_3d_sensitivity_control->value();
    applyGuiMapNavigationConfiguration(configuration);
    this->navigation_save_debounce->start();
}

void MapSettingsWidget::saveNavigationSettingsNow()
{
    GuiMapNavigationConfiguration configuration;
    configuration.scroll_zoom_sensitivity = this->scroll_zoom_sensitivity_control->value();
    configuration.mouse_3d_pan_sensitivity = this->mouse_3d_pan_sensitivity_control->value();
    configuration.orbit_3d_sensitivity = this->orbit_3d_sensitivity_control->value();

    if (saveGuiMapNavigationConfiguration(configuration))
        showNavigationStatus(QStringLiteral("Saved."), false);
    else
        showNavigationStatus(QStringLiteral("Failed to save map control settings."), true);
}

void MapSettingsWidget::schedulePerformanceSave()
{
    this->performance_save_debounce->start();
}

void MapSettingsWidget::savePerformanceSettingsNow()
{
    GuiMapPerformanceConfiguration configuration;
    configuration.terrain_detail_reduction =
        qRound(this->terrain_detail_reduction_control->value());

    if (saveGuiMapPerformanceConfiguration(configuration))
        showPerformanceStatus(QStringLiteral("Saved."), false);
    else
        showPerformanceStatus(QStringLiteral("Failed to save map performance settings."), true);
}

void MapSettingsWidget::saveServerSettings()
{
    MapServerClientConfiguration configuration;
    configuration.base_url = this->server_base_url_edit->text();
    configuration.api_key = this->server_api_key_edit->text().toUtf8();
    configuration.delete_api_key = this->server_delete_api_key_edit->text().toUtf8();

    if (saveMapServerClientConfiguration(configuration))
    {
        showServerStatus(
            QStringLiteral("Saved. Restart the application for the new map server "
                           "settings to take effect."),
            false);
    }
    else
    {
        showServerStatus(QStringLiteral("Failed to save map server settings."), true);
    }
}

void MapSettingsWidget::showNavigationStatus(const QString &message, bool error)
{
    this->navigation_status->setText(message);
    QPalette palette = this->navigation_status->palette();
    if (error)
    {
        QColor color = this->palette().color(QPalette::Text);
        color = color.lightness() > 128 ? QColor(255, 120, 120) : QColor(180, 20, 20);
        palette.setColor(QPalette::WindowText, color);
    }
    else
    {
        palette.setColor(QPalette::WindowText, this->palette().color(QPalette::Text));
    }
    this->navigation_status->setPalette(palette);
}

void MapSettingsWidget::showPerformanceStatus(const QString &message, bool error)
{
    this->performance_status->setText(message);
    QPalette palette = this->performance_status->palette();
    if (error)
    {
        QColor color = this->palette().color(QPalette::Text);
        color = color.lightness() > 128 ? QColor(255, 120, 120) : QColor(180, 20, 20);
        palette.setColor(QPalette::WindowText, color);
    }
    else
    {
        palette.setColor(QPalette::WindowText, this->palette().color(QPalette::Text));
    }
    this->performance_status->setPalette(palette);
}

void MapSettingsWidget::showServerStatus(const QString &message, bool error)
{
    this->server_status->setText(message);
    QPalette palette = this->server_status->palette();
    if (error)
    {
        QColor color = this->palette().color(QPalette::Text);
        color = color.lightness() > 128 ? QColor(255, 120, 120) : QColor(180, 20, 20);
        palette.setColor(QPalette::WindowText, color);
    }
    else
    {
        palette.setColor(QPalette::WindowText, this->palette().color(QPalette::Text));
    }
    this->server_status->setPalette(palette);
}
