#include "top_control_bar.h"

#include "_sizes.h"
#include "widgets/combo_checkboxes.h"

#include <aowis/model/hydraulic/hydraulic_simulation_results.h>

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLayout>
#include <QLabel>
#include <QMenu>
#include <QPalette>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
class TopControlBarContent : public QWidget
{
public:
    explicit TopControlBarContent(QWidget *parent = nullptr)
        : QWidget(parent),
        left_group(new QWidget(this)),
        center_group(new QWidget(this)),
        right_group(new QWidget(this)),
        left_layout(new QHBoxLayout(this->left_group)),
        center_layout(new QHBoxLayout(this->center_group)),
        right_layout(new QHBoxLayout(this->right_group))
    {
        this->left_layout->setContentsMargins(0, 0, 0, 0);
        this->left_layout->setSpacing(10);

        this->center_layout->setContentsMargins(0, 0, 0, 0);
        this->center_layout->setSpacing(10);

        this->right_layout->setContentsMargins(0, 0, 0, 0);
        this->right_layout->setSpacing(10);

        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(Sizes::TopControlBarHeight);
    }

    QHBoxLayout *leftLayout() const
    {
        return this->left_layout;
    }

    QHBoxLayout *centerLayout() const
    {
        return this->center_layout;
    }

    QHBoxLayout *rightLayout() const
    {
        return this->right_layout;
    }

    QSize sizeHint() const override
    {
        const int width = this->left_group->sizeHint().width()
                          + this->center_group->sizeHint().width()
                          + this->right_group->sizeHint().width()
                          + 64;
        return QSize(width, Sizes::TopControlBarHeight);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);

        constexpr int side_margin = 10;
        constexpr int group_spacing = 14;

        const QSize left_size = this->left_group->sizeHint();
        const QSize center_size = this->center_group->sizeHint();
        const QSize right_size = this->right_group->sizeHint();

        const int left_y = qMax(0, (height() - left_size.height()) / 2);
        const int center_y = qMax(0, (height() - center_size.height()) / 2);
        const int right_y = qMax(0, (height() - right_size.height()) / 2);

        const int left_x = side_margin;
        const int right_x = width() - side_margin - right_size.width();

        const int centered_x = (width() - center_size.width()) / 2;
        const int minimum_center_x = left_x + left_size.width() + group_spacing;
        const int maximum_center_x = right_x - group_spacing - center_size.width();

        int center_x = centered_x;
        if (minimum_center_x <= maximum_center_x)
            center_x = qBound(minimum_center_x, centered_x, maximum_center_x);
        else
            center_x = minimum_center_x;

        this->left_group->setGeometry(left_x, left_y, left_size.width(), left_size.height());
        this->center_group->setGeometry(center_x, center_y, center_size.width(), center_size.height());
        this->right_group->setGeometry(right_x, right_y, right_size.width(), right_size.height());
    }

private:
    QWidget *left_group = nullptr;
    QWidget *center_group = nullptr;
    QWidget *right_group = nullptr;

    QHBoxLayout *left_layout = nullptr;
    QHBoxLayout *center_layout = nullptr;
    QHBoxLayout *right_layout = nullptr;
};

QLabel *createCaption(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    QFont font = label->font();

    if (font.pointSizeF() > 0.0)
        font.setPointSizeF(qMax(8.0, font.pointSizeF() - 2.0));
    else
        font.setPixelSize(10);

    label->setFont(font);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);

    return label;
}

QWidget *createLabeledControl(const QString &caption, QWidget *control, QWidget *parent)
{
    QWidget *container = new QWidget(parent);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);
    layout->addWidget(createCaption(caption, container));
    layout->addWidget(control);
    return container;
}

QFrame *createSeparator(QWidget *parent)
{
    QFrame *separator = new QFrame(parent);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    return separator;
}

TopControlBarContent *barContent(QWidget *content)
{
    return static_cast<TopControlBarContent *>(content);
}

QSize toolbarIconButtonSize()
{
    const int extent = Sizes::TopControlBarHeight - 8;
    return QSize(extent, extent);
}

QSize toolbarIconSize()
{
    const int extent = Sizes::TopControlBarHeight - 10;
    return QSize(extent, extent);
}

void configureToolbarIconButton(QAbstractButton *button)
{
    button->setFixedSize(toolbarIconButtonSize());
    button->setIconSize(toolbarIconSize());
    button->setStyleSheet(QStringLiteral("padding: 0;"));
}

void configureStackedToolbarIconButton(QAbstractButton *button)
{
    const int button_extent = toolbarIconButtonSize().width() / 2;
    const int icon_extent = qMax(1, button_extent - 2);
    button->setFixedSize(button_extent, button_extent);
    button->setIconSize(QSize(icon_extent, icon_extent));
    button->setStyleSheet(QStringLiteral("padding: 0;"));
}

