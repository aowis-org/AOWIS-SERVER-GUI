#ifndef TAB_DEMAND_POINTS_WIDGET_H
#define TAB_DEMAND_POINTS_WIDGET_H

#include "network/hydraulic_entity_table_widget.h"

class HydraulicData;

class DemandPointsWidget : public HydraulicEntityTableWidget
{
    Q_OBJECT

public:
    explicit DemandPointsWidget(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
};

#endif // TAB_DEMAND_POINTS_WIDGET_H
