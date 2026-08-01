#include "main_window.h"
#include <QPushButton>
#include <QShortcut>
#include <qapplication.h>

namespace
{
QIcon rotatedIcon(const QString &path)
{
    const QPixmap pixmap(path);
    return QIcon(pixmap.transformed(QTransform().rotate(90), Qt::SmoothTransformation));
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    hydraulic_data(new HydraulicData(this)),
    gps( new GpsProvider(this) ),
    dock_entity_inspector( new EntityInspectorDock(hydraulic_data, this) ),
    dock_entity_map_legend( new EntityMapLegendDock(hydraulic_data, this)),
    dock_map_editor_guide( new MapEditorGuideDock(this) ),
    top_control_bar( new TopControlBar(this) ),
    map_tile_repository( new MapTileRepository(this) ),
    map_model_monitor( new MapModel(this) ),
    map_model_editor( new MapModel(this) ),
    tabs( new QTabWidget(this) ),
    settings( new SettingsWidget(this) ),
    map_monitor( new MapMonitorContainer(this->map_model_monitor, this->map_tile_repository, this->gps, this) ),
    map_editor( new MapEditorContainer(this->map_model_editor, this->map_tile_repository, this->hydraulic_data, this->gps, this->dock_entity_inspector, this) ),
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
    alarms( new AlarmsWidget(this) ),
    simulation_manager( new SimulationManager(hydraulic_data, this) )
{
    #ifdef AOWIS_STANDALONE
    setWindowTitle("AOWIS Controller [Standalone]");
    #else
    setWindowTitle("AOWIS Controller");
    #endif
    
    showMaximized();
    
    QLocale layout = qApp->inputMethod()->locale();
    qDebug() << layout.name();   // e.g. "en_US", "de_DE"
    
    
    //this->gps->start();
    #ifdef Q_OS_LINUX
    this->gps->startGpsd("127.0.0.1");
    #endif
    
    addDockWidget(Qt::RightDockWidgetArea, this->dock_entity_inspector);
    addDockWidget(Qt::RightDockWidgetArea, this->dock_entity_map_legend);
    addDockWidget(Qt::RightDockWidgetArea, this->dock_map_editor_guide);
    addToolBar(Qt::TopToolBarArea, this->top_control_bar);

    const QList<QDockWidget *> right_docks = findChildren<QDockWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QDockWidget *dock : right_docks)
    {
        connect(dock, &QDockWidget::visibilityChanged, this, [this, dock](bool visible)
        {
            onRightDockVisibilityChanged(dock, visible);
        });
    }

    QShortcut *shortcut_toggle_right_docks = new QShortcut(QKeySequence(Qt::Key_Tab), this);
    shortcut_toggle_right_docks->setContext(Qt::WindowShortcut);
    shortcut_toggle_right_docks->setAutoRepeat(false);
    connect(shortcut_toggle_right_docks, &QShortcut::activated, this, &MainWindow::toggleRightDockArea);
    
    this->dock_entity_map_legend->setVisible(false);
    this->dock_map_editor_guide->setVisible(false);
    connect(this->map_monitor, &MapMonitorContainer::signalShowMapLegendLink, this->dock_entity_map_legend, &EntityMapLegendDock::showMapLegendLink);
    connect(this->map_monitor, &MapMonitorContainer::signalShowMapLegendNode, this->dock_entity_map_legend, &EntityMapLegendDock::showMapLegendNode);
    connect(this->map_monitor, &MapMonitorContainer::signalShowMapLegendHeatmap, this->dock_entity_map_legend, &EntityMapLegendDock::showMapLegendHeatmap);
    connect(this->map_editor, &MapEditorContainer::signalMapEditorGuideVisibilityChanged, this->dock_map_editor_guide, &MapEditorGuideDock::setRequestedVisible);
    connect(this->dock_map_editor_guide, &MapEditorGuideDock::requestedVisibilityChanged, this->map_editor, &MapEditorContainer::setMapEditorGuideChecked);
    
    /*
    setStyleSheet(
        "QMainWindow::separator"
        "{"
        "    width: 0px;"
        "    height: 0px;"
        "}"
    );
    */
    
    this->map_mon = this->map_monitor->getMap();
    this->map_edit = this->map_editor->getMap();
    
    this->tabs->setContentsMargins(0, 0, 0, 0);
    
    this->setCentralWidget(this->tabs);
    
    this->footer = new FooterStatusBar(this);
    setStatusBar(this->footer->statusBar());
    
    // don't make it smaller, because that could lead to positioning issues on the canvas
    this->setMinimumHeight(700);
    this->setMinimumWidth(1222);
    
    this->tabs->setIconSize(QSize(40, 40));
    this->tabs->setTabPosition(QTabWidget::West);
    
    /*
    this->tabs->addTab(this->settings, QIcon(":/icon/settings.png"), "Settings");
    this->tabs->addTab(this->map_editor, QIcon(":/icon/map_edit.png"), "Map Editor");
    this->tabs->addTab(this->map_monitor, QIcon(":/icon/map_monitor.png"), "Map Monitor");
    this->tabs->addTab(this->energy, QIcon(":/icon/energy.png"), "Energy");
    
    this->tabs->addTab(this->reservoirs, QIcon(":/icon/reservoir.png"), "Reservoirs");
    this->tabs->addTab(this->tanks, QIcon(":/icon/tower.png"), "Tanks");
    this->tabs->addTab(this->pumps, QIcon(":/icon/pump.png"), "Pumps");
    this->tabs->addTab(this->valves, QIcon(":/icon/valve.png"), "Valves");
    this->tabs->addTab(this->junctions, QIcon(":/icon/junction.png"), "Junctions");
    
    this->tabs->addTab(this->pipes, QIcon(":/icon/pipe.png"), "Pipes");
    
    this->tabs->addTab(this->customerPoints, QIcon(":/icon/customer.png"), "Customer Points");
    this->tabs->addTab(this->customers, QIcon(":/icon/users.png"), "Customers");
    */
    
    this->tabs->addTab(new QWidget(this), rotatedIcon(":/icon/dashboard_global.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Dashboard");
    
    //this->tabs->addTab(new QWidget(this), rotatedIcon(":/icon/site_switcher.png"), "");
    //this->tabs->setTabToolTip(this->tabs->count() - 1, "Site Switcher");
    
    //this->tabs->addTab(new QWidget(this), rotatedIcon(":/icon/dashboard.png"), "");
    //this->tabs->setTabToolTip(this->tabs->count() - 1, "Site Dashboard");
    
    this->tabs->addTab(this->map_editor, rotatedIcon(":/icon/map_edit.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Map Editor");
    
    this->tabs->addTab(this->map_monitor, rotatedIcon(":/icon/map_monitor.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Map Monitor");
    
    this->tabs->addTab(this->energy, rotatedIcon(":/icon/energy.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Energy");
    
    this->tabs->addTab(this->reservoirs, rotatedIcon(":/icon/reservoir.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Reservoirs");
    
    this->tabs->addTab(this->tanks, rotatedIcon(":/icon/tower.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Tanks");
    
    this->tabs->addTab(this->pumps, rotatedIcon(":/icon/pump.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Pumps");
    
    this->tabs->addTab(this->valves, rotatedIcon(":/icon/valve.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Valves");
    
    this->tabs->addTab(this->junctions, rotatedIcon(":/icon/junction.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Junctions");
    
    this->tabs->addTab(this->pipes, rotatedIcon(":/icon/pipe.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Pipes");
    
    this->tabs->addTab(this->customerPoints, rotatedIcon(":/icon/customer.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Customer Points");
    
    this->tabs->addTab(this->customers, rotatedIcon(":/icon/users.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Customers");
    
    this->tab_spacer_tab_index = this->tabs->addTab(new QWidget(this->tabs), "");
    this->tabs->setTabEnabled(this->tab_spacer_tab_index, false);
    
    this->tabs->addTab(this->alarms, rotatedIcon(":/icon/alarm.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Alarms");
    
    this->tabs->addTab(this->logs, rotatedIcon(":/icon/log.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Logs");
    
    this->tabs->addTab(this->settings, rotatedIcon(":/icon/settings.png"), "");
    this->tabs->setTabToolTip(this->tabs->count() - 1, "Settings");
    
    this->tabs->setCurrentIndex(1);

    connect(this->tabs, &QTabWidget::currentChanged, this, [this](int)
    {
        this->updateMapEdgePanning();
        this->dock_entity_map_legend->setMapMonitorActive(this->tabs->currentWidget() == this->map_monitor);
        this->dock_map_editor_guide->setMapEditorActive(this->tabs->currentWidget() == this->map_editor);
    });
    this->dock_entity_map_legend->setMapMonitorActive(this->tabs->currentWidget() == this->map_monitor);
    this->dock_map_editor_guide->setMapEditorActive(this->tabs->currentWidget() == this->map_editor);
    
    QTimer::singleShot(0, this, &MainWindow::updateTabSpacer);
    
    connect(this->map_mon, &MapWidget::signalZoomChanged, this->footer, &FooterStatusBar::setMapZoom);
    connect(this->map_mon, &MapWidget::signalCoordsChangedWgs84, this->footer, &FooterStatusBar::setMapCoordinatesWGS84);
    connect(this->map_mon, &MapWidget::signalCoordsChangedUTM, this->footer, &FooterStatusBar::setMapCoordinatesUTM);
    
    connect(this->map_edit, &MapWidget::signalZoomChanged, this->footer, &FooterStatusBar::setMapZoom);
    connect(this->map_edit, &MapWidget::signalCoordsChangedWgs84, this->footer, &FooterStatusBar::setMapCoordinatesWGS84);
    connect(this->map_edit, &MapWidget::signalCoordsChangedUTM, this->footer, &FooterStatusBar::setMapCoordinatesUTM);
    
    MapNavigationWidget *map_edit_nav = this->map_editor->mapNavigationWidget();
    MapNavigationWidget *map_mon_nav = this->map_monitor->mapNavigationWidget();

    connect(this->map_model_editor, &MapModel::providerChanged, this->map_model_monitor, &MapModel::setProvider);
    connect(this->map_model_monitor, &MapModel::providerChanged, this->map_model_editor, &MapModel::setProvider);
    connect(this->map_model_editor, &MapModel::providerChanged, map_edit_nav, &MapNavigationWidget::mapProviderChange);
    connect(this->map_model_monitor, &MapModel::providerChanged, map_mon_nav, &MapNavigationWidget::mapProviderChange);

    connect(this->map_model_editor, &MapModel::zoomChanged, this, [this](int)
    {
        this->syncMapMovement(this->map_edit, this->map_mon);
    });
    connect(this->map_model_editor, &MapModel::centerChangedWGS84, this, [this](CoordinateWGS84)
    {
        this->syncMapMovement(this->map_edit, this->map_mon);
    });
    connect(this->map_model_monitor, &MapModel::zoomChanged, this, [this](int)
    {
        this->syncMapMovement(this->map_mon, this->map_edit);
    });
    connect(this->map_model_monitor, &MapModel::centerChangedWGS84, this, [this](CoordinateWGS84)
    {
        this->syncMapMovement(this->map_mon, this->map_edit);
    });

    connect(map_edit_nav, &MapNavigationWidget::signalSyncMapMovementStateChanged, this, [this, map_mon_nav](bool state)
    {
        this->sync_map_movement = state;
        map_mon_nav->mapMovementSyncStateChange(state);

        if (state)
            this->syncMapMovement(this->map_edit, this->map_mon);
    });
    connect(map_mon_nav, &MapNavigationWidget::signalSyncMapMovementStateChanged, this, [this, map_edit_nav](bool state)
    {
        this->sync_map_movement = state;
        map_edit_nav->mapMovementSyncStateChange(state);

        if (state)
            this->syncMapMovement(this->map_mon, this->map_edit);
    });
    
    connect(this->top_control_bar, &TopControlBar::signalHeadlossFormulaChanged, this->dock_entity_inspector, &EntityInspectorDock::onHeadlossFormulaChanged);
    connect(this->top_control_bar, &TopControlBar::signalSimulationStart, this->simulation_manager, &SimulationManager::run);
    connect(this->top_control_bar, &TopControlBar::signalShowEpanetLog, this->simulation_manager, &SimulationManager::showEpanetLog);
    connect(this->top_control_bar, &TopControlBar::signalExportEpanetNetwork, this->simulation_manager, &SimulationManager::exportEpanetNetwork);
    connect(this->top_control_bar, &TopControlBar::signalFullScreenToggle, this, &MainWindow::fullScreenToggle);

    connect(this->hydraulic_data, &HydraulicData::signalSelectedTank, this, &MainWindow::restoreRightDockAreaForMapEditorSelection);
    connect(this->hydraulic_data, &HydraulicData::signalSelectedReservoir, this, &MainWindow::restoreRightDockAreaForMapEditorSelection);
    connect(this->hydraulic_data, &HydraulicData::signalSelectedJunction, this, &MainWindow::restoreRightDockAreaForMapEditorSelection);
    connect(this->hydraulic_data, &HydraulicData::signalSelectedPipe, this, &MainWindow::restoreRightDockAreaForMapEditorSelection);
    connect(this->hydraulic_data, &HydraulicData::signalSelectedPump, this, &MainWindow::restoreRightDockAreaForMapEditorSelection);
    connect(this->hydraulic_data, &HydraulicData::signalSelectedValve, this, &MainWindow::restoreRightDockAreaForMapEditorSelection);
    connect(this->hydraulic_data, &HydraulicData::signalSelectedCustomerPoint, this, &MainWindow::restoreRightDockAreaForMapEditorSelection);

#ifdef Q_OS_WASM
    emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, EM_TRUE, &MainWindow::fullScreenChangeCallback);

    EmscriptenFullscreenChangeEvent fullscreen_status = {};
    if (emscripten_get_fullscreen_status(&fullscreen_status) == EMSCRIPTEN_RESULT_SUCCESS)
    {
        this->browser_fullscreen_active = fullscreen_status.isFullscreen != 0;
        this->top_control_bar->setFullScreenState(this->browser_fullscreen_active);
        this->updateMapEdgePanning();
    }
#endif
    
    #ifdef AOWIS_STANDALONE
    #else    
    checkServerMapInit();
    #endif
}


void MainWindow::syncMapMovement(MapWidget *source, MapWidget *target)
{
    if (!this->sync_map_movement || this->syncing_map_movement || !source || !target)
        return;

    MapModel *source_model = source->model();
    MapModel *target_model = target->model();

    if (!source_model || !target_model)
        return;

    this->syncing_map_movement = true;
    target_model->setView(
        source_model->centerLon(), source_model->centerLat(), source_model->zoom(), target->size());
    this->syncing_map_movement = false;
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
    
    QTabBar *bar = this->tabs->tabBar();
    
    int used_height = 0;
    
    for (int tab = 0; tab < this->tabs->count(); ++tab)
    {
        if (tab == this->tab_spacer_tab_index)
            continue;
        
        used_height += bar->tabRect(tab).height();
    }
    
    const int available_height = this->tabs->contentsRect().height();
    
    // Small universal breathing room. Usually 0-6 is enough.
    const int bottom_padding = 2;
    
    const int spacer_height = qMax(0, available_height - used_height - bottom_padding);
    
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

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    
    switch (key)
    {
    #ifndef Q_OS_WASM    
    case Qt::Key_F11:
        fullScreenToggle();
        event->accept();
        return;
    #endif
    
    #ifdef Q_OS_WASM
    case Qt::Key_F5:
        QMessageBox *box = new QMessageBox(this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        
        box->setIcon(QMessageBox::Question);
        box->setWindowTitle("Reload page");
        box->setText("Do you really want to reload this page?<br>You might loose unsaved inputs.");
        box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box->setDefaultButton(QMessageBox::No);
        
        connect(box, &QMessageBox::buttonClicked, this,
                [this, box, event](QAbstractButton *button)
                {
                    if (box->standardButton(button) == QMessageBox::Yes)
                    {
                        emscripten_run_script("window.location.reload();");
                    }
                });
        
        box->open();
        
        event->accept();
        return;
    #endif
    }
    
    QWidget::keyPressEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);

#ifndef Q_OS_WASM
    if (event->type() == QEvent::WindowStateChange)
    {
        QTimer::singleShot(0, this, [this]
        {
            this->top_control_bar->setFullScreenState(this->isFullScreen());
            this->updateMapEdgePanning();
        });
    }
#endif
}

void MainWindow::toggleRightDockArea()
{
    const QList<QDockWidget *> docks = findChildren<QDockWidget *>(QString(), Qt::FindDirectChildrenOnly);

    if (!this->right_dock_area_hidden)
    {
        this->right_dock_visibility.clear();
        this->right_dock_area_hidden = true;

        for (QDockWidget *dock : docks)
        {
            if (dock->isFloating() || dockWidgetArea(dock) != Qt::RightDockWidgetArea)
                continue;

            this->right_dock_visibility.insert(dock, dock->isVisible());
            hideRightDock(dock);
        }

        return;
    }

    this->right_dock_area_hidden = false;
    this->right_docks_being_hidden.clear();

    for (QDockWidget *dock : docks)
    {
        if (!this->right_dock_visibility.contains(dock) || dock->isFloating() || dockWidgetArea(dock) != Qt::RightDockWidgetArea)
            continue;

        dock->setVisible(this->right_dock_visibility.value(dock));
    }

    this->right_dock_visibility.clear();
}

void MainWindow::restoreRightDockAreaForMapEditorSelection()
{
    if (!this->right_dock_area_hidden || this->tabs->currentWidget() != this->map_editor)
        return;

    toggleRightDockArea();
}

void MainWindow::hideRightDock(QDockWidget *dock)
{
    if (dock == nullptr || !dock->isVisible())
        return;

    this->right_docks_being_hidden.insert(dock);
    dock->hide();
}

void MainWindow::onRightDockVisibilityChanged(QDockWidget *dock, bool visible)
{
    if (!this->right_dock_area_hidden || dock == nullptr || dock->isFloating() || dockWidgetArea(dock) != Qt::RightDockWidgetArea)
        return;

    if (!visible)
    {
        if (this->right_docks_being_hidden.remove(dock) == 0)
            this->right_dock_visibility.insert(dock, false);

        return;
    }

    this->right_dock_visibility.insert(dock, true);
    hideRightDock(dock);
}

void MainWindow::fullScreenToggle()
{
#ifdef Q_OS_WASM
    emscripten_run_script(R"JS(
        if (typeof toggleAowisFullscreen === 'function') {
            toggleAowisFullscreen();
        } else if (document.fullscreenElement) {
            document.exitFullscreen();
        } else {
            document.documentElement.requestFullscreen();
        }
    )JS");
#else
    const bool enter_fullscreen = !this->isFullScreen();

    if (enter_fullscreen)
    {
        this->window_state_saved = this->windowState();
        this->window_geometry_saved = this->geometry();
        this->showFullScreen();
    }
    else
    {
        this->showNormal();
        this->setGeometry(this->window_geometry_saved);
        this->setWindowState(this->window_state_saved);
    }

    this->top_control_bar->setFullScreenState(enter_fullscreen);
    this->updateMapEdgePanning();
#endif
}

#ifdef Q_OS_WASM
EM_BOOL MainWindow::fullScreenChangeCallback(int event_type, const EmscriptenFullscreenChangeEvent *event, void *user_data)
{
    Q_UNUSED(event_type);

    MainWindow *window = static_cast<MainWindow *>(user_data);

    if (window == nullptr || event == nullptr)
        return EM_FALSE;

    window->browser_fullscreen_active = event->isFullscreen != 0;
    window->top_control_bar->setFullScreenState(window->browser_fullscreen_active);
    window->updateMapEdgePanning();
    return EM_TRUE;
}
#endif

void MainWindow::updateMapEdgePanning()
{
    if (!this->map_mon || !this->map_edit || !this->tabs)
        return;

#ifdef Q_OS_WASM
    const bool fullscreen = this->browser_fullscreen_active;
#else
    const bool fullscreen = this->isFullScreen();
#endif

    this->map_edit->setEdgePanningEnabled(fullscreen && this->tabs->currentWidget() == this->map_editor);
    this->map_mon->setEdgePanningEnabled(fullscreen && this->tabs->currentWidget() == this->map_monitor);
}
