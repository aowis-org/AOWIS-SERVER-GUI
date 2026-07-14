#include "entity_inspector_dock.h"


EntityInspectorDock::EntityInspectorDock(QWidget *parent)
    : QDockWidget("Entity Inspector", parent)
{
    setMinimumWidth(Sizes::SidebarRightWidth);
    this->resize(Sizes::SidebarRightWidth, this->height());
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    
    //setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setAllowedAreas(Qt::RightDockWidgetArea);
    
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);
    
    showEntityTank();
    //showEntityJunction();
    //showEntityPipe();
    //showEntityPump();
    //showEntityValve();
    //showEntityReservoir();
    //showEntityCustomerPoint();
}

void EntityInspectorDock::clearEntity()
{
    if (this->widget_current)
        this->widget_current->deleteLater();
    
    this->widget_current = nullptr;
}

void EntityInspectorDock::setInspector(EntityInspectorWidget *inspector)
{
    clearEntity();
    
    this->widget_current = inspector;
    setWidget(this->widget_current);
    
    this->widget_current->onHeadlossFormulaChanged(this->headloss_formulas_current);
}

void EntityInspectorDock::showEntityTank()
{
    EntityInspectorTank *inspector = new EntityInspectorTank();
    setInspector(inspector);
}
void EntityInspectorDock::showEntityJunction()
{
    EntityInspectorJunction *inspector = new EntityInspectorJunction();
    setInspector(inspector);
}
void EntityInspectorDock::showEntityPipe()
{
    EntityInspectorPipe *inspector = new EntityInspectorPipe();
    setInspector(inspector);
}
void EntityInspectorDock::showEntityPump()
{
    EntityInspectorPump *inspector = new EntityInspectorPump();
    setInspector(inspector);
}
void EntityInspectorDock::showEntityValve()
{
    EntityInspectorValve *inspector = new EntityInspectorValve();
    setInspector(inspector);
}
void EntityInspectorDock::showEntityReservoir()
{
    EntityInspectorReservoir *inspector = new EntityInspectorReservoir();
    setInspector(inspector);
}
void EntityInspectorDock::showEntityCustomerPoint()
{
    EntityInspectorCustomerPoint *inspector = new EntityInspectorCustomerPoint();
    setInspector(inspector);
}

void EntityInspectorDock::onHeadlossFormulaChanged(HeadlossFormulas formulas)
{
    this->headloss_formulas_current = formulas;
    
    if (this->widget_current != nullptr) {
        this->widget_current->onHeadlossFormulaChanged(formulas);
    }
}
