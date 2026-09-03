#include "tabs/tab_pipes_widget.h"

PipesWidget::PipesWidget(HydraulicData *hydraulic_data, QWidget *parent)
    : HydraulicEntityTableWidget(hydraulic_data, InfrastructureEntity::Pipe,
                                 QStringLiteral("Pipes"), parent)
{
}
