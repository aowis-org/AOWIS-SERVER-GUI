#ifndef ENTITY_INSPECTOR_WIDGET_H
#define ENTITY_INSPECTOR_WIDGET_H

#include <QWidget>
#include <QTabWidget>
#include <QScrollArea>
#include <QDialog>
#include <QPointer>
#include <QIcon>

#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QString>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QComboBox>
#include <QDateEdit>
#include <QCheckBox>

#include <QTableWidget>
#include <QHeaderView>

#include "../widgets/group_box_collapsible.h"
#include "../_enums_structs.h"
#include "../_sizes.h"

class EntityInspectorWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit EntityInspectorWidget(QWidget *parent = nullptr);
    
protected:
    //QVBoxLayout *mainLayout() const;
    QVBoxLayout *layoutOverview();
    QVBoxLayout *layoutConfiguration();
    QVBoxLayout *layoutSimMeas();
    QVBoxLayout *layoutHistory();
    
    void setTitle(const QString &title);
    
    void addGroupOverviewImage(const QString &icon_path, const QString &name);
    
    void addGroupGeneral(const QString &name);
    QLineEdit *line_name = nullptr;
    QDateEdit *date_install = nullptr;
    QComboBox *combo_model_role = nullptr;
    
    void addGroupEndpoints();
    
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
    
    void addGroupDemands();
    
    
    void openDemandsEditor();
    QPointer<QDialog> dialog_demands = nullptr;
    QPointer<QTableWidget> table_demands = nullptr;
    
    void addGroupHistory();
    
    void addStretches();
    
private:
    QTabWidget *tabs = nullptr;
    QVBoxLayout *layout_main = nullptr;
    QLabel *label_title = nullptr;
    
    QScrollArea *scroll_overview = nullptr;
    QWidget *widget_overview = nullptr;
    QVBoxLayout *layout_overview = nullptr;
    
    QScrollArea *scroll_configuration = nullptr;
    QWidget *widget_configuration = nullptr;
    QVBoxLayout *layout_configuration = nullptr;
    
    QScrollArea *scroll_sim_meas = nullptr;
    QWidget *widget_sim_meas = nullptr;
    QVBoxLayout *layout_sim_meas = nullptr;
    
    QScrollArea *scroll_history = nullptr;
    QWidget *widget_history = nullptr;
    QVBoxLayout *layout_history = nullptr;
    
private slots:
    void onGroupExpand(GroupBoxCollapsible *group);
    
    void onElevationModeSignalChanged(int index);
    void onElevationCalc();
    
public slots:
    virtual void onHeadlossFormulaChanged(HeadlossFormulas formulas);
};

#endif
