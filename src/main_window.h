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
#include <QEvent>
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
#include "map/map_tile_repository.h"
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
#include "top_control_bar.h"
#include "entity_inspector/entity_inspector_dock.h"
#include "entity_inspector/entity_map_legend_dock.h"
#include "map/map_editor_guide_dock.h"
#include "simulation_manager.h"

#include "hydraulic_data.h"

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
    void changeEvent(QEvent *event) override;
    
private:
    Qt::WindowStates window_state_saved;
    QRect window_geometry_saved;
    void fullScreenToggle();
    void updateMapEdgePanning();

#ifdef Q_OS_WASM
    static EM_BOOL fullScreenChangeCallback(int event_type, const EmscriptenFullscreenChangeEvent *event, void *user_data);
    bool browser_fullscreen_active = false;
#endif
    
    HydraulicData *hydraulic_data = nullptr;
    
    GpsProvider *gps = nullptr;
    
    MenuBar *menu;
    FooterStatusBar *footer;
    
    EntityInspectorDock *dock_entity_inspector;
    EntityMapLegendDock *dock_entity_map_legend;
    MapEditorGuideDock *dock_map_editor_guide;
    TopControlBar *top_control_bar;
    
    MapTileRepository *map_tile_repository;
    MapModel *map_model_monitor;
    MapModel *map_model_editor;
    MapWidget *map_mon = nullptr;
    MapWidget *map_edit = nullptr;

    bool sync_map_movement = true;
    bool syncing_map_movement = false;
    void syncMapMovement(MapWidget *source, MapWidget *target);
    
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
