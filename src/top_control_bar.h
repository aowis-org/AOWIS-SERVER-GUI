#pragma once

#include <QToolBar>

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

    void setFullScreenState(bool fullscreen);
    void setSimulationResultsAvailable(bool available);
    void resetSimulationRunIcon();
    void setSimulationRunRunningIcon();
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
    void signalFullScreenToggle();

private:
    QWidget *content = nullptr;
    ComboCheckboxes *combo_headloss_formula = nullptr;
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
    void addFlowUnitCombo();
    void addChemicalQualityDropdown();
    void addHeadlossFormulaDropdown();
    void addSimulationControls();
    void addViewControls();
    void updateSimulationNavigationState();
    void setSimulationPlaybackActive(bool active);
    void requestSimulationResultIndex(int result_index);
};
