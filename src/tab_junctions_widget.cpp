#include "tab_junctions_widget.h"

JunctionsWidget::JunctionsWidget(HydraulicData *hydraulic_data, QWidget *parent)
    : HydraulicEntityTableWidget(hydraulic_data, InfrastructureEntity::Junction,
                                 QStringLiteral("Junctions"), parent)
{
}
