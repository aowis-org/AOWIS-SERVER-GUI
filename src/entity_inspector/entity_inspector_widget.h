#ifndef ENTITY_INSPECTOR_WIDGET_H
#define ENTITY_INSPECTOR_WIDGET_H

#include <QWidget>
#include <QByteArray>
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
#include <QHash>

#include <QTableWidget>
#include <QHeaderView>

#include "../widgets/group_box_collapsible.h"
#include "../hydraulic_data.h"
#include <aowis/model/hydraulic/network_hydraulic.h>

#include "../_enums_structs.h"
#include "../_sizes.h"

class RESTClient;
class QMessageBox;
class QGridLayout;

class EntityInspectorWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit EntityInspectorWidget(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
    void setCurrentTabIndex(int index);

signals:
    void signalCurrentTabChanged(int index);
    
protected:
    //QVBoxLayout *mainLayout() const;
    QVBoxLayout *layoutOverview();
    QVBoxLayout *layoutConfiguration();
    QVBoxLayout *layoutSimMeas();
    QVBoxLayout *layoutQuality();
    QVBoxLayout *layoutAlerts();
    QVBoxLayout *layoutHistory();
    
    void setTitle(const QString &title);
    
    void addGroupOverviewImage(const QString &icon_path, const QString &name);
    
    void addGroupGeneral(const QString &name);
    QLineEdit *line_name = nullptr;
    QDateEdit *date_added = nullptr;
    QDateEdit *date_installed = nullptr;
    QComboBox *combo_model_role = nullptr;
    QCheckBox *check_enabled = nullptr;
    
    void addGroupEndpoints();
    QLabel *label_node_1_id = nullptr;
    QLabel *label_node_2_id = nullptr;
    QPushButton *button_node_1_locate = nullptr;
    QPushButton *button_node_1_inspect = nullptr;
    QPushButton *button_node_2_locate = nullptr;
    QPushButton *button_node_2_inspect = nullptr;
    
    void addGroupPosition();
    QDoubleSpinBox *spin_latitude = nullptr;
    QDoubleSpinBox *spin_longitude = nullptr;
    QPushButton *button_find_on_map = nullptr;

    void bindHydraulicNode(InfrastructureEntity entity_type, const QUuid &uuid,
                           const QString &title_prefix);
    void bindHydraulicLink(InfrastructureEntity entity_type, const QUuid &uuid,
                           const QString &title_prefix);
    
    void addGroupElevation();
    GroupBoxCollapsible *group_elevation = nullptr;
    QComboBox *combo_elevation_input_type = nullptr;
    
    QLabel *label_terrain_elevation = nullptr;
    QPushButton *button_terrain_elevation = nullptr;
    QLabel *label_terrain_elevation_status = nullptr;
    QDoubleSpinBox *spin_terrain_elevation = nullptr;
    QLabel *label_elevation_offset = nullptr;
    QDoubleSpinBox *spin_elevation_offset = nullptr;
    QLabel *label_elevation_value = nullptr;
    QDoubleSpinBox *spin_elevation_value = nullptr;
    QComboBox *combo_head_pattern = nullptr;
    
    void addGroupDemands();
    QLabel *label_demands_summary = nullptr;
    QDoubleSpinBox *spin_emitter_coefficient = nullptr;

    void addGroupSimulation();

    void openDemandsEditor();
    QPointer<QDialog> dialog_demands = nullptr;
    QPointer<QTableWidget> table_demands = nullptr;
    
    void addGroupHistory();
    
    void addStretches();
    
private:
    enum class SimulationField
    {
        ResultTime,
        DemandRequested,
        DemandDelivered,
        DemandDeficit,
        TotalDemand,
        NetDemand,
        EmitterFlow,
        LeakageFlow,
        Head,
        PressureHead,
        WaterLevel,
        Volume,
        MixingZoneVolume,
        Flow,
        Velocity,
        HeadLoss,
        UnitHeadLoss,
        FrictionFactor,
        Roughness,
        Status,
        PumpState,
        Speed,
        Efficiency,
        Power,
        ValveRegulating,
        Setting,
        ReferencedByControl,
        TimeOnline,
        AverageEfficiency,
        AverageSpecificPower,
        AveragePower,
        PeakPower,
        AverageCostPerDay
    };

    struct SimulationRowWidgets
    {
        QLabel *name = nullptr;
        QLabel *value = nullptr;
    };

    void addSimulationRow(QGridLayout *grid, int &row, SimulationField field,
                          const QString &name, const QString &tooltip = QString());
    void refreshSimulation();
    void resetSimulationValues();
    void setSimulationText(SimulationField field, const QString &text);
    void setSimulationValue(SimulationField field, double value, int decimals,
                            const QString &unit = QString());
    void setSimulationEntityAvailable(bool available);
    void refreshPumpEnergySummary(const HydraulicSimulationResultTimeline &timeline);

    void refreshHydraulicGeneral(const QString &id, const HydraulicEntityMetadata &metadata);
    void refreshHydraulicNode();
    void refreshHydraulicLink();
    void refreshHydraulicEndpoints();
    void refreshHydraulicEndpoint(const QUuid &node_uuid, QLabel *label_node_id,
                                  QPushButton *button_locate, QPushButton *button_inspect);
    void locateHydraulicEndpoint(const QUuid &node_uuid);
    void inspectHydraulicEndpoint(const QUuid &node_uuid);
    void refreshHydraulicNodeElevation();
    void scheduleJunctionDemandsRefresh();
    void refreshJunctionDemands();
    void rebuildJunctionDemandRows(const HydraulicNodeJunction &junction);
    void addJunctionDemandRow(int demand_index, const HydraulicNodeJunctionDemand &demand);
    void updateJunctionDemandRow(int demand_index, const HydraulicNodeJunctionDemand &demand);
    void populateTimePatternCombo(QComboBox *combo_pattern, HydraulicTimePatternMode pattern_mode, const QUuid &pattern_uuid);
    void updateElevationModeUi();
    void updateCalculatedElevation();
    void requestTerrainElevation();
    void handleTerrainElevationResponse(const QByteArray &data);
    void handleTerrainElevationError(const QString &error);
    void showTerrainElevationErrorMessage(const QString &error);
    void setTerrainElevationRequestActive(bool active);
    bool setElevationInputType(HydraulicNodeElevationInputType input_type);
    bool setElevationValue(double value_m);
    bool setTerrainElevation(double terrain_elevation_m);
    bool setElevationOffset(double offset_m);
    std::optional<QDate> optionalDate(const QDateEdit *date_edit) const;
    void setOptionalDate(QDateEdit *date_edit, const std::optional<QDate> &date);

    HydraulicData *hydraulic_data = nullptr;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    QUuid entity_uuid;
    QUuid node_uuid_1;
    QUuid node_uuid_2;
    QString entity_title_prefix;
    bool junction_demands_refresh_pending = false;
    RESTClient *terrain_elevation_client = nullptr;
    QPointer<QMessageBox> terrain_elevation_message_box = nullptr;
    QUuid terrain_elevation_request_entity_uuid;
    CoordinateWGS84 terrain_elevation_request_coordinate;
    bool terrain_elevation_request_active = false;

    QHash<int, SimulationRowWidgets> simulation_rows;
    QLabel *label_simulation_message = nullptr;
    QLabel *label_simulation_energy_message = nullptr;
    
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
    
    QScrollArea *scroll_quality = nullptr;
    QWidget *widget_quality = nullptr;
    QVBoxLayout *layout_quality = nullptr;
    
    QScrollArea *scroll_alerts = nullptr;
    QWidget *widget_alerts = nullptr;
    QVBoxLayout *layout_alerts = nullptr;
    
    QScrollArea *scroll_history = nullptr;
    QWidget *widget_history = nullptr;
    QVBoxLayout *layout_history = nullptr;
    
private slots:
    void onGroupExpand(GroupBoxCollapsible *group);
    
    void onElevationInputTypeChanged(int index);
    void onElevationValueChanged(double value_m);
    void onTerrainElevationChanged(double terrain_elevation_m);
    void onElevationOffsetChanged(double offset_m);
    
public slots:
    virtual void onHeadlossFormulaChanged(HeadlossFormulas formulas);
};

#endif
