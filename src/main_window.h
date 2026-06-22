#pragma once

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QGridLayout>
#include <QTabWidget>

#include <QPushButton>
#include <QLineEdit>
#include <QLabel>

#include <QByteArray>

#include <QTimer>
#include <QTime>
#include <QDateTime>

#include "footer_statusbar.h"
#include "rest_client.h"
#include "menubar.h"

#include "tab_settings_widget.h"

#include "map_model.h"
#include "map_widget.h"
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

#ifdef Q_OS_WASM
#include "gps_provider_dummy.h"
#else
#include "gps_provider.h"
#endif

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    
private:
    GpsProvider *gps = nullptr;
    
    MenuBar *menu;
    FooterStatusBar *footer;
    
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
    
    //QGridLayout *layout = new QGridLayout;
    
    QLabel *label_image_a;
    QLabel *label_image_b;
    
    RESTClient *rest_check_map;
    
    QDateTime time_server_map_success_last;
    void checkServerMapInit();
    void checkServerMap();
    bool checking_server_map = false;
    
};