QString formatSimulationElapsedTime(quint64 elapsed_s)
{
    const quint64 total_minutes = elapsed_s / 60;
    const quint64 hours = total_minutes / 60;
    const quint64 minutes = total_minutes % 60;
    return QStringLiteral("%1:%2").arg(hours, 2, 10, QLatin1Char('0')).arg(minutes, 2, 10, QLatin1Char('0'));
}
}

TopControlBar::TopControlBar(QWidget *parent)
    : QToolBar(parent),
    content(new TopControlBarContent(this))
{
    setObjectName(QStringLiteral("top_control_bar"));
    setAllowedAreas(Qt::TopToolBarArea);
    setMovable(false);
    setFloatable(false);
    setContextMenuPolicy(Qt::PreventContextMenu);
    setIconSize(toolbarIconSize());
    setFixedHeight(Sizes::TopControlBarHeight);
    setContentsMargins(0, 0, 0, 0);

    if (layout() != nullptr)
        layout()->setContentsMargins(0, 0, 0, 0);

    this->content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addWidget(this->content);

    addProjectControls();
    addFlowUnitCombo();
    addQualityHeadlossControls();
    addSimulationControls();
    addViewControls();
}

HydraulicHeadlossFormula TopControlBar::selectedSimulationHeadlossFormula() const
{
    if (this->combo_headloss_formula == nullptr || this->combo_headloss_formula->currentIndex() < 0)
        return HydraulicHeadlossFormula::HazenWilliams;

    const HeadlossFormula formula = static_cast<HeadlossFormula>(
        this->combo_headloss_formula->currentData().toInt());

    switch (formula)
    {
    case HeadlossFormula::HazenWilliams:
        return HydraulicHeadlossFormula::HazenWilliams;
    case HeadlossFormula::DarcyWeisbach:
        return HydraulicHeadlossFormula::DarcyWeisbach;
    case HeadlossFormula::ChezyManning:
        return HydraulicHeadlossFormula::ChezyManning;
    case HeadlossFormula::None:
        break;
    }

    return HydraulicHeadlossFormula::HazenWilliams;
}

QList<WaterQualityAnalysisType> TopControlBar::selectedSimulationQualityAnalyses() const
{
    QList<WaterQualityAnalysisType> analyses;
    if (this->combo_quality_analysis == nullptr)
        return analyses;

    const QList<int> indexes_checked = this->combo_quality_analysis->checkedIndexes();
    for (const int index : indexes_checked)
    {
        analyses.append(static_cast<WaterQualityAnalysisType>(
            this->combo_quality_analysis->itemData(index).toInt()));
    }

    return analyses;
}

void TopControlBar::setSelectedSimulationHeadlossFormula(HydraulicHeadlossFormula formula)
{
    if (this->combo_headloss_formula == nullptr)
        return;

    HeadlossFormula gui_formula = HeadlossFormula::HazenWilliams;
    switch (formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
        gui_formula = HeadlossFormula::HazenWilliams;
        break;
    case HydraulicHeadlossFormula::DarcyWeisbach:
        gui_formula = HeadlossFormula::DarcyWeisbach;
        break;
    case HydraulicHeadlossFormula::ChezyManning:
        gui_formula = HeadlossFormula::ChezyManning;
        break;
    }

    const int index = this->combo_headloss_formula->findData(static_cast<int>(gui_formula));
    if (index >= 0)
        this->combo_headloss_formula->setCurrentIndex(index);
}

void TopControlBar::setSelectedSimulationQualityAnalyses(
    const QList<WaterQualityAnalysisType> &analyses)
{
    if (this->combo_quality_analysis == nullptr)
        return;

    for (int index = 0; index < this->combo_quality_analysis->count(); ++index)
    {
        const WaterQualityAnalysisType analysis = static_cast<WaterQualityAnalysisType>(
            this->combo_quality_analysis->itemData(index).toInt());
        this->combo_quality_analysis->setItemChecked(index, analyses.contains(analysis));
    }
}

void TopControlBar::setFullScreenState(bool fullscreen)
{
    if (this->button_fullscreen == nullptr)
        return;

    const QString icon_path = fullscreen ? QStringLiteral(":/icon/fullscreen_undo.png") : QStringLiteral(":/icon/fullscreen.png");
    this->button_fullscreen->setIcon(QIcon(icon_path));
    this->button_fullscreen->setToolTip(fullscreen ? QStringLiteral("Leave fullscreen [F11]") : QStringLiteral("Enter fullscreen [F11]"));
}

void TopControlBar::setSimulationResultsAvailable(bool available)
{
    if (this->button_sim_statistics == nullptr)
        return;

    this->button_sim_statistics->setEnabled(available);
    this->button_sim_statistics->setToolTip(
        available
            ? QStringLiteral("Show simulation statistics")
            : QStringLiteral("Show simulation statistics<br>You need to run a simulation first"));
}

