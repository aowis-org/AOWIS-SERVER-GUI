#include "tabs/tab_reservoirs_widget.h"

ReservoirsWidget::ReservoirsWidget(HydraulicData *hydraulic_data, QWidget *parent)
    : HydraulicEntityTableWidget(hydraulic_data, InfrastructureEntity::Reservoir,
                                 QStringLiteral("Reservoirs"), parent)
{
}
