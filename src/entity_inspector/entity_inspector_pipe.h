#ifndef ENTITY_INSPECTOR_PIPE_H
#define ENTITY_INSPECTOR_PIPE_H

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
#include <QCheckBox>

#include "entity_inspector_widget.h"

#include "../widgets/group_box_collapsible.h"

#include "../_enums_structs.h"
#include "../_sizes.h"
#include "../map/map_network_structs.h"

class EntityInspectorPipe : public EntityInspectorWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorPipe(QWidget *parent = nullptr);
    
private:
    QLabel *picture = nullptr;
    
    void addGroupGeometry();
    QComboBox *combo_status_initial = nullptr;
    QDoubleSpinBox *spin_diameter = nullptr;
    QDoubleSpinBox *spin_length = nullptr;
    
    void addGroupRoughness();
    QComboBox *combo_material = nullptr;
    QDoubleSpinBox *spin_roughness_hw = nullptr;
    QDoubleSpinBox *spin_roughness_dw = nullptr;
    QDoubleSpinBox *spin_roughness_cm = nullptr;
    
    QDoubleSpinBox *spin_loss_coefficient = nullptr;
    
    void addGroupQuality();
    QCheckBox *check_override = nullptr;
    QDoubleSpinBox *spin_bulk_reaction = nullptr;
    QDoubleSpinBox *spin_wall_reaction = nullptr;
    
    void addGroupSimMeas();
    
    
    void addGroupGraphs();
    
signals:
    
protected slots:
    void onHeadlossFormulaChanged(HeadlossFormulas formulas) override;
};

#endif // ENTITY_INSPECTOR_PIPE_H
