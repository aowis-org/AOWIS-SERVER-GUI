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

#include "../widgets/group_box_collapsible.h"

#include "../_enums_structs.h"
#include "../_sizes.h"
#include "../map_network_structs.h"

class EntityInspectorTank : public QWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorTank(QWidget *parent = nullptr);
    
private:
    QVBoxLayout *layout = nullptr;
    QLabel *label = nullptr;
    
    QLabel *picture = nullptr;
    
    void addGroupGeneral();
    QLabel *label_title = nullptr;
    QLineEdit *line_name = nullptr;
    
    void addGroupPosition();
    QDoubleSpinBox *spin_latitude = nullptr;
    QDoubleSpinBox *spin_longitude = nullptr;
    
    void addGroupElevation();
    QComboBox *combo_elevation_mode = nullptr;
    
    QLabel *label_terrain_elevation = nullptr;
    QDoubleSpinBox *spin_terrain_elevation = nullptr;
    QLabel *label_tank_bottom_offset = nullptr;
    QDoubleSpinBox *spin_tank_bottom_offset = nullptr;
    QDoubleSpinBox *spin_tank_bottom_elevation = nullptr;
    
    void addGroupGeometry();
    QCheckBox *check_overflow = nullptr;
    QDoubleSpinBox *spin_level_initial = nullptr;
    
    QComboBox *combo_geometry_type = nullptr;
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
    
    void addGroupSimMeas();
    
    
    void addGroupGraphs();
    
    
private slots:
    void elevationCalc();
    
signals:
};

#endif // ENTITY_INSPECTOR_TANK_H
