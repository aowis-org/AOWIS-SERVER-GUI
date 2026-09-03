#ifndef TAB_PIPES_WIDGET_H
#define TAB_PIPES_WIDGET_H

#include "network/hydraulic_entity_table_widget.h"

class HydraulicData;

class PipesWidget : public HydraulicEntityTableWidget
{
    Q_OBJECT

public:
    explicit PipesWidget(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
};

#endif // TAB_PIPES_WIDGET_H
