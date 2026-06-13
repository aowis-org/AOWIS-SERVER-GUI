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
    
    this->menu = new MenuBar();
    //setMenuBar(this->menu);
    
    this->map = this->map_container->mapWidget();
    this->tabs->addTab(this->map_container, "Map");
    connect(this->map, &MapWidget::signalZoomChanged, this->footer, &FooterStatusBar::setMapZoom);
    connect(this->map, &MapWidget::signalCoordsChanged, this->footer, &FooterStatusBar::setMapCoordinates);
    
    connect(this->menu, &MenuBar::signalMapZoomIn, this->map, &MapWidget::zoomIn);
    connect(this->menu, &MenuBar::signalMapZoomOut, this->map, &MapWidget::zoomOut);
    connect(this->menu, &MenuBar::signalMapChange, this->map, &MapWidget::changeMapProvider);
    
    this->layout->addWidget(this->tabs, 0, 0, 1, 2);
    
    checkAPIServer();
    
    
    
    
    
}

void MainWindow::checkAPIServer()
{
    // Testing the HTTP connection to AOWIS-Map-Server
    
    RESTClient *rest = new RESTClient("http://aowis-server-map.localhost:80", this);
    connect(rest, &RESTClient::requestFinished, this, [this, rest](const QByteArray &data)
    {
        this->line_server_status->setText(data);
        
        rest->deleteLater();
    });
    connect(rest, &RESTClient::requestError, this, [this, rest](const QString &err)
    {
        this->line_server_status->setText("REST ERROR: " + err);
        
        rest->deleteLater();
    });
    rest->get("/status");
}

