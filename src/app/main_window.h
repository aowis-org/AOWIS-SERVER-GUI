#pragma once

#include <QApplication>
#include <QDockWidget>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QSet>
#include <QWidget>
#include <QGridLayout>
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

#include "app/footer_statusbar.h"
#include "services/rest_client.h"
#include "app/menubar.h"

#include "tabs/tab_settings_widget.h"
#include "config/shortcut_registry.h"

#include "map/core/map_model.h"
#include "map/data/map_tile_repository.h"
#include "map/data/map_terrain_repository.h"
#include "map/core/map_widget.h"
#include "tabs/tab_map_monitor_container.h"
#include "tabs/tab_map_editor_container.h"

#include "tabs/tab_energy_widget.h"
#include "tabs/tab_reservoirs_widget.h"
#include "tabs/tab_tanks_widget.h"
#include "tabs/tab_pumps_widget.h"
#include "tabs/tab_valves_widget.h"
#include "tabs/tab_junctions_widget.h"
#include "tabs/tab_pipes_widget.h"
#include "tabs/tab_customer_points_widget.h"
#include "tabs/tab_customers_widget.h"
#include "tabs/tab_logs_widget.h"
#include "tabs/tab_alarms_widget.h"
#include "app/top_control_bar.h"
#include "entity_inspector/entity_inspector_dock.h"
#include "entity_inspector/entity_map_legend_dock.h"
#include "map/editor/map_editor_guide_dock.h"
#include "simulation/simulation_manager.h"
#include "widgets/main_navigation_widget.h"

#include "network/hydraulic_data.h"

#ifdef Q_OS_WASM
#include <emscripten.h>
#include <emscripten/html5.h>
#include "gps/gps_provider_dummy.h"
#else
#include "gps/gps_provider.h"
#endif

class QShortcut;

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
    void toggleRightDockArea();
    void showEntityInspectorForSelection();
    void hideRightDock(QDockWidget *dock);
    void onRightDockVisibilityChanged(QDockWidget *dock, bool visible);
    void scheduleRightDockResize();
    void resizeRightDocks();
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
    bool right_dock_area_hidden = false;
    QHash<QDockWidget *, bool> right_dock_visibility;
    QSet<QDockWidget *> right_docks_being_hidden;
    bool right_dock_resize_pending = false;
    
    MapTileRepository *map_tile_repository;
    MapTerrainRepository *map_terrain_repository;
    MapModel *map_model_monitor;
    MapModel *map_model_editor;
    MapWidget *map_mon = nullptr;
    MapWidget *map_edit = nullptr;

    bool sync_map_movement = true;
    bool syncing_map_movement = false;
    void syncMapMovement(MapWidget *source, MapWidget *target);
    
    MainNavigationWidget *main_navigation = nullptr;
    SettingsWidget *settings;
    int settings_page_index = -1;
    QShortcut *shortcut_toggle_right_docks = nullptr;
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
    
protected:
    void resizeEvent(QResizeEvent *event) override;
};
