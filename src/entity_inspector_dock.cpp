#include "entity_inspector_dock.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>

EntityInspectorDock::EntityInspectorDock(QWidget *parent)
    : QDockWidget("Entity Inspector", parent)
{
    setMinimumWidth(200);
        
    // Create the inner widget that holds your UI
    QScrollArea *scroll = new QScrollArea(this);
    QVBoxLayout *layout = new QVBoxLayout(this);
    
    // Example content
    layout->addWidget(new QLabel("Map inspector content goes here"));
    layout->addStretch();
    
    // Put the content widget inside the dock
    setWidget(scroll);
    
    // Optional: restrict docking areas
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    // Optional: control features
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);
    
    QPushButton *test = new QPushButton("Test");
    layout->addWidget(test);
    
    showEntityTank();
}

void EntityInspectorDock::showEntityTank()
{
    
}


