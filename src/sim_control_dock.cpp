#include "sim_control_dock.h"

SimControlDock::SimControlDock(QWidget *parent)
    : QDockWidget{parent},
    layout( new QHBoxLayout(this) )
{
    setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);
    
    setLayout(this->layout);
    
    ComboCheckboxes *combo = new ComboCheckboxes(this);
    
    combo->addItem("a", QVariant("a"), false);
    combo->addItem("b", QVariant("b"), true);
    combo->addItem("c", QVariant("c"), false);
    
    this->layout->addWidget(combo);
}
