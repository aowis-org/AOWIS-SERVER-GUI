#ifndef TAB_VALVES_WIDGET_H
#define TAB_VALVES_WIDGET_H

#include "hydraulic_entity_table_widget.h"

class HydraulicData;

class ValvesWidget : public HydraulicEntityTableWidget
{
    Q_OBJECT

public:
    explicit ValvesWidget(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
};

#endif // TAB_VALVES_WIDGET_H
