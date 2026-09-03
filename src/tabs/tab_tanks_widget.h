#ifndef TAB_TANKS_WIDGET_H
#define TAB_TANKS_WIDGET_H

#include "network/hydraulic_entity_table_widget.h"

class HydraulicData;

class TanksWidget : public HydraulicEntityTableWidget
{
    Q_OBJECT

public:
    explicit TanksWidget(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
};

#endif // TAB_TANKS_WIDGET_H
