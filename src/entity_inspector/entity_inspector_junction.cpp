#include "entity_inspector_junction.h"

EntityInspectorJunction::EntityInspectorJunction(HydraulicData *hydraulic_data, const QUuid &uuid, QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent)
{
    addGroupOverviewImage(":/icon/junction.png", QString());
    addGroupGeneral(QString());
    addGroupPosition();
    bindHydraulicNode(InfrastructureEntity::Junction, uuid, "Junction");

    addGroupElevation();

    addGroupDemands();
    addGroupSimulation();
    addGroupQuality();
    addGroupAlerts();
    addStretches();
}

void EntityInspectorJunction::addGroupQuality()
{
    addGroupNodeQualityInputs();
}
