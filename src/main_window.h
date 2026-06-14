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
#include "map_monitor_container.h"
#include "map_widget.h"
#include "reservoirs_widget.h"
#include "tanks_widget.h"
#include "pumps_widget.h"
#include "valves_widget.h"
#include "junctions_widget.h"
#include "customer_points_widget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    
private:
    MenuBar *menu;
    MapMonitorContainer *map_monitor_container;
    FooterStatusBar *footer;
    
    QTabWidget *tabs;
    MapWidget *map;
    ReservoirsWidget *reservoirs;
    TanksWidget *tanks;
    PumpsWidget *pumps;
    ValvesWidget *valves;
    JunctionsWidget *junctions;
    CustomerPointsWidget *customerPoints;
    
    //QGridLayout *layout = new QGridLayout;
    
    QLabel *label_image_a;
    QLabel *label_image_b;
    
    RESTClient *rest_check_map;
    
    QDateTime time_server_map_success_last;
    void checkServerMapInit();
    void checkServerMap();
    bool checking_server_map = false;
};
