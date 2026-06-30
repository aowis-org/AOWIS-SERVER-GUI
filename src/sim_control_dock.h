#ifndef SIM_CONTROL_DOCK_H
#define SIM_CONTROL_DOCK_H

#include <QObject>
#include <QWidget>
#include <QDockWidget>

#include <QHBoxLayout>

#include "widgets/combo_checkboxes.h"

class SimControlDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit SimControlDock(QWidget *parent = nullptr);
    
private:
    QHBoxLayout *layout = nullptr;
    
signals:
};

#endif // SIM_CONTROL_DOCK_H
