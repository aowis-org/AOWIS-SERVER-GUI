#pragma once

#include <QMainWindow>

#include <QWidget>
#include <QGridLayout>

#include <QPushButton>
#include <QLineEdit>
#include <QLabel>

#include <QByteArray>

#include "rest_client.h"
#include "menubar.h"
#include "map_widget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    
private:
    MenuBar *menu;
    MapWidget *map;
    
    QGridLayout *layout = new QGridLayout;
    QLineEdit *line_server_status;
    
    QLabel *label_image_a;
    QLabel *label_image_b;
    
    void checkAPIServer();
    void getOpenStreetMapTile(QLabel *label, QString endpoint);
};
