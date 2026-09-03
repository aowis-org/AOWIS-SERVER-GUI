#ifndef TAB_RESERVOIRS_WIDGET_H
#define TAB_RESERVOIRS_WIDGET_H

#include "network/hydraulic_entity_table_widget.h"

class HydraulicData;

class ReservoirsWidget : public HydraulicEntityTableWidget
{
    Q_OBJECT

public:
    explicit ReservoirsWidget(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
};

#endif // TAB_RESERVOIRS_WIDGET_H
