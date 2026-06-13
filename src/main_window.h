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

#include "footer_statusbar.h"
#include "rest_client.h"
#include "menubar.h"
#include "map_container.h"
#include "map_widget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    
private:
    MenuBar *menu;
    MapContainer *map_container;
    MapWidget *map;
    FooterStatusBar *footer;
    QTabWidget *tabs;
    
    QGridLayout *layout = new QGridLayout;
    QLineEdit *line_server_status;
    
    QLabel *label_image_a;
    QLabel *label_image_b;
    
    RESTClient *rest_check_map;
    
    void checkServerMapInit();
    void checkServerMap();
    bool checking_server_map = false;
};
