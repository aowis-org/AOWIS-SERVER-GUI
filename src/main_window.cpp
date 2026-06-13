#include "main_window.h"
#include <QPushButton>
#include <qapplication.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    tabs( new QTabWidget(this) ),
    map_container( new MapContainer(this) )
{
    setWindowTitle("AOWIS - Controller");
    showMaximized();
    
    this->tabs->setContentsMargins(0, 0, 0, 0);
    
    QWidget *widget_central = new QWidget(this);
    this->setCentralWidget(widget_central);
    widget_central->setLayout(this->layout);
    
    this->footer = new FooterStatusBar(this);
    setStatusBar(this->footer->statusBar());
    
    this->line_server_status = new QLineEdit();
    this->line_server_status->setDisabled(true);
    
    this->layout->addWidget(this->line_server_status, 1, 0, 1, 2);
    
    this->setMinimumHeight(600);
    this->setMinimumWidth(800);
    
    //this->menu = new MenuBar();
    //setMenuBar(this->menu);
    
    this->map = this->map_container->mapWidget();
    this->tabs->addTab(this->map_container, "Map Monitor");
    
    connect(this->map, &MapWidget::signalZoomChanged, this->footer, &FooterStatusBar::setMapZoom);
    connect(this->map, &MapWidget::signalCoordsChanged, this->footer, &FooterStatusBar::setMapCoordinates);
    
    //connect(this->menu, &MenuBar::signalMapZoomIn, this->map, &MapWidget::zoomIn);
    //connect(this->menu, &MenuBar::signalMapZoomOut, this->map, &MapWidget::zoomOut);
    //connect(this->menu, &MenuBar::signalMapChange, this->map, &MapWidget::changeMapProvider);
    
    this->layout->addWidget(this->tabs, 0, 0, 1, 2);
    
    
    checkServerMapInit();
}

void MainWindow::checkServerMapInit()
{
    this->rest_check_map = new RESTClient("http://aowis-server-map.localhost:80", this);
    connect(this->rest_check_map, &RESTClient::requestFinished, this, [this](const QByteArray &data)
            {
                this->checking_server_map = false;
                this->line_server_status->setText(data);
            });
    connect(this->rest_check_map, &RESTClient::requestError, this, [this](const QString &err)
            {
                this->checking_server_map = false;
                this->line_server_status->setText("REST ERROR: " + err);
            });
    
    // on app start run directly
    checkServerMap();
    
    // set up timer for periodic check
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::checkServerMap);
    timer->start(5000);
}
void MainWindow::checkServerMap()
{
    if (this->checking_server_map)
        return;
    
    this->checking_server_map = true;
    this->rest_check_map->get("/status");
}

