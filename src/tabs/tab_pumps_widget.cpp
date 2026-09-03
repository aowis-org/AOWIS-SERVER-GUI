#include "tabs/tab_pumps_widget.h"

PumpsWidget::PumpsWidget(HydraulicData *hydraulic_data, QWidget *parent)
    : HydraulicEntityTableWidget(hydraulic_data, InfrastructureEntity::Pump,
                                 QStringLiteral("Pumps"), parent)
{
}
