#ifndef ENTITY_INSPECTOR_RESERVOIR_H
#define ENTITY_INSPECTOR_RESERVOIR_H

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

class EntityInspectorReservoir : public EntityInspectorWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorReservoir(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
    
private:
    HydraulicData *hydraulic_data = nullptr;
    QLabel *picture = nullptr;
    
    void addGroupDemands();
    
    
    void addGroupQuality();
    
    
signals:
};

#endif // ENTITY_INSPECTOR_RESERVOIR_H
