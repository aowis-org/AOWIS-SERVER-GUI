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

#include "entity_inspector_widget.h"

#include "../widgets/group_box_collapsible.h"

#include "../_enums_structs.h"
#include "../_sizes.h"
#include "../map/map_network_structs.h"

class EntityInspectorValve : public EntityInspectorWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorValve(QWidget *parent = nullptr);
private:
    QLabel *picture = nullptr;
    
    void addGroupValveConfiguration();
    QComboBox *combo_valve_type = nullptr;
    QLabel *label_setting = nullptr;
    QDoubleSpinBox *spin_setting = nullptr;
    QComboBox *combo_setting_curve = nullptr;
    
    QComboBox *combo_status_initial = nullptr;
    QDoubleSpinBox *spin_diameter = nullptr;
    QDoubleSpinBox *spin_loss_coeff = nullptr;
    void onValveTypeChanged(ValveType type);
    
    void addGroupSimMeas();
    
    
    void addGroupGraphs();

signals:
};

#endif // ENTITY_INSPECTOR_VALVE_H
