#ifndef ENTITY_INSPECTOR_WIDGET_H
#define ENTITY_INSPECTOR_WIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QString>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QComboBox>
#include <QDateEdit>
#include <QCheckBox>

#include "../widgets/group_box_collapsible.h"
#include "../_sizes.h"

class EntityInspectorWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit EntityInspectorWidget(QWidget *parent = nullptr);
    
protected:
    QVBoxLayout *mainLayout() const;
    
    void setTitle(const QString &title);
    void addGroupGeneral(const QString &icon_path, const QString &name);
    QLineEdit *line_name = nullptr;
    QDateEdit *date_install = nullptr;
    
    void addGroupPosition();
    QDoubleSpinBox *spin_latitude = nullptr;
    QDoubleSpinBox *spin_longitude = nullptr;
    
    void addGroupElevation();
    GroupBoxCollapsible *group_elevation = nullptr;
    QComboBox *combo_elevation_mode = nullptr;
    
    QLabel *label_terrain_elevation = nullptr;
    QPushButton *button_terrain_elevation = nullptr;
    QDoubleSpinBox *spin_terrain_elevation = nullptr;
    QLabel *label_tank_bottom_offset = nullptr;
    QDoubleSpinBox *spin_tank_bottom_offset = nullptr;
    QLabel *label_tank_bottom_elevation = nullptr;
    QDoubleSpinBox *spin_tank_bottom_elevation = nullptr;
    
private:
    QVBoxLayout *layout_main = nullptr;
    QLabel *label_title = nullptr;
    
    
    
private slots:
    void onGroupExpand(GroupBoxCollapsible *group);
    
    void onElevationModeSignalChanged(int index);
    void onElevationCalc();
};

#endif
