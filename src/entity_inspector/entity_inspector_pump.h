#ifndef ENTITY_INSPECTOR_PUMP_H
#define ENTITY_INSPECTOR_PUMP_H

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

#include <aowis/model/hydraulic/network_hydraulic.h>
#include "../_enums_structs.h"
#include "../_sizes.h"
#include "../map/map_models.h"

class EntityInspectorPump : public EntityInspectorWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorPump(HydraulicData *hydraulic_data, HydraulicLinkPump pump, QWidget *parent = nullptr);
    
private:
    HydraulicLinkPump pump;
    QLabel *picture = nullptr;
    
    void addGroupControls();
    QComboBox *combo_type = nullptr;
    QDoubleSpinBox *spin_speed_initial = nullptr;
    QComboBox *combo_status_initial = nullptr;
    QComboBox *combo_speed_pattern = nullptr;
    QComboBox *combo_controls = nullptr;
    
    void addGroupEnergyCostInput();
    QComboBox *combo_efficiency_curve = nullptr;
    QDoubleSpinBox *spin_energy_price = nullptr;
    QComboBox *combo_price_pattern = nullptr;
    
    void addGroupEnergy();
    
signals:
};

#endif // ENTITY_INSPECTOR_PUMP_H
