#include "tabs/tab_demand_points_widget.h"

DemandPointsWidget::DemandPointsWidget(HydraulicData *hydraulic_data, QWidget *parent)
    : HydraulicEntityTableWidget(hydraulic_data, InfrastructureEntity::DemandPoint,
                                 QStringLiteral("Demand Points"), parent)
{
}
