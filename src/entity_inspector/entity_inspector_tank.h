#ifndef ENTITY_INSPECTOR_TANK_H
#define ENTITY_INSPECTOR_TANK_H

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
//#include "../../external/AOWIS-SERVER-EPANET/src/lib/model/simulation_request.h"
#include <aowis/model/hydraulic/network.h>

class EntityInspectorTank : public EntityInspectorWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorTank(HydraulicData *hydraulic_data, Tank tank, QWidget *parent = nullptr);
    
private:
    Tank tank;
    QLabel *picture = nullptr;
    
    void addGroupGeometry();
    GroupBoxCollapsible *group_geometry = nullptr;
    
    QCheckBox *check_overflow = nullptr;
    QDoubleSpinBox *spin_level_initial = nullptr;
    
    QComboBox *combo_geometry_type = nullptr;
    TankGeometryInputType tankGeometryInputType();
    int geometry_type_current = 0;
    
    QLabel *label_spin_diameter = nullptr;
    QDoubleSpinBox *spin_diameter = nullptr;
    QLabel *label_spin_area = nullptr;
    QDoubleSpinBox *spin_area = nullptr;
    
    QLabel *label_spin_level_min = nullptr;
    QDoubleSpinBox *spin_level_min = nullptr;
    QLabel *label_spin_level_max = nullptr;
    QDoubleSpinBox *spin_level_max = nullptr;
    QLabel *label_spin_volume_min = nullptr;
    QDoubleSpinBox *spin_volume_min = nullptr;
    
    QLabel *label_volume_max = nullptr;
    QLabel *label_volume_max_label = nullptr;
    
    QLabel *label_volume_curve = nullptr;
    QComboBox *combo_volume_curve = nullptr;
    
    void addGroupQuality();
    QComboBox *combo_chem_source = nullptr;
    QComboBox *combo_chem_mixing = nullptr;
    
    
private slots:
    void onGroupCollapse(GroupBoxCollapsible *group);
    void onGroupExpand(GroupBoxCollapsible *group);
    
    void onComboGeometryTypeChange(int index);
    
signals:
};

#endif // ENTITY_INSPECTOR_TANK_H
