#include "entity_inspector/entity_inspector_reservoir.h"

EntityInspectorReservoir::EntityInspectorReservoir(HydraulicData *hydraulic_data, const QUuid &uuid, QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent)
{
    addGroupOverviewImage(":/icon/lake.png", QString());
    addGroupGeneral(QString());
    addGroupPosition();
    bindHydraulicNode(InfrastructureEntity::Reservoir, uuid, "Reservoir");
    addGroupElevation();

    addGroupSimulation();
    addGroupWaterQualitySimulation();
    addGroupQuality();
    addGroupAlerts();
    addGroupHistory();
    addStretches();
}

void EntityInspectorReservoir::addGroupQuality()
{
    addGroupNodeQualityInputs();
}
