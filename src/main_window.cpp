#include "main_window.h"
#include <QPushButton>
#include <qapplication.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
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
    setMenuBar(this->menu);
    
    this->map = new MapWidget(this);
    this->layout->addWidget(this->map, 0, 0, 1, 2);
    connect(this->map, &MapWidget::signalZoomChanged, this->footer, &FooterStatusBar::setMapZoom);
    connect(this->map, &MapWidget::signalCoordsChanged, this->footer, &FooterStatusBar::setMapCoordinates);
    
    checkAPIServer();
    
    connect(this->menu, &MenuBar::signalMapZoomIn, this->map, &MapWidget::zoomIn);
    connect(this->menu, &MenuBar::signalMapZoomOut, this->map, &MapWidget::zoomOut);
    connect(this->menu, &MenuBar::signalMapChange, this->map, &MapWidget::changeMapProvider);
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

