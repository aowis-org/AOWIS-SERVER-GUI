#include "main_window.h"
#include <QPushButton>
#include <qapplication.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    gps( new GpsProvider(this) ),
    map_model( new MapModel(this) ),
    tabs( new QTabWidget(this) ),
    settings( new SettingsWidget(this) ),
    map_monitor( new MapMonitorContainer(map_model, gps, this) ),
    map_editor( new MapEditorContainer(map_model, gps, this) ),
    energy( new EnergyWidget(this) ),
    reservoirs( new ReservoirsWidget(this) ),
    tanks( new TanksWidget(this) ),
    pumps( new PumpsWidget(this) ),
    valves( new ValvesWidget(this) ),
    junctions( new JunctionsWidget(this) ),
    pipes( new PipesWidget(this) ),
    customerPoints( new CustomerPointsWidget(this) ),
    customers( new CustomersWidget(this) ),
    logs( new LogsWidget(this) ),
    alarms( new AlarmsWidget(this) )
{
    #ifdef AOWIS_STANDALONE
    setWindowTitle("AOWIS Controller [Standalone]");
    #else
    setWindowTitle("AOWIS Controller");
    #endif
    
    showMaximized();
    
    //this->gps->start();
    this->gps->startGpsd("127.0.0.1");
    
    this->map_mon = this->map_monitor->getMap();
    this->map_edit = this->map_editor->getMap();
    
    this->tabs->setContentsMargins(0, 0, 0, 0);
    
    this->setCentralWidget(this->tabs);
    
    this->footer = new FooterStatusBar(this);
    setStatusBar(this->footer->statusBar());
    
    this->setMinimumHeight(600);
    this->setMinimumWidth(800);
    
    this->tabs->setIconSize(QSize(40, 40));
    this->tabs->setTabPosition(QTabWidget::West);
    
    /*
    this->tabs->addTab(this->settings, QIcon(":/img/gothic/settings.png"), "Settings");
    this->tabs->addTab(this->map_editor, QIcon(":/img/gothic/map_edit.png"), "Map Editor");
    this->tabs->addTab(this->map_monitor, QIcon(":/img/gothic/map_monitor.png"), "Map Monitor");
    this->tabs->addTab(this->energy, QIcon(":/img/gothic/energy.png"), "Energy");
    
    this->tabs->addTab(this->reservoirs, QIcon(":/img/gothic/reservoir.png"), "Reservoirs");
    this->tabs->addTab(this->tanks, QIcon(":/img/gothic/tower.png"), "Tanks");
    this->tabs->addTab(this->pumps, QIcon(":/img/gothic/pump.png"), "Pumps");
    this->tabs->addTab(this->valves, QIcon(":/img/gothic/valve.png"), "Valves");
    this->tabs->addTab(this->junctions, QIcon(":/img/gothic/junction.png"), "Junctions");
    
    this->tabs->addTab(this->pipes, QIcon(":/img/gothic/pipe.png"), "Pipes");
    
    this->tabs->addTab(this->customerPoints, QIcon(":/img/gothic/customer.png"), "Customer Points");
    this->tabs->addTab(this->customers, QIcon(":/img/gothic/users.png"), "Customers");
    */
    
    auto rotatedIcon = [](const QString &path)
    {
        QPixmap pixmap(path);
        return QIcon(pixmap.transformed(QTransform().rotate(90), Qt::SmoothTransformation));
    };
    
    this->tabs->addTab(new QWidget(this), rotatedIcon(":/img/gothic/dashboard_global.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Dashboard");
    
    //this->tabs->addTab(new QWidget(this), rotatedIcon(":/img/gothic/site_switcher.png"), "");
    //this->tabs->setTabToolTip(this->tabs->count() - 1, "Site Switcher");
    
    //this->tabs->addTab(new QWidget(this), rotatedIcon(":/img/gothic/dashboard.png"), "");
    //this->tabs->setTabToolTip(this->tabs->count() - 1, "Site Dashboard");
    
    this->tabs->addTab(this->map_editor, rotatedIcon(":/img/gothic/map_edit.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Map Editor");
    
    this->tabs->addTab(this->map_monitor, rotatedIcon(":/img/gothic/map_monitor.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Map Monitor");
    
    this->tabs->addTab(this->energy, rotatedIcon(":/img/gothic/energy.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Energy");
    
    this->tabs->addTab(this->reservoirs, rotatedIcon(":/img/gothic/reservoir.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Reservoirs");
    
    this->tabs->addTab(this->tanks, rotatedIcon(":/img/gothic/tower.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Tanks");
    
    this->tabs->addTab(this->pumps, rotatedIcon(":/img/gothic/pump.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Pumps");
    
    this->tabs->addTab(this->valves, rotatedIcon(":/img/gothic/valve.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Valves");
    
    this->tabs->addTab(this->junctions, rotatedIcon(":/img/gothic/junction.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Junctions");
    
    this->tabs->addTab(this->pipes, rotatedIcon(":/img/gothic/pipe.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Pipes");
    
    this->tabs->addTab(this->customerPoints, rotatedIcon(":/img/gothic/customer.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Customer Points");
    
    this->tabs->addTab(this->customers, rotatedIcon(":/img/gothic/users.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Customers");
    
    this->tab_spacer_tab_index = this->tabs->addTab(new QWidget(this->tabs), "");
    this->tabs->setTabEnabled(this->tab_spacer_tab_index, false);
    
    this->tabs->addTab(this->alarms, rotatedIcon(":/img/gothic/alarm.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Alarms");
    
    this->tabs->addTab(this->logs, rotatedIcon(":/img/gothic/log.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Logs");
    
    this->tabs->addTab(this->settings, rotatedIcon(":/img/gothic/settings.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Settings");
    
    this->tabs->setCurrentIndex(2);
    
    QTimer::singleShot(0, this, &MainWindow::updateTabSpacer);
    
    connect(this->map_mon, &MapWidget::signalZoomChanged, this->footer, &FooterStatusBar::setMapZoom);
    connect(this->map_mon, &MapWidget::signalCoordsChangedWgs84, this->footer, &FooterStatusBar::setMapCoordinatesWGS84);
    connect(this->map_mon, &MapWidget::signalCoordsChangedUTM, this->footer, &FooterStatusBar::setMapCoordinatesUTM);
    
    connect(this->map_edit, &MapWidget::signalZoomChanged, this->footer, &FooterStatusBar::setMapZoom);
    connect(this->map_edit, &MapWidget::signalCoordsChangedWgs84, this->footer, &FooterStatusBar::setMapCoordinatesWGS84);

    #ifdef AOWIS_STANDALONE
    #else    
    checkServerMapInit();
    #endif
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

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    this->updateTabSpacer();
}

void MainWindow::updateTabSpacer()
{
    if (this->tab_spacer_tab_index < 0)
        return;
    
    const int normal_tab_height = 56;
    
    // Your first tab is larger.
    const int first_tab_height = 68;
    const int first_tab_margin_bottom = 8;
    
    // Safety offset. Increase if bottom tabs are too low.
    // Decrease if bottom tabs are too high.
    #ifdef Q_OS_WASM
    const int spacer_offset = -19;
    #else
    const int spacer_offset = -78;
    #endif
    
    int used_height = 0;
    
    for (int tab = 0; tab < this->tabs->count(); ++tab)
    {
        if (tab == this->tab_spacer_tab_index)
            continue;
        
        if (tab == 0)
            used_height += first_tab_height + first_tab_margin_bottom;
        else
            used_height += normal_tab_height;
    }
    
    // Important: do NOT use tabBar()->height() here.
    // The tab bar height changes because of the spacer, causing feedback/creeping.
    const int available_height = this->tabs->contentsRect().height();
    
    // Important: subtract offset BEFORE qMax().
    const int spacer_height = qMax(0, available_height - used_height - spacer_offset);
    
    if (spacer_height == this->tab_last_spacer_height)
        return;
    
    this->tab_last_spacer_height = spacer_height;
    
    this->tabs->setStyleSheet(QString(R"(
        QTabBar::tab {
            width: 56px;
            height: 56px;
        }
        
        QTabBar::tab:disabled {
            width: 56px;
            height: %1px;
            background: transparent;
        }
    )").arg(spacer_height));
}
