#ifndef ENTITY_INSPECTOR_LOCATION_H
#define ENTITY_INSPECTOR_LOCATION_H

#include <QObject>

#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>

#include "../widgets/group_box_collapsible.h"

class EntityInspectorLocation : public QObject
{
    Q_OBJECT
public:
    explicit EntityInspectorLocation(QObject *parent = nullptr);
    
    void addGroupPosition(QVBoxLayout *layout);
    
    void addGroupElevation(QVBoxLayout *layout);
    
private:
    QDoubleSpinBox *spin_latitude = nullptr;
    QDoubleSpinBox *spin_longitude = nullptr;
    
    GroupBoxCollapsible *group_elevation = nullptr;
    QComboBox *combo_elevation_mode = nullptr;
    int elevation_mode_current = 0;
    
    QLabel *label_terrain_elevation = nullptr;
    QPushButton *button_terrain_elevation = nullptr;
    QDoubleSpinBox *spin_terrain_elevation = nullptr;
    QLabel *label_tank_bottom_offset = nullptr;
    QDoubleSpinBox *spin_tank_bottom_offset = nullptr;
    QDoubleSpinBox *spin_tank_bottom_elevation = nullptr;
    
private slots:
    void onGroupCollapse(GroupBoxCollapsible *group);
    void onGroupExpand(GroupBoxCollapsible *group);
    
    void onElevationModeSignalChanged(int index);
    void onElevationCalc();
    
signals:
    
    
};

#endif // ENTITY_INSPECTOR_LOCATION_H