void TopControlBar::resetSimulationRunIcon()
{
    if (this->button_sim_start == nullptr)
        return;

    this->button_sim_start->setIcon(QIcon(QStringLiteral(":/icon/simulation_start.png")));
}

void TopControlBar::setSimulationRunRunningIcon()
{
    if (this->button_sim_start == nullptr)
        return;

    this->button_sim_start->setIcon(QIcon(QStringLiteral(":/icon/simulation_stop.png")));
    this->button_sim_start->setToolTip(
        QStringLiteral("Stop Simulation<br>Simulation running...<br>[Ctrl]+[R]<br>[Shift]+[Enter]"));
}

void TopControlBar::setSimulationRunStoppingIcon()
{
    if (this->button_sim_start == nullptr)
        return;

    this->button_sim_start->setIcon(QIcon(QStringLiteral(":/icon/simulation_stop.png")));
    this->button_sim_start->setToolTip(
        QStringLiteral("Stopping Simulation...<br>The current EPANET solver step will finish before cancellation."));
}

void TopControlBar::setSimulationRunCancelledIcon()
{
    if (this->button_sim_start == nullptr)
        return;

    this->button_sim_start->setIcon(QIcon(QStringLiteral(":/icon/simulation_start.png")));
    this->button_sim_start->setToolTip(
        QStringLiteral("Run Configured Simulations<br>[Ctrl]+[R]<br>[Shift]+[Enter]<br><br>Last run: Cancelled"));
}

void TopControlBar::setSimulationRunResultIcon(const HydraulicSimulationResultTimeline &result_timeline)
{
    if (this->button_sim_start == nullptr)
        return;

    int warning_count = 0;
    int error_count = 0;

    for (const HydraulicSimulationDiagnostic &diagnostic : result_timeline.diagnostics)
    {
        if (diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Warning)
            ++warning_count;
        else if (diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Error
                 || diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Fatal)
            ++error_count;
    }

    QString icon_path;
    QString last_run_status;

    if (!result_timeline.status.success)
    {
        icon_path = QStringLiteral(":/icon/simulation_error.png");
        last_run_status = QStringLiteral("Failed");
    }
    else if (error_count > 0)
    {
        icon_path = QStringLiteral(":/icon/simulation_error.png");
        last_run_status = QStringLiteral("Completed with %1 error%2")
            .arg(error_count)
            .arg(error_count == 1 ? QString() : QStringLiteral("s"));
    }
    else if (result_timeline.validity != HydraulicSimulationResultValidity::Valid)
    {
        icon_path = QStringLiteral(":/icon/simulation_error.png");
        last_run_status = QStringLiteral("Invalid result");
    }
    else if (warning_count > 0)
    {
        icon_path = QStringLiteral(":/icon/simulation_warning.png");
        last_run_status = QStringLiteral("Successful with %1 warning%2")
            .arg(warning_count)
            .arg(warning_count == 1 ? QString() : QStringLiteral("s"));
    }
    else
    {
        icon_path = QStringLiteral(":/icon/simulation_success.png");
        last_run_status = QStringLiteral("Successful");
    }

    this->button_sim_start->setIcon(QIcon(icon_path));
    this->button_sim_start->setToolTip(
        QStringLiteral("Run Configured Simulations<br>[Ctrl]+[R]<br>[Shift]+[Enter]<br><br>Last run: %1")
            .arg(last_run_status));
}

void TopControlBar::setSimulationResultTimeline(const HydraulicSimulationResultTimeline &result_timeline)
{
    if (this->combo_sim_timepoint == nullptr)
        return;

    setSimulationPlaybackActive(false);

    const QSignalBlocker blocker(this->combo_sim_timepoint);
    this->combo_sim_timepoint->clear();
    this->simulation_result_count = result_timeline.results.size();

    for (const HydraulicSimulationResult &result : result_timeline.results)
    {
        const QString elapsed_time = formatSimulationElapsedTime(result.time_elapsed_s);
        this->combo_sim_timepoint->addItem(elapsed_time);

        if (result_timeline.simulation_start_utc.isValid())
        {
            const QDateTime timestamp = result_timeline.simulation_start_utc.addSecs(static_cast<qint64>(result.time_elapsed_s));
            this->combo_sim_timepoint->setItemData(
                this->combo_sim_timepoint->count() - 1,
                timestamp.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'")),
                Qt::ToolTipRole);
        }
    }

    this->combo_sim_timepoint->setCurrentIndex(this->simulation_result_count > 0 ? 0 : -1);
    updateSimulationNavigationState();
}

void TopControlBar::clearSimulationResultTimeline()
{
    setSimulationPlaybackActive(false);
    this->simulation_result_count = 0;

    if (this->combo_sim_timepoint != nullptr)
    {
        const QSignalBlocker blocker(this->combo_sim_timepoint);
        this->combo_sim_timepoint->clear();
    }

    updateSimulationNavigationState();
}

