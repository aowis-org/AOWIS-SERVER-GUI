#pragma once

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QGridLayout>
#include <QTabWidget>
#include <QTabBar>
#include <QTransform>
#include <QResizeEvent>
#include <QProcessEnvironment>

#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QIcon>

#include <QKeyEvent>
#include <QMessageBox>
#include <QContextMenuEvent>

#include <QByteArray>

#include <QTimer>
#include <QTime>
#include <QDateTime>

#include "footer_statusbar.h"
#include "rest_client.h"
#include "menubar.h"

#include "tab_settings_widget.h"

#include "map/map_model.h"
#include "map/map_widget.h"
#include "tab_map_monitor_container.h"
#include "tab_map_editor_container.h"

#include "tab_energy_widget.h"
#include "tab_reservoirs_widget.h"
#include "tab_tanks_widget.h"
#include "tab_pumps_widget.h"
#include "tab_valves_widget.h"
#include "tab_junctions_widget.h"
#include "tab_pipes_widget.h"
#include "tab_customer_points_widget.h"
#include "tab_customers_widget.h"
#include "tab_logs_widget.h"
#include "tab_alarms_widget.h"
#include "sim_control_dock.h"
#include "entity_inspector/entity_inspector_dock.h"
#include "entity_inspector/entity_map_legend_dock.h"
#include "simulation_manager.h"

#ifdef Q_OS_WASM
#include <emscripten.h>
#include <emscripten/html5.h>
#include "gps_provider_dummy.h"
#else
#include "gps_provider.h"
#endif

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    
protected:
    void keyPressEvent(QKeyEvent *event) override;
    
private:
    Qt::WindowStates window_state_saved;
    QRect window_geometry_saved;
    void fullScreenToggle();
    
    GpsProvider *gps = nullptr;
    
    MenuBar *menu;
    FooterStatusBar *footer;
    
    EntityInspectorDock *dock_entity_inspector;
    EntityMapLegendDock *dock_entity_map_legend;
    SimControlDock *dock_sim_control;
    
    MapModel *map_model;
    MapWidget *map_mon;
    MapWidget *map_edit;
    
    QTabWidget *tabs;
    SettingsWidget *settings;
    MapMonitorContainer *map_monitor;
    MapEditorContainer *map_editor;
    EnergyWidget *energy;
    ReservoirsWidget *reservoirs;
    TanksWidget *tanks;
    PumpsWidget *pumps;
    ValvesWidget *valves;
    JunctionsWidget *junctions;
    PipesWidget* pipes;
    CustomerPointsWidget *customerPoints;
    CustomersWidget *customers;
    LogsWidget *logs;
    AlarmsWidget *alarms;
    SimulationManager *simulation_manager;
    
    
    //QGridLayout *layout = new QGridLayout;
    
    QLabel *label_image_a;
    QLabel *label_image_b;
    
    RESTClient *rest_check_map;
    
    QDateTime time_server_map_success_last;
    void checkServerMapInit();
    void checkServerMap();
    bool checking_server_map = false;
    
    int tab_spacer_tab_index = -1;
    int tab_last_spacer_height = -1;
    void updateTabSpacer();
    
protected:
    void resizeEvent(QResizeEvent *event) override;
};
