#pragma once

#include <QList>
#include <QString>
#include <QToolBar>

#include <aowis/model/hydraulic/hydraulic_types.h>

#include "_enums_structs.h"
#include "epanet2_enums.h"

struct HydraulicSimulationResultTimeline;

class ComboCheckboxes;
class QComboBox;
class QPushButton;
class QTimer;
class QToolButton;
class QWidget;

class TopControlBar : public QToolBar
{
    Q_OBJECT

public:
    explicit TopControlBar(QWidget *parent = nullptr);

    HydraulicHeadlossFormula selectedSimulationHeadlossFormula() const;
    QList<WaterQualityAnalysisType> selectedSimulationQualityAnalyses() const;
    void setSelectedSimulationHeadlossFormula(HydraulicHeadlossFormula formula);
    void setSelectedSimulationQualityAnalyses(const QList<WaterQualityAnalysisType> &analyses);

    void setFullScreenState(bool fullscreen);
    void setSimulationResultsAvailable(bool available);
    void resetSimulationRunIcon();
    void setSimulationRunRunningIcon();
    void setSimulationRunStoppingIcon();
    void setSimulationRunCancelledIcon();
    void setSimulationRunResultIcon(const HydraulicSimulationResultTimeline &result_timeline);
    void setSimulationResultTimeline(const HydraulicSimulationResultTimeline &result_timeline);
    void clearSimulationResultTimeline();
    void setCurrentSimulationResultIndex(int result_index);
    void setEpanetLogAvailable(bool available);

signals:
    void signalHeadlossFormulaChanged(HeadlossFormulas formulas);
    void signalSimulationStart();
    void signalShowSimulationStatistics();
    void signalSimulationResultIndexSelected(int result_index);
    void signalShowEpanetLog();
    void signalExportEpanetNetwork();
    void signalImportProject();
    void signalBuiltinRevisionActivationRequested(
        const QString &resource_path,
        const QString &file_name);
    void signalShowNetworkOnMap();
    void signalFullScreenToggle();

private:
    QWidget *content = nullptr;
    ComboCheckboxes *combo_quality_analysis = nullptr;
    QComboBox *combo_project = nullptr;
    QComboBox *combo_revision = nullptr;
    QComboBox *combo_headloss_formula = nullptr;
    QToolButton *button_fullscreen = nullptr;
    QPushButton *button_sim_start = nullptr;
    QToolButton *button_sim_statistics = nullptr;
    QToolButton *button_sim_log = nullptr;
    QToolButton *button_sim_step_previous = nullptr;
    QToolButton *button_sim_step_next = nullptr;
    QToolButton *button_sim_playback = nullptr;
    QComboBox *combo_sim_timepoint = nullptr;
    QComboBox *combo_sim_speed = nullptr;
    QTimer *simulation_playback_timer = nullptr;
    int simulation_result_count = 0;

    EN_FlowUnits selected_flow_units = EN_LPS;

    void addProjectControls();
    void updateRevisionControls();
    void addFlowUnitCombo();
    void addQualityHeadlossControls();
    void addSimulationControls();
    void addViewControls();
    void updateSimulationNavigationState();
    void setSimulationPlaybackActive(bool active);
    void requestSimulationResultIndex(int result_index);
};