void TopControlBar::setCurrentSimulationResultIndex(int result_index)
{
    if (this->combo_sim_timepoint == nullptr)
        return;

    if (result_index < 0 || result_index >= this->simulation_result_count)
    {
        const QSignalBlocker blocker(this->combo_sim_timepoint);
        this->combo_sim_timepoint->setCurrentIndex(-1);
        updateSimulationNavigationState();
        return;
    }

    if (this->combo_sim_timepoint->currentIndex() != result_index)
    {
        const QSignalBlocker blocker(this->combo_sim_timepoint);
        this->combo_sim_timepoint->setCurrentIndex(result_index);
    }

    if (this->simulation_playback_timer != nullptr
        && this->simulation_playback_timer->isActive()
        && result_index >= this->simulation_result_count - 1)
    {
        setSimulationPlaybackActive(false);
    }

    updateSimulationNavigationState();
}

void TopControlBar::updateSimulationNavigationState()
{
    const bool has_results = this->simulation_result_count > 0;
    const int current_index = this->combo_sim_timepoint != nullptr ? this->combo_sim_timepoint->currentIndex() : -1;

    if (this->combo_sim_timepoint != nullptr)
        this->combo_sim_timepoint->setEnabled(has_results);

    if (this->button_sim_step_previous != nullptr)
        this->button_sim_step_previous->setEnabled(has_results && current_index > 0);

    if (this->button_sim_step_next != nullptr)
        this->button_sim_step_next->setEnabled(has_results && current_index >= 0 && current_index < this->simulation_result_count - 1);

    const bool can_play = this->simulation_result_count > 1;
    if (this->button_sim_playback != nullptr)
        this->button_sim_playback->setEnabled(can_play);

    if (this->combo_sim_speed != nullptr)
        this->combo_sim_speed->setEnabled(can_play);
}

void TopControlBar::setSimulationPlaybackActive(bool active)
{
    if (this->simulation_playback_timer == nullptr || this->button_sim_playback == nullptr)
        return;

    if (active && this->simulation_result_count > 1)
        this->simulation_playback_timer->start();
    else
        this->simulation_playback_timer->stop();

    const bool playing = this->simulation_playback_timer->isActive();
    this->button_sim_playback->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    this->button_sim_playback->setToolTip(playing ? QStringLiteral("Pause simulation timeline") : QStringLiteral("Play simulation timeline"));
}

void TopControlBar::requestSimulationResultIndex(int result_index)
{
    if (result_index < 0 || result_index >= this->simulation_result_count)
        return;

    emit signalSimulationResultIndexSelected(result_index);
}

void TopControlBar::setEpanetLogAvailable(bool available)
{
    if (this->button_sim_log == nullptr)
        return;

    this->button_sim_log->setEnabled(available);
    this->button_sim_log->setToolTip(
        available
            ? QStringLiteral("Show EPANET log")
            : QStringLiteral("Show EPANET log<br>You need to run a simulation first"));
}

