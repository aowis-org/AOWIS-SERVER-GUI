#include "entity_inspector_dock.h"


EntityInspectorDock::EntityInspectorDock(QWidget *parent)
    : QDockWidget("Entity Inspector", parent),
    scroll(new QScrollArea(this))
{
    //setMinimumWidth(Sizes::SidebarRightWidthBase);
    //setMaximumWidth(Sizes::SidebarRightWidthBase);
    
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
    QWidget *old_widget = this->scroll->takeWidget();
    
    if (old_widget)
        old_widget->deleteLater();
    
    this->widget_current = nullptr;
}

void EntityInspectorDock::setInspector(QWidget *inspector)
{
    clearEntity();
    
    this->widget_current = inspector;
    this->scroll->setWidget(this->widget_current);
}

void EntityInspectorDock::showEntityTank()
{
    EntityInspectorTank *inspector = new EntityInspectorTank();
    setInspector(inspector);
}


