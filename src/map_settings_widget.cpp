#include "map_settings_widget.h"

#include "gui_configuration.h"
#include "map_server_client_configuration.h"
#include "slider_number_control.h"

#include <QCheckBox>
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
// Debounce disk writes while a slider is actively being dragged: the value
// is applied to the live renderer immediately either way (see
// savePerformanceSettingsNow(), called both on this timeout and, via
// schedulePerformanceSave(), on every change), only the persisted-to-disk
// copy is coalesced.
constexpr int PerformanceSaveDebounceMs = 400;

// Mirrors the bounds map_rhi_basemap_renderer.cpp enforces on this value
// (TerrainReliefMinimumZoom) and MapModel::MaxZoom.
constexpr int TerrainMinDetailZoomBound = 8;
constexpr int TerrainMaxDetailZoomBound = 19;

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

    buildPerformanceSection(content);
    buildServerSection(content);
    content_layout->addStretch(1);

    scroll_area->setWidget(content);

    QVBoxLayout *outer_layout = new QVBoxLayout(this);
    outer_layout->setContentsMargins(0, 0, 0, 0);
    outer_layout->addWidget(scroll_area);
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

    this->view_distance_control = new SliderNumberControl(group);
    this->view_distance_control->setRange(150.0, 50000.0);
    this->view_distance_control->setSingleStep(100.0);
    this->view_distance_control->setDecimals(0);
    this->view_distance_control->setSuffix(QStringLiteral(" m"));
    this->view_distance_control->setValue(current.max_view_distance_m);
    form->addRow(QStringLiteral("View distance"), this->view_distance_control);
    form->addRow(QString(), helpLabel(
        QStringLiteral("Absolute maximum distance the 3D camera can be pulled back to, "
                       "independent of the current zoom level."), group));

    this->terrain_lod_target_control = new SliderNumberControl(group);
    this->terrain_lod_target_control->setRange(8.0, 128.0);
    this->terrain_lod_target_control->setSingleStep(1.0);
    this->terrain_lod_target_control->setDecimals(0);
    this->terrain_lod_target_control->setSuffix(QStringLiteral(" px"));
    this->terrain_lod_target_control->setValue(current.terrain_lod_target_cell_size_px);
    form->addRow(QStringLiteral("Terrain detail target size"), this->terrain_lod_target_control);
    form->addRow(QString(), helpLabel(
        QStringLiteral("Target on-screen size of one terrain mesh cell. Smaller keeps the terrain "
                       "mesh denser (sharper) out to a greater distance, at a higher rendering cost."),
        group));

    this->terrain_max_detail_zoom_control = new SliderNumberControl(group);
    this->terrain_max_detail_zoom_control->setRange(
        double(TerrainMinDetailZoomBound), double(TerrainMaxDetailZoomBound));
    this->terrain_max_detail_zoom_control->setSingleStep(1.0);
    this->terrain_max_detail_zoom_control->setDecimals(0);
    this->terrain_max_detail_zoom_control->setValue(double(current.terrain_max_detail_zoom));
    form->addRow(QStringLiteral("Max terrain detail down to zoom"), this->terrain_max_detail_zoom_control);
    form->addRow(QString(), helpLabel(
        QStringLiteral("Highest zoom level at which the terrain elevation data is still used at "
                       "increasing resolution. Raise this to keep full terrain detail available "
                       "when zoomed in further."),
        group));

    this->terrain_full_detail_zoom_control = new SliderNumberControl(group);
    this->terrain_full_detail_zoom_control->setRange(
        double(TerrainMinDetailZoomBound), double(TerrainMaxDetailZoomBound));
    this->terrain_full_detail_zoom_control->setSingleStep(1.0);
    this->terrain_full_detail_zoom_control->setDecimals(0);
    this->terrain_full_detail_zoom_control->setValue(double(current.terrain_full_detail_zoom));
    form->addRow(QStringLiteral("Full detail at center down to zoom"),
                 this->terrain_full_detail_zoom_control);
    form->addRow(QString(), helpLabel(
        QStringLiteral("At or above this zoom level, the terrain tile right at the crosshair is "
                       "forced to maximum mesh detail regardless of camera distance -- instead of "
                       "the usual falloff, which otherwise reduces it there by one LOD step per "
                       "zoom level (e.g. zoom 19 -> 18 already looks less detailed by default). "
                       "Lower this to keep the center fully detailed across a wider zoom range; "
                       "everywhere else, and below this zoom level, still follows the normal "
                       "falloff."),
        group));

    this->array_batching_checkbox = new QCheckBox(
        QStringLiteral("Enable draw-call batching (texture array)"), group);
    this->array_batching_checkbox->setChecked(current.array_batching_enabled);
    form->addRow(QString(), this->array_batching_checkbox);
    form->addRow(QString(), helpLabel(
        QStringLiteral("Draws multiple already-loaded basemap tiles in a single GPU call instead of "
                       "one call per tile. Purely a performance optimization -- tiles still loading "
                       "always draw individually regardless of this setting, so turning it off never "
                       "changes what is shown, only how many draw calls it takes."),
        group));

    group_layout->addLayout(form);

    this->performance_status = new QLabel(group);
    this->performance_status->setWordWrap(true);
    group_layout->addWidget(this->performance_status);

    QPushButton *restore_defaults = new QPushButton(QStringLiteral("Restore Defaults"), group);
    group_layout->addWidget(restore_defaults, 0, Qt::AlignLeft);

    this->performance_save_debounce = new QTimer(this);
    this->performance_save_debounce->setSingleShot(true);
    this->performance_save_debounce->setInterval(PerformanceSaveDebounceMs);
    connect(this->performance_save_debounce, &QTimer::timeout,
            this, &MapSettingsWidget::savePerformanceSettingsNow);

    connect(this->view_distance_control, &SliderNumberControl::valueChanged,
            this, [this](double) { schedulePerformanceSave(); });
    connect(this->terrain_lod_target_control, &SliderNumberControl::valueChanged,
            this, [this](double) { schedulePerformanceSave(); });
    connect(this->terrain_max_detail_zoom_control, &SliderNumberControl::valueChanged,
            this, [this](double) { schedulePerformanceSave(); });
    connect(this->terrain_full_detail_zoom_control, &SliderNumberControl::valueChanged,
            this, [this](double) { schedulePerformanceSave(); });
    connect(this->array_batching_checkbox, &QCheckBox::toggled,
            this, [this](bool) { schedulePerformanceSave(); });

    connect(restore_defaults, &QPushButton::clicked, this, [this]
    {
        const GuiMapPerformanceConfiguration defaults;
        this->view_distance_control->setValue(defaults.max_view_distance_m);
        this->terrain_lod_target_control->setValue(defaults.terrain_lod_target_cell_size_px);
        this->terrain_max_detail_zoom_control->setValue(double(defaults.terrain_max_detail_zoom));
        this->terrain_full_detail_zoom_control->setValue(double(defaults.terrain_full_detail_zoom));
        this->array_batching_checkbox->setChecked(defaults.array_batching_enabled);
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

void MapSettingsWidget::schedulePerformanceSave()
{
    this->performance_save_debounce->start();
}

void MapSettingsWidget::savePerformanceSettingsNow()
{
    GuiMapPerformanceConfiguration configuration;
    configuration.max_view_distance_m = this->view_distance_control->value();
    configuration.terrain_lod_target_cell_size_px = this->terrain_lod_target_control->value();
    configuration.terrain_max_detail_zoom = int(this->terrain_max_detail_zoom_control->value());
    configuration.terrain_full_detail_zoom = int(this->terrain_full_detail_zoom_control->value());
    configuration.array_batching_enabled = this->array_batching_checkbox->isChecked();

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
