#ifndef ENTITY_INSPECTOR_VALVE_H
#define ENTITY_INSPECTOR_VALVE_H

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
#include <QUuid>

#include "entity_inspector_widget.h"

#include "../widgets/group_box_collapsible.h"

#include <aowis/model/hydraulic/network_hydraulic.h>
#include "../_enums_structs.h"
#include "../_sizes.h"
#include "../map/map_models.h"

class EntityInspectorValve : public EntityInspectorWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorValve(HydraulicData *hydraulic_data, const HydraulicLinkValve &valve, QWidget *parent = nullptr);

private:
    void addGroupValveConfiguration();
    void bindValve();
    void refreshValve();
    void populateSettingCurveCombo(const QUuid &curve_uuid);
    void onValveTypeChanged(HydraulicLinkValveType type);

    HydraulicData *hydraulic_data = nullptr;
    QUuid valve_uuid;

    QComboBox *combo_valve_type = nullptr;
    QLabel *label_setting = nullptr;
    QDoubleSpinBox *spin_setting = nullptr;
    QComboBox *combo_setting_curve = nullptr;
    
    QComboBox *combo_status_initial = nullptr;
    QDoubleSpinBox *spin_diameter = nullptr;
    QDoubleSpinBox *spin_loss_coeff = nullptr;
};

#endif // ENTITY_INSPECTOR_VALVE_H
