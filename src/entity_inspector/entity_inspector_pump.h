#ifndef ENTITY_INSPECTOR_PUMP_H
#define ENTITY_INSPECTOR_PUMP_H

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QUuid>
#include <QWidget>

#include "entity_inspector_widget.h"

#include "../widgets/group_box_collapsible.h"

#include <aowis/model/hydraulic/network_hydraulic.h>
class EntityInspectorPump : public EntityInspectorWidget
{
    Q_OBJECT

public:
    explicit EntityInspectorPump(HydraulicData *hydraulic_data, const HydraulicLinkPump &pump, QWidget *parent = nullptr);

private:
    void addGroupControls();
    void addGroupEnergyCostInput();
    void addGroupEnergy();
    void bindPump();
    void refreshPump();
    void populateSpeedPatternCombo(const QUuid &pattern_uuid);
    void populateEfficiencyInputCombo(const HydraulicLinkPump &pump);
    void populatePricePatternCombo(const HydraulicLinkPump &pump);

    HydraulicData *hydraulic_data = nullptr;
    QUuid pump_uuid;

    QComboBox *combo_type = nullptr;
    QLabel *label_constant_power = nullptr;
    QDoubleSpinBox *spin_constant_power = nullptr;
    QDoubleSpinBox *spin_speed_initial = nullptr;
    QComboBox *combo_status_initial = nullptr;
    QComboBox *combo_speed_pattern = nullptr;
    QComboBox *combo_controls = nullptr;

    QComboBox *combo_efficiency_curve = nullptr;
    QDoubleSpinBox *spin_energy_price = nullptr;
    QComboBox *combo_price_pattern = nullptr;
};

#endif // ENTITY_INSPECTOR_PUMP_H
