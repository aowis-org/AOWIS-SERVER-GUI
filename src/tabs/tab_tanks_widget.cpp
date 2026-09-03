#include "tabs/tab_tanks_widget.h"

TanksWidget::TanksWidget(HydraulicData *hydraulic_data, QWidget *parent)
    : HydraulicEntityTableWidget(hydraulic_data, InfrastructureEntity::Tank,
                                 QStringLiteral("Tanks"), parent)
{
}