void TopControlBar::addProjectControls()
{
    TopControlBarContent *bar_content = barContent(this->content);

    QWidget *project_revision_container = new QWidget(this->content);
    QVBoxLayout *project_revision_layout = new QVBoxLayout(project_revision_container);
    project_revision_layout->setContentsMargins(0, 0, 0, 0);
    project_revision_layout->setSpacing(2);

    QComboBox *combo_project = new QComboBox(project_revision_container);
    combo_project->setFixedSize(178, 30);
    combo_project->addItem(QStringLiteral("Select project"));

    QComboBox *combo_revision = new QComboBox(project_revision_container);
    combo_revision->setFixedSize(178, 30);
    combo_revision->addItem(QStringLiteral("Select revision"));

    QToolButton *button_import_project = new QToolButton(project_revision_container);
    button_import_project->setAutoRaise(true);
    button_import_project->setIcon(QIcon(QStringLiteral(":/icon/download_export.png")));
    button_import_project->setIconSize(QSize(28, 28));
    button_import_project->setFixedSize(30, 30);
    button_import_project->setToolTip(QStringLiteral("Import project"));
    button_import_project->setStyleSheet(QStringLiteral("padding: 0;"));
    connect(button_import_project, &QToolButton::clicked, this, &TopControlBar::signalImportProject);

    QToolButton *button_show_on_map = new QToolButton(project_revision_container);
    button_show_on_map->setAutoRaise(true);
    button_show_on_map->setIcon(QIcon(QStringLiteral(":/icon/target.png")));
    button_show_on_map->setIconSize(QSize(28, 28));
    button_show_on_map->setFixedSize(30, 30);
    button_show_on_map->setToolTip(QStringLiteral("Show on map"));
    button_show_on_map->setStyleSheet(QStringLiteral("padding: 0;"));
    connect(button_show_on_map, &QToolButton::clicked, this, &TopControlBar::signalShowNetworkOnMap);

    QToolButton *button_set_active = new QToolButton(project_revision_container);
    button_set_active->setAutoRaise(true);
    button_set_active->setIcon(QIcon(QStringLiteral(":/icon/set_active.png")));
    button_set_active->setIconSize(QSize(28, 28));
    button_set_active->setFixedSize(30, 30);
    button_set_active->setToolTip(QStringLiteral("Set active revision"));
    button_set_active->setStyleSheet(QStringLiteral("padding: 0;"));

    QLabel *project_caption = createCaption(QStringLiteral("Project"), project_revision_container);
    QLabel *revision_caption = createCaption(QStringLiteral("Revision"), project_revision_container);
    const int caption_width = qMax(project_caption->sizeHint().width(), revision_caption->sizeHint().width());
    project_caption->setFixedWidth(caption_width);
    revision_caption->setFixedWidth(caption_width);

    QHBoxLayout *project_layout = new QHBoxLayout();
    project_layout->setContentsMargins(0, 0, 0, 0);
    project_layout->setSpacing(6);
    project_layout->addWidget(project_caption);
    project_layout->addWidget(combo_project);
    project_layout->addWidget(button_import_project);
    project_layout->addWidget(button_show_on_map);
    project_layout->addStretch(1);

    QHBoxLayout *revision_layout = new QHBoxLayout();
    revision_layout->setContentsMargins(0, 0, 0, 0);
    revision_layout->setSpacing(6);
    revision_layout->addWidget(revision_caption);
    revision_layout->addWidget(combo_revision);
    revision_layout->addWidget(button_set_active);
    revision_layout->addStretch(1);

    project_revision_layout->addLayout(project_layout);
    project_revision_layout->addLayout(revision_layout);
    bar_content->leftLayout()->addWidget(project_revision_container);
}

void TopControlBar::addFlowUnitCombo()
{
    TopControlBarContent *bar_content = barContent(this->content);
    bar_content->centerLayout()->addWidget(createSeparator(this->content));

    QToolButton *button_flow_units = new QToolButton(this->content);
    button_flow_units->setText(QStringLiteral("CMH"));
    button_flow_units->setFixedSize(82, 30);
    button_flow_units->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button_flow_units->setPopupMode(QToolButton::InstantPopup);

    QMenu *menu_flow_units = new QMenu(button_flow_units);
    button_flow_units->setMenu(menu_flow_units);

    QAction *action_cmh = menu_flow_units->addAction(QStringLiteral("CMH — cubic meters per hour"));
    action_cmh->setData(static_cast<int>(EN_CMH));

    QAction *action_lps = menu_flow_units->addAction(QStringLiteral("LPS — liters per second"));
    action_lps->setData(static_cast<int>(EN_LPS));

    menu_flow_units->addSeparator();

    QMenu *menu_other_metric = menu_flow_units->addMenu(QStringLiteral("Other metric"));

    QAction *action_lpm = menu_other_metric->addAction(QStringLiteral("LPM — liters per minute"));
    action_lpm->setData(static_cast<int>(EN_LPM));

    QAction *action_mld = menu_other_metric->addAction(QStringLiteral("MLD — million liters per day"));
    action_mld->setData(static_cast<int>(EN_MLD));

    QAction *action_cmd = menu_other_metric->addAction(QStringLiteral("CMD — cubic meters per day"));
    action_cmd->setData(static_cast<int>(EN_CMD));

    QAction *action_cms = menu_other_metric->addAction(QStringLiteral("CMS — cubic meters per second"));
    action_cms->setData(static_cast<int>(EN_CMS));

    QMenu *menu_imperial = menu_flow_units->addMenu(QStringLiteral("Imperial / US"));

    QAction *action_cfs = menu_imperial->addAction(QStringLiteral("CFS — cubic feet per second"));
    action_cfs->setData(static_cast<int>(EN_CFS));

    QAction *action_gpm = menu_imperial->addAction(QStringLiteral("GPM — gallons per minute"));
    action_gpm->setData(static_cast<int>(EN_GPM));

    QAction *action_mgd = menu_imperial->addAction(QStringLiteral("MGD — million gallons per day"));
    action_mgd->setData(static_cast<int>(EN_MGD));

    QAction *action_imgd = menu_imperial->addAction(QStringLiteral("IMGD — imperial million gallons per day"));
    action_imgd->setData(static_cast<int>(EN_IMGD));

    QAction *action_afd = menu_imperial->addAction(QStringLiteral("AFD — acre-feet per day"));
    action_afd->setData(static_cast<int>(EN_AFD));

    connect(menu_flow_units, &QMenu::triggered, this, [this, button_flow_units](QAction *action)
    {
        if (action == nullptr || action->menu() != nullptr)
            return;

        bool ok = false;
        const int value = action->data().toInt(&ok);

        if (!ok)
            return;

        this->selected_flow_units = static_cast<EN_FlowUnits>(value);
        button_flow_units->setText(action->text().section(' ', 0, 0));
    });

    this->selected_flow_units = EN_CMH;
    bar_content->centerLayout()->addWidget(createLabeledControl(QStringLiteral("Flow units"), button_flow_units, this->content));
}

