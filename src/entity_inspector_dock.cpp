#include "entity_inspector_dock.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>

EntityInspectorDock::EntityInspectorDock(QWidget *parent)
    : QDockWidget("Entity Inspector", parent)
{
    setMinimumWidth(200);
        
    // Create the inner widget that holds your UI
    QWidget *content = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(content);
    
    // Example content
    layout->addWidget(new QLabel("Map inspector content goes here"));
    layout->addStretch();
    
    // Put the content widget inside the dock
    setWidget(content);
    
    // Optional: restrict docking areas
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    // Optional: control features
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);
    
    QPushButton *test = new QPushButton("Test");
    layout->addWidget(test);
}

void EntityInspectorDock::showEntityTank()
{
    
}
    


