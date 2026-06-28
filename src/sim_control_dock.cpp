#include "sim_control_dock.h"

SimControlDock::SimControlDock(QWidget *parent)
    : QDockWidget{parent}
{
    setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);
}