void TopControlBar::addQualityHeadlossControls()
{
    TopControlBarContent *bar_content = barContent(this->content);

    this->combo_quality_analysis = new ComboCheckboxes(this->content);
    this->combo_quality_analysis->setFixedSize(190, 30);
    this->combo_quality_analysis->setSummaryLimit(2);

    this->combo_quality_analysis->addItem(
        QStringLiteral("CHEMICAL"),
        static_cast<int>(WaterQualityAnalysisType::Chemical),
        true,
        QStringLiteral("One dissolved constituent concentration, e.g. chlorine"));

    this->combo_quality_analysis->addItem(
        QStringLiteral("AGE"),
        static_cast<int>(WaterQualityAnalysisType::WaterAge),
        true,
        QStringLiteral("Water age"));

    this->combo_quality_analysis->addItem(
        QStringLiteral("TRACE"),
        static_cast<int>(WaterQualityAnalysisType::SourceTrace),
        true,
        QStringLiteral("Source tracing: percent of water originating from one node"));

    this->combo_headloss_formula = new QComboBox(this->content);
    this->combo_headloss_formula->setFixedSize(190, 30);

    this->combo_headloss_formula->addItem(
        QStringLiteral("Hazen-Williams"),
        static_cast<int>(HeadlossFormula::HazenWilliams));
    this->combo_headloss_formula->setItemData(
        this->combo_headloss_formula->count() - 1,
        QStringLiteral("Run simulation with the Hazen-Williams headloss formula.<br><br>Requires pipe roughness coefficient C."),
        Qt::ToolTipRole);

    this->combo_headloss_formula->addItem(
        QStringLiteral("Darcy-Weisbach"),
        static_cast<int>(HeadlossFormula::DarcyWeisbach));
    this->combo_headloss_formula->setItemData(
        this->combo_headloss_formula->count() - 1,
        QStringLiteral("Run simulation with the Darcy-Weisbach headloss formula.<br><br>Requires absolute pipe roughness ε in mm."),
        Qt::ToolTipRole);

    this->combo_headloss_formula->addItem(
        QStringLiteral("Chezy-Manning"),
        static_cast<int>(HeadlossFormula::ChezyManning));
    this->combo_headloss_formula->setItemData(
        this->combo_headloss_formula->count() - 1,
        QStringLiteral("Run simulation with the Chezy-Manning headloss formula.<br><br>Requires Manning roughness coefficient n."),
        Qt::ToolTipRole);

    connect(this->combo_headloss_formula, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        HeadlossFormulas formulas = HeadlossFormula::None;
        if (index >= 0)
        {
            const HeadlossFormula formula = static_cast<HeadlossFormula>(
                this->combo_headloss_formula->itemData(index).toInt());
            formulas |= formula;
        }

        emit signalHeadlossFormulaChanged(formulas);
    });

    QWidget *quality_headloss_container = new QWidget(this->content);
    QVBoxLayout *quality_headloss_layout = new QVBoxLayout(quality_headloss_container);
    quality_headloss_layout->setContentsMargins(0, 0, 0, 0);
    quality_headloss_layout->setSpacing(2);

    QLabel *quality_caption = createCaption(QStringLiteral("Water quality"), quality_headloss_container);
    QLabel *headloss_caption = createCaption(QStringLiteral("Headloss"), quality_headloss_container);
    const int caption_width = qMax(quality_caption->sizeHint().width(), headloss_caption->sizeHint().width());
    quality_caption->setFixedWidth(caption_width);
    headloss_caption->setFixedWidth(caption_width);

    QHBoxLayout *quality_layout = new QHBoxLayout();
    quality_layout->setContentsMargins(0, 0, 0, 0);
    quality_layout->setSpacing(6);
    quality_layout->addWidget(quality_caption);
    quality_layout->addWidget(this->combo_quality_analysis);

    QHBoxLayout *headloss_layout = new QHBoxLayout();
    headloss_layout->setContentsMargins(0, 0, 0, 0);
    headloss_layout->setSpacing(6);
    headloss_layout->addWidget(headloss_caption);
    headloss_layout->addWidget(this->combo_headloss_formula);

    quality_headloss_layout->addLayout(quality_layout);
    quality_headloss_layout->addLayout(headloss_layout);
    bar_content->centerLayout()->addWidget(quality_headloss_container);
}

