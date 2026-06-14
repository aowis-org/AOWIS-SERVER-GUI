#include "main_window.h"
#include <QPushButton>
#include <qapplication.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    tabs( new QTabWidget(this) ),
    map_monitor( new MapMonitorContainer(this) ),
    map_editor( new MapEditorContainer(this) ),
    reservoirs( new ReservoirsWidget(this) ),
    tanks( new TanksWidget(this) ),
    pumps( new PumpsWidget(this) ),
    valves( new ValvesWidget(this) ),
    junctions( new JunctionsWidget(this) ),
    pipes( new PipesWidget(this) ),
    customerPoints( new CustomerPointsWidget(this) ),
    customers( new CustomersWidget(this) )
{
    setWindowTitle("AOWIS - Controller");
    showMaximized();
    
    this->tabs->setContentsMargins(0, 0, 0, 0);
    
    this->setCentralWidget(this->tabs);
    
    this->footer = new FooterStatusBar(this);
    setStatusBar(this->footer->statusBar());
    
    this->setMinimumHeight(600);
    this->setMinimumWidth(800);
    
    //this->menu = new MenuBar();
    //setMenuBar(this->menu);
    
    this->map = this->map_monitor->mapWidget();
    this->tabs->addTab(this->map_monitor, "Map Monitor");
    this->tabs->addTab(this->map_editor, "Map Editor");
    this->tabs->addTab(this->reservoirs, "Reservoirs");
    this->tabs->addTab(this->tanks, "Tanks");
    this->tabs->addTab(this->pumps, "Pumps");
    this->tabs->addTab(this->valves, "Valves");
    this->tabs->addTab(this->junctions, "Junctions");
    this->tabs->addTab(this->pipes, "Pipes");
    this->tabs->addTab(this->customerPoints, "Customer Points");
    this->tabs->addTab(this->customers, "Customers");
    
    connect(this->map, &MapWidget::signalZoomChanged, this->footer, &FooterStatusBar::setMapZoom);
    connect(this->map, &MapWidget::signalCoordsChanged, this->footer, &FooterStatusBar::setMapCoordinates);
    
    //connect(this->menu, &MenuBar::signalMapZoomIn, this->map, &MapWidget::zoomIn);
    //connect(this->menu, &MenuBar::signalMapZoomOut, this->map, &MapWidget::zoomOut);
    //connect(this->menu, &MenuBar::signalMapChange, this->map, &MapWidget::changeMapProvider);
    
    
    checkServerMapInit();
}

void MainWindow::checkServerMapInit()
{
    this->time_server_map_success_last = QDateTime::currentDateTime();    
    this->footer->statusUpdateServerMap(StatusColorCode::Yellow);
    
    this->rest_check_map = new RESTClient("http://aowis-server-map.localhost:80", this);
    connect(this->rest_check_map, &RESTClient::requestFinished, this, [this](const QByteArray &data)
            {
                this->checking_server_map = false;
                
                this->time_server_map_success_last = QDateTime::currentDateTime();
            });
    connect(this->rest_check_map, &RESTClient::requestError, this, [this](const QString &err)
            {
                this->checking_server_map = false;
            });
    
    // set up timer for periodic check
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::checkServerMap);
    timer->start(4500);
}
void MainWindow::checkServerMap()
{
    if (this->checking_server_map)
        return;
    
    int delta = this->time_server_map_success_last.secsTo(QDateTime::currentDateTime());
    if (delta < 5)
        this->footer->statusUpdateServerMap(StatusColorCode::Green);
    else if (delta < 15)
        this->footer->statusUpdateServerMap(StatusColorCode::Yellow);
    else
        this->footer->statusUpdateServerMap(StatusColorCode::Red);
    
    this->checking_server_map = true;
    this->rest_check_map->get("/status");
}

