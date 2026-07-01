#include "entity_inspector_dock.h"


EntityInspectorDock::EntityInspectorDock(QWidget *parent)
    : QDockWidget("Entity Inspector", parent)
{
    //setMinimumWidth(Sizes::SidebarRightWidthBase);
    //setMaximumWidth(Sizes::SidebarRightWidthBase);
    
    this->scroll = new QScrollArea(this);
    this->scroll->setWidgetResizable(true);
    setWidget(this->scroll);
    
    //setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setAllowedAreas(Qt::RightDockWidgetArea);
    
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);
    
    showEntityTank();
}

void EntityInspectorDock::clearEntity()
{
    if (this->widget_current)
    {
        this->widget_current->deleteLater();
        this->widget_current = nullptr;
    }
}

void EntityInspectorDock::showEntityTank()
{
    clearEntity();
    
    this->widget_current = new EntityInspectorTank(this->scroll);
    this->scroll->setWidget(this->widget_current);
}