void TopControlBar::addSimulationControls()
{
    TopControlBarContent *bar_content = barContent(this->content);

    this->button_sim_start = new QPushButton(this->content);
    resetSimulationRunIcon();
    this->button_sim_start->setFlat(true);
    configureToolbarIconButton(this->button_sim_start);
    this->button_sim_start->setToolTip(QStringLiteral("Run Configured Simulations<br>[Ctrl]+[R]<br>[Shift]+[Enter]"));
    this->button_sim_start->addAction(QString(), QKeySequence(Qt::SHIFT | Qt::Key_Return), this->button_sim_start, &QPushButton::click);
    this->button_sim_start->addAction(QString(), QKeySequence(Qt::CTRL | Qt::Key_R), this->button_sim_start, &QPushButton::click);

    QWidget *result_button_stack = new QWidget(this->content);
    QVBoxLayout *result_button_stack_layout = new QVBoxLayout(result_button_stack);
    result_button_stack_layout->setContentsMargins(0, 0, 0, 0);
    result_button_stack_layout->setSpacing(0);

    this->button_sim_statistics = new QToolButton(result_button_stack);
    this->button_sim_statistics->setAutoRaise(true);
    this->button_sim_statistics->setIcon(QIcon(QStringLiteral(":/icon/dashboard.png")));
    configureStackedToolbarIconButton(this->button_sim_statistics);

    this->button_sim_log = new QToolButton(result_button_stack);
    this->button_sim_log->setAutoRaise(true);
    this->button_sim_log->setIcon(QIcon(QStringLiteral(":/icon/log.png")));
    configureStackedToolbarIconButton(this->button_sim_log);

    const int control_height = toolbarIconButtonSize().height();
    const int half_height = control_height / 2;
    const int timeline_width = 104;

    QWidget *timeline_navigation = new QWidget(this->content);
    timeline_navigation->setFixedSize(timeline_width, control_height);
    QVBoxLayout *timeline_navigation_layout = new QVBoxLayout(timeline_navigation);
    timeline_navigation_layout->setContentsMargins(0, 0, 0, 0);
    timeline_navigation_layout->setSpacing(0);

    this->combo_sim_timepoint = new QComboBox(timeline_navigation);
    this->combo_sim_timepoint->setFixedSize(timeline_width, half_height);
    this->combo_sim_timepoint->setToolTip(QStringLiteral("Select simulation time point"));

    QWidget *step_buttons = new QWidget(timeline_navigation);
    step_buttons->setFixedSize(timeline_width, half_height);
    QHBoxLayout *step_buttons_layout = new QHBoxLayout(step_buttons);
    step_buttons_layout->setContentsMargins(0, 0, 0, 0);
    step_buttons_layout->setSpacing(0);

    this->button_sim_step_previous = new QToolButton(step_buttons);
    this->button_sim_step_previous->setAutoRaise(true);
    this->button_sim_step_previous->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    this->button_sim_step_previous->setFixedSize(timeline_width / 2, half_height);
    this->button_sim_step_previous->setIconSize(QSize(qMax(1, half_height - 4), qMax(1, half_height - 4)));
    this->button_sim_step_previous->setToolTip(QStringLiteral("Previous simulation time point"));

    this->button_sim_step_next = new QToolButton(step_buttons);
    this->button_sim_step_next->setAutoRaise(true);
    this->button_sim_step_next->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
    this->button_sim_step_next->setFixedSize(timeline_width - timeline_width / 2, half_height);
    this->button_sim_step_next->setIconSize(QSize(qMax(1, half_height - 4), qMax(1, half_height - 4)));
    this->button_sim_step_next->setToolTip(QStringLiteral("Next simulation time point"));

    step_buttons_layout->addWidget(this->button_sim_step_previous);
    step_buttons_layout->addWidget(this->button_sim_step_next);
    timeline_navigation_layout->addWidget(this->combo_sim_timepoint);
    timeline_navigation_layout->addWidget(step_buttons);

    QWidget *playback_stack = new QWidget(this->content);
    playback_stack->setFixedSize(54, control_height);
    QVBoxLayout *playback_stack_layout = new QVBoxLayout(playback_stack);
    playback_stack_layout->setContentsMargins(0, 0, 0, 0);
    playback_stack_layout->setSpacing(0);

    this->button_sim_playback = new QToolButton(playback_stack);
    this->button_sim_playback->setAutoRaise(true);
    this->button_sim_playback->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    this->button_sim_playback->setFixedSize(54, half_height);
    this->button_sim_playback->setIconSize(QSize(qMax(1, half_height - 4), qMax(1, half_height - 4)));
    this->button_sim_playback->setToolTip(QStringLiteral("Play simulation timeline"));

    this->combo_sim_speed = new QComboBox(playback_stack);
    this->combo_sim_speed->setFixedSize(54, half_height);
    this->combo_sim_speed->setToolTip(QStringLiteral("Simulation playback speed"));
    this->combo_sim_speed->addItem(QStringLiteral("¼×"), 0.25);
    this->combo_sim_speed->addItem(QStringLiteral("½×"), 0.5);
    this->combo_sim_speed->addItem(QStringLiteral("1×"), 1.0);
    this->combo_sim_speed->addItem(QStringLiteral("2×"), 2.0);
    this->combo_sim_speed->addItem(QStringLiteral("4×"), 4.0);
    this->combo_sim_speed->setCurrentIndex(2);

    playback_stack_layout->addWidget(this->button_sim_playback);
    playback_stack_layout->addWidget(this->combo_sim_speed);

    this->simulation_playback_timer = new QTimer(this);
    this->simulation_playback_timer->setInterval(1000);

    setSimulationResultsAvailable(false);
    setEpanetLogAvailable(false);
    clearSimulationResultTimeline();

    connect(this->button_sim_start, &QPushButton::clicked, this, [this]
    {
        emit signalSimulationStart();
    });

    connect(this->button_sim_statistics, &QToolButton::clicked, this, [this]
    {
        emit signalShowSimulationStatistics();
    });

    connect(this->button_sim_log, &QToolButton::clicked, this, [this]
    {
        emit signalShowEpanetLog();
    });

    connect(this->combo_sim_timepoint, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int result_index)
    {
        if (result_index >= 0)
            requestSimulationResultIndex(result_index);
    });

    connect(this->button_sim_step_previous, &QToolButton::clicked, this, [this]
    {
        requestSimulationResultIndex(this->combo_sim_timepoint->currentIndex() - 1);
    });

    connect(this->button_sim_step_next, &QToolButton::clicked, this, [this]
    {
        requestSimulationResultIndex(this->combo_sim_timepoint->currentIndex() + 1);
    });

    connect(this->button_sim_playback, &QToolButton::clicked, this, [this]
    {
        if (this->simulation_playback_timer->isActive())
        {
            setSimulationPlaybackActive(false);
            return;
        }

        if (this->simulation_result_count <= 1)
            return;

        if (this->combo_sim_timepoint->currentIndex() >= this->simulation_result_count - 1)
            requestSimulationResultIndex(0);

        setSimulationPlaybackActive(true);
    });

    connect(this->combo_sim_speed, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
    {
        const double speed = this->combo_sim_speed->currentData().toDouble();
        const int interval_ms = qMax(1, qRound(1000.0 / qMax(0.01, speed)));
        this->simulation_playback_timer->setInterval(interval_ms);
    });

    connect(this->simulation_playback_timer, &QTimer::timeout, this, [this]
    {
        const int current_index = this->combo_sim_timepoint->currentIndex();
        if (current_index < 0 || current_index >= this->simulation_result_count - 1)
        {
            setSimulationPlaybackActive(false);
            return;
        }

        requestSimulationResultIndex(current_index + 1);
    });

    result_button_stack_layout->addWidget(this->button_sim_statistics);
    result_button_stack_layout->addWidget(this->button_sim_log);

    bar_content->centerLayout()->addWidget(this->button_sim_start);
    bar_content->centerLayout()->addWidget(result_button_stack);
    bar_content->centerLayout()->addWidget(timeline_navigation);
    bar_content->centerLayout()->addWidget(playback_stack);
}

