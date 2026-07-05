#ifndef SIM_CONTROL_DOCK_H
#define SIM_CONTROL_DOCK_H

#include <QObject>
#include <QWidget>
#include <QDockWidget>
#include <QLabel>
#include <QPushButton>

#include <QHBoxLayout>

#include "widgets/combo_checkboxes.h"

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
    
signals:
};

#endif // SIM_CONTROL_DOCK_H
