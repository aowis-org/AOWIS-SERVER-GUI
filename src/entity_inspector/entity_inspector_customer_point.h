#ifndef ENTITY_INSPECTOR_CUSTOMER_POINT_H
#define ENTITY_INSPECTOR_CUSTOMER_POINT_H

#include <QObject>
#include <QWidget>

#include <QVBoxLayout>
#include <QGridLayout>

#include <QPixmap>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>

#include "entity_inspector_widget.h"

#include "../widgets/group_box_collapsible.h"

#include "../_enums_structs.h"
#include "../_sizes.h"
#include "../map/map_models.h"

class EntityInspectorCustomerPoint : public EntityInspectorWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorCustomerPoint(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
    
private:
    QLabel *picture = nullptr;
    
    void addGroupConnections();
    QComboBox *combo_connection_pipe = nullptr;
    QComboBox *combo_connection_junction = nullptr;
    
    
    void addGroupQuality();
    
    
    void addGroupSimMeas();
    
    
    void addGroupGraphs();
    
signals:
};

#endif // ENTITY_INSPECTOR_CUSTOMER_POINT_H
