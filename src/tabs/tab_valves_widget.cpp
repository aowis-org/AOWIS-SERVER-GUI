#include "tabs/tab_valves_widget.h"

ValvesWidget::ValvesWidget(HydraulicData *hydraulic_data, QWidget *parent)
    : HydraulicEntityTableWidget(hydraulic_data, InfrastructureEntity::Valve,
                                 QStringLiteral("Valves"), parent)
{
}