void TopControlBar::addViewControls()
{
    TopControlBarContent *bar_content = barContent(this->content);
    bar_content->rightLayout()->addWidget(createSeparator(this->content));

    QWidget *button_stack = new QWidget(this->content);
    QVBoxLayout *button_stack_layout = new QVBoxLayout(button_stack);
    button_stack_layout->setContentsMargins(0, 0, 0, 0);
    button_stack_layout->setSpacing(0);

    this->button_fullscreen = new QToolButton(button_stack);
    this->button_fullscreen->setAutoRaise(true);
    configureStackedToolbarIconButton(this->button_fullscreen);

    connect(this->button_fullscreen, &QToolButton::clicked, this, [this]
    {
        emit signalFullScreenToggle();
    });

    QToolButton *button_export_epanet = new QToolButton(button_stack);
    button_export_epanet->setAutoRaise(true);
    button_export_epanet->setIcon(QIcon(QStringLiteral(":/icon/save.png")));
    button_export_epanet->setToolTip(QStringLiteral("Export EPANET network"));
    configureStackedToolbarIconButton(button_export_epanet);

    connect(button_export_epanet, &QToolButton::clicked, this, [this]
    {
        emit signalExportEpanetNetwork();
    });

    setFullScreenState(false);
    button_stack_layout->addWidget(this->button_fullscreen);
    button_stack_layout->addWidget(button_export_epanet);
    bar_content->rightLayout()->addWidget(button_stack);
}
