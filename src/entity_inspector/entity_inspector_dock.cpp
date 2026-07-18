#include "entity_inspector_dock.h"


EntityInspectorDock::EntityInspectorDock(HydraulicData *network_data, QWidget *parent)
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
    
    connect(this->hydraulic_data, &HydraulicData::signalTankSelected, this, [this]
    {
        qDebug() << "aaa";
        showEntityTank();
    });
    
    setVisible(false);
    
    //showEntityTank();
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
    qDebug() << "bbb";
    
    clearEntity();
    
    this->widget_current = inspector;
    setWidget(this->widget_current);
    
    this->widget_current->onHeadlossFormulaChanged(this->headloss_formulas_current);
    
    setVisible(true);
}

void EntityInspectorDock::showEntityTank()
{
    qDebug() << "ccc";
    EntityInspectorTank *inspector = new EntityInspectorTank(this->hydraulic_data);
    setInspector(inspector);
}
void EntityInspectorDock::showEntityJunction()
{
    EntityInspectorJunction *inspector = new EntityInspectorJunction(this->hydraulic_data);
    setInspector(inspector);
}
void EntityInspectorDock::showEntityPipe()
{
    EntityInspectorPipe *inspector = new EntityInspectorPipe(this->hydraulic_data);
    setInspector(inspector);
}
void EntityInspectorDock::showEntityPump()
{
    EntityInspectorPump *inspector = new EntityInspectorPump(this->hydraulic_data);
    setInspector(inspector);
}
void EntityInspectorDock::showEntityValve()
{
    EntityInspectorValve *inspector = new EntityInspectorValve(this->hydraulic_data);
    setInspector(inspector);
}
void EntityInspectorDock::showEntityReservoir()
{
    EntityInspectorReservoir *inspector = new EntityInspectorReservoir(this->hydraulic_data);
    setInspector(inspector);
}
void EntityInspectorDock::showEntityCustomerPoint()
{
    EntityInspectorCustomerPoint *inspector = new EntityInspectorCustomerPoint(this->hydraulic_data);
    setInspector(inspector);
}

void EntityInspectorDock::onHeadlossFormulaChanged(HeadlossFormulas formulas)
{
    this->headloss_formulas_current = formulas;
    
    if (this->widget_current != nullptr) {
        this->widget_current->onHeadlossFormulaChanged(formulas);
    }
}
