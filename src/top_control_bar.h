#pragma once

#include <QToolBar>

#include "_enums_structs.h"
#include "epanet2_enums.h"

class ComboCheckboxes;
class QToolButton;
class QWidget;

class TopControlBar : public QToolBar
{
    Q_OBJECT

public:
    explicit TopControlBar(QWidget *parent = nullptr);

    void setFullScreenState(bool fullscreen);
    void setSimulationResultsAvailable(bool available);
    void setEpanetLogAvailable(bool available);

signals:
    void signalHeadlossFormulaChanged(HeadlossFormulas formulas);
    void signalSimulationStart();
    void signalShowSimulationStatistics();
    void signalShowEpanetLog();
    void signalExportEpanetNetwork();
    void signalFullScreenToggle();

private:
    QWidget *content = nullptr;
    ComboCheckboxes *combo_headloss_formula = nullptr;
    QToolButton *button_fullscreen = nullptr;
    QToolButton *button_sim_statistics = nullptr;
    QToolButton *button_sim_log = nullptr;

    EN_FlowUnits selected_flow_units = EN_LPS;

    void addProjectControls();
    void addFlowUnitCombo();
    void addChemicalQualityDropdown();
    void addHeadlossFormulaDropdown();
    void addSimulationControls();
    void addViewControls();
};
