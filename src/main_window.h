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
#include "map_widget.h"
#include "map_monitor_container.h"
#include "map_editor_container.h"
#include "energy_widget.h"
#include "reservoirs_widget.h"
#include "tanks_widget.h"
#include "pumps_widget.h"
#include "valves_widget.h"
#include "junctions_widget.h"
#include "pipes_widget.h"
#include "customer_points_widget.h"
#include "customers_widget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    
private:
    MenuBar *menu;
    FooterStatusBar *footer;
    
    QTabWidget *tabs;
    MapWidget *map;
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
