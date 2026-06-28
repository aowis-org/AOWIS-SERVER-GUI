#ifndef SIM_CONTROL_DOCK_H
#define SIM_CONTROL_DOCK_H

#include <QObject>
#include <QWidget>
#include <QDockWidget>

class SimControlDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit SimControlDock(QWidget *parent = nullptr);

signals:
};

#endif // SIM_CONTROL_DOCK_H
