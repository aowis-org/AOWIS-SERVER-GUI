#ifndef TAB_JUNCTIONS_WIDGET_H
#define TAB_JUNCTIONS_WIDGET_H

#include "network/hydraulic_entity_table_widget.h"

class HydraulicData;

class JunctionsWidget : public HydraulicEntityTableWidget
{
    Q_OBJECT

public:
    explicit JunctionsWidget(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
};

#endif // TAB_JUNCTIONS_WIDGET_H
