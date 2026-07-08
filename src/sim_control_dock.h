#ifndef SIM_CONTROL_DOCK_H
#define SIM_CONTROL_DOCK_H

#include <QObject>
#include <QWidget>
#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QIcon>

#include <QHBoxLayout>

#include "widgets/combo_checkboxes.h"

#include "_enums_structs.h"
#include "_sizes.h"

class SimControlDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit SimControlDock(QWidget *parent = nullptr);
    
private:
    QWidget *content = nullptr;
    QHBoxLayout *layout = nullptr;
    
    void addChemicalQualityDropdown();
    
    void addHeadlossFormulaDropdown();
    ComboCheckboxes *combo_headloss_formula = nullptr;
    
signals:
    void signalHeadlossFormulaChanged(HeadlossFormulas formulas);
};

#endif // SIM_CONTROL_DOCK_H
