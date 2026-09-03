#ifndef TAB_PUMPS_WIDGET_H
#define TAB_PUMPS_WIDGET_H

#include "network/hydraulic_entity_table_widget.h"

class HydraulicData;

class PumpsWidget : public HydraulicEntityTableWidget
{
    Q_OBJECT

public:
    explicit PumpsWidget(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
};

#endif // TAB_PUMPS_WIDGET_H
