#include "main_window.h"
#include "gui_configuration.h"
#include "map_server_client_configuration.h"
#include <QPushButton>
#include <QShortcut>
#include <qapplication.h>

#include <optional>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    hydraulic_data(new HydraulicData(this)),
    gps( new GpsProvider(this) ),
    dock_entity_inspector( new EntityInspectorDock(hydraulic_data, this) ),
    dock_entity_map_legend( new EntityMapLegendDock(hydraulic_data, this)),
    dock_map_editor_guide( new MapEditorGuideDock(this) ),
    top_control_bar( new TopControlBar(this) ),
    map_tile_repository( new MapTileRepository(this) ),
    map_terrain_repository( new MapTerrainRepository(this) ),
    map_model_monitor( new MapModel(this) ),
    map_model_editor( new MapModel(this) ),
    settings( new SettingsWidget(this) ),
    map_monitor( new MapMonitorContainer(this->map_model_monitor, this->map_tile_repository,
                                         this->map_terrain_repository, this->hydraulic_data,
                                         this->gps, this) ),
    map_editor( new MapEditorContainer(this->map_model_editor, this->map_tile_repository, this->hydraulic_data, this->gps, this->dock_entity_inspector, this) ),
    energy( new EnergyWidget(this) ),
    reservoirs( new ReservoirsWidget(this->hydraulic_data, this) ),
    tanks( new TanksWidget(this->hydraulic_data, this) ),
    pumps( new PumpsWidget(this->hydraulic_data, this) ),
    valves( new ValvesWidget(this->hydraulic_data, this) ),
    junctions( new JunctionsWidget(this->hydraulic_data, this) ),
    pipes( new PipesWidget(this->hydraulic_data, this) ),
    customerPoints( new CustomerPointsWidget(this) ),
    customers( new CustomersWidget(this) ),
    logs( new LogsWidget(this) ),
    alarms( new AlarmsWidget(this) ),
    simulation_manager( new SimulationManager(hydraulic_data, this) )
{
    this->main_navigation = new MainNavigationWidget(this);

    #ifdef AOWIS_STANDALONE
    setWindowTitle("AOWIS Controller [Standalone]");
    #else
    setWindowTitle("AOWIS Controller");
    #endif
    
    #ifdef Q_OS_LINUX
    showMaximized();
    #endif
    
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
            scheduleRightDockResize();
        });
        connect(dock, &QDockWidget::topLevelChanged, this, [this](bool)
        {
            scheduleRightDockResize();
        });
        connect(dock, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea)
        {
            scheduleRightDockResize();
        });
    }

    connect(this->dock_entity_map_legend, &EntityMapLegendDock::signalDockHeightPreferredChanged, this, [this](int)
    {
        scheduleRightDockResize();
    });

    QShortcut *shortcut_toggle_right_docks = new QShortcut(
        guiShortcutKeySequence(guiConfiguration().shortcuts.sidebar_toggle), this);
    shortcut_toggle_right_docks->setContext(Qt::WindowShortcut);
    shortcut_toggle_right_docks->setAutoRepeat(false);
    connect(shortcut_toggle_right_docks, &QShortcut::activated, this, &MainWindow::toggleRightDockArea);
    
    this->dock_entity_map_legend->setVisible(false);
    this->dock_map_editor_guide->setVisible(false);
    scheduleRightDockResize();
    connect(this->map_monitor, &MapMonitorContainer::signalShowMapLegendLink, this, [this](VisualLink visual_link)
    {
        if (visual_link != VisualLink::None && this->right_dock_area_hidden && this->main_navigation->currentWidget() == this->map_monitor)
            toggleRightDockArea();

        this->dock_entity_map_legend->showMapLegendLink(visual_link);
    });
    connect(this->map_monitor, &MapMonitorContainer::signalShowMapLegendNode, this, [this](VisualNode visual_node)
    {
        if (visual_node != VisualNode::None && this->right_dock_area_hidden && this->main_navigation->currentWidget() == this->map_monitor)
            toggleRightDockArea();

        this->dock_entity_map_legend->showMapLegendNode(visual_node);
    });
    connect(this->map_monitor, &MapMonitorContainer::signalShowMapLegendHeatmap, this, [this](VisualHeatmap visual_heatmap)
    {
        if (visual_heatmap != VisualHeatmap::None && this->right_dock_area_hidden && this->main_navigation->currentWidget() == this->map_monitor)
            toggleRightDockArea();

        this->dock_entity_map_legend->showMapLegendHeatmap(visual_heatmap);
    });
    connect(this->map_editor, &MapEditorContainer::signalMapEditorGuideVisibilityChanged, this, [this](bool visible)
    {
        if (visible && this->right_dock_area_hidden)
        {
            // Restore first so the guide is not immediately suppressed as a shortcut-hidden dock.
            toggleRightDockArea();
        }

        this->dock_map_editor_guide->setRequestedVisible(visible);
    });
    connect(this->map_editor, &MapEditorContainer::signalEditNetworkSectionActive, this->dock_map_editor_guide, &MapEditorGuideDock::setEditNetworkSectionActive);
    connect(this->dock_map_editor_guide, &MapEditorGuideDock::requestedVisibilityChanged, this->map_editor, &MapEditorContainer::setMapEditorGuideChecked);
    this->dock_map_editor_guide->setEditNetworkSectionActive(this->map_editor->isEditNetworkSectionActive());
    
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
    
    this->main_navigation->setContentsMargins(0, 0, 0, 0);
    
    setCentralWidget(this->main_navigation);
    
    this->footer = new FooterStatusBar(this);
    setStatusBar(this->footer->statusBar());
    
    // don't make it smaller, because that could lead to positioning issues on the canvas
    this->setMinimumHeight(700);
    this->setMinimumWidth(1222);
    
    this->main_navigation->addPage(new QWidget(this->main_navigation),
                                  QIcon(":/icon/dashboard_global.png"),
                                  "Dashboard");

    //this->main_navigation->addPage(new QWidget(this->main_navigation),
    //                              QIcon(":/icon/site_switcher.png"),
    //                              "Site Switcher");

    //this->main_navigation->addPage(new QWidget(this->main_navigation),
    //                              QIcon(":/icon/dashboard.png"),
    //                              "Site Dashboard");

    this->main_navigation->addPage(this->map_editor,
                                  QIcon(":/icon/map_edit.png"),
                                  "Map Editor");
    this->main_navigation->addPage(this->map_monitor,
                                  QIcon(":/icon/map_monitor.png"),
                                  "Map Monitor");
    this->main_navigation->addPage(this->energy,
                                  QIcon(":/icon/energy.png"),
                                  "Energy");
    this->main_navigation->addPage(this->reservoirs,
                                  QIcon(":/icon/reservoir.png"),
                                  "Reservoirs");
    this->main_navigation->addPage(this->tanks,
                                  QIcon(":/icon/tower.png"),
                                  "Tanks");
    this->main_navigation->addPage(this->pumps,
                                  QIcon(":/icon/pump.png"),
                                  "Pumps");
    this->main_navigation->addPage(this->valves,
                                  QIcon(":/icon/valve.png"),
                                  "Valves");
    this->main_navigation->addPage(this->junctions,
                                  QIcon(":/icon/junction.png"),
                                  "Junctions");
    this->main_navigation->addPage(this->pipes,
                                  QIcon(":/icon/pipe.png"),
                                  "Pipes");
    this->main_navigation->addPage(this->customerPoints,
                                  QIcon(":/icon/customer.png"),
                                  "Customer Points");
    this->main_navigation->addPage(this->customers,
                                  QIcon(":/icon/users.png"),
                                  "Customers");

    this->main_navigation->addPage(this->alarms,
                                  QIcon(":/icon/alarm.png"),
                                  "Alarms",
                                  MainNavigationWidget::Placement::Bottom);
    this->main_navigation->addPage(this->logs,
                                  QIcon(":/icon/log.png"),
                                  "Logs",
                                  MainNavigationWidget::Placement::Bottom);
    this->main_navigation->addPage(this->settings,
                                  QIcon(":/icon/settings.png"),
                                  "Settings",
                                  MainNavigationWidget::Placement::Bottom);

    this->main_navigation->setCurrentIndex(2);

    connect(this->main_navigation, &MainNavigationWidget::currentChanged, this, [this](int)
    {
        updateMapEdgePanning();
        this->dock_entity_map_legend->setMapMonitorActive(this->main_navigation->currentWidget() == this->map_monitor);
        this->dock_map_editor_guide->setMapEditorActive(this->main_navigation->currentWidget() == this->map_editor);
    });
    this->dock_entity_map_legend->setMapMonitorActive(this->main_navigation->currentWidget() == this->map_monitor);
    this->dock_map_editor_guide->setMapEditorActive(this->main_navigation->currentWidget() == this->map_editor);
    
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
    connect(this->map_model_editor, &MapModel::view2dContinuousScaleChanged,
            this, [this](double)
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
    connect(this->map_model_monitor, &MapModel::view2dContinuousScaleChanged,
            this, [this](double)
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
    connect(this->top_control_bar, &TopControlBar::signalHeadlossFormulaChanged, this, [this](HeadlossFormulas)
    {
        this->hydraulic_data->setSimulationHeadlossFormula(
            this->top_control_bar->selectedSimulationHeadlossFormula());
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, [this]
    {
        this->top_control_bar->setSelectedSimulationHeadlossFormula(
            this->hydraulic_data->networkHydraulic().options_hydraulic.headloss_formula);
    });
    connect(this->top_control_bar, &TopControlBar::signalSimulationStart, this, [this]
    {
        this->simulation_manager->runOrStop(
            this->top_control_bar->selectedSimulationQualityAnalyses());
    });
    connect(this->simulation_manager, &SimulationManager::signalSimulationStarted, this->top_control_bar, &TopControlBar::setSimulationRunRunningIcon);
    connect(this->simulation_manager, &SimulationManager::signalSimulationStopRequested, this->top_control_bar, &TopControlBar::setSimulationRunStoppingIcon);
    connect(this->simulation_manager, &SimulationManager::signalSimulationFinished, this, [this](bool cancelled)
    {
        if (cancelled)
            this->top_control_bar->setSimulationRunCancelledIcon();
    });
    connect(this->top_control_bar, &TopControlBar::signalShowSimulationStatistics, this->simulation_manager, &SimulationManager::showSimulationStatistics);
    connect(this->top_control_bar, &TopControlBar::signalShowEpanetLog, this->simulation_manager, &SimulationManager::showEpanetLog);
#ifdef Q_OS_WASM
    connect(
        this->top_control_bar,
        &TopControlBar::signalImportProject,
        this->simulation_manager,
        &SimulationManager::importEpanetNetwork,
        Qt::DirectConnection);
#else
    connect(this->top_control_bar, &TopControlBar::signalImportProject, this->simulation_manager, &SimulationManager::importEpanetNetwork);
#endif
    connect(
        this->top_control_bar,
        &TopControlBar::signalBuiltinRevisionActivationRequested,
        this->simulation_manager,
        &SimulationManager::importEpanetNetworkResource);
    connect(this->top_control_bar, &TopControlBar::signalExportEpanetNetwork, this->simulation_manager, &SimulationManager::exportEpanetNetwork);
    connect(this->simulation_manager, &SimulationManager::signalEpanetNetworkImported, this, [this]
    {
        QList<WaterQualityAnalysisType> analyses;
        for (const WaterQualitySolverOptions &quality_options : this->hydraulic_data->simulationQualityRunOptions())
        {
            if (quality_options.analysis != WaterQualityAnalysisType::None
                && !analyses.contains(quality_options.analysis))
            {
                analyses.append(quality_options.analysis);
            }
        }
        this->top_control_bar->setSelectedSimulationQualityAnalyses(analyses);
    });
    connect(this->simulation_manager, &SimulationManager::signalEpanetNetworkImported,
            this->top_control_bar, &TopControlBar::signalShowNetworkOnMap);
    connect(this->top_control_bar, &TopControlBar::signalShowNetworkOnMap, this, [this]
    {
        if (!this->hydraulic_data->boundingBoxWgs84Valid())
            return;

        const CoordinateWGS84 &minimum = this->hydraulic_data->boundingBoxWgs84Minimum();
        const CoordinateWGS84 &maximum = this->hydraulic_data->boundingBoxWgs84Maximum();
        const double center_latitude = (minimum.latitude_deg + maximum.latitude_deg) / 2.0;
        const double center_longitude = (minimum.longitude_deg + maximum.longitude_deg) / 2.0;

        if (this->map_model_editor != nullptr)
            this->map_model_editor->setCenter(center_longitude, center_latitude,
                                              this->map_edit != nullptr ? this->map_edit->size() : QSize());

        if (this->map_model_monitor != nullptr)
            this->map_model_monitor->setCenter(center_longitude, center_latitude,
                                               this->map_mon != nullptr ? this->map_mon->size() : QSize());
    });
    connect(this->hydraulic_data, &HydraulicData::signalSimulationResultTimelineChanged, this, [this](bool available)
    {
        this->top_control_bar->setSimulationResultsAvailable(available);

        const std::optional<HydraulicSimulationResultTimeline> &result_timeline =
            this->hydraulic_data->simulationResultTimeline();

        if (!result_timeline.has_value() || this->hydraulic_data->simulationDiagnosticsStale())
            this->top_control_bar->resetSimulationRunIcon();
        else
            this->top_control_bar->setSimulationRunResultIcon(result_timeline.value());

        if (!available || !result_timeline.has_value())
        {
            this->top_control_bar->clearSimulationResultTimeline();
            return;
        }

        this->top_control_bar->setSimulationResultTimeline(result_timeline.value());
    });
    connect(this->hydraulic_data, &HydraulicData::signalCurrentSimulationResultChanged,
            this->top_control_bar, &TopControlBar::setCurrentSimulationResultIndex);
    connect(this->top_control_bar, &TopControlBar::signalSimulationResultIndexSelected, this, [this](int result_index)
    {
        this->hydraulic_data->setCurrentSimulationResultIndex(result_index);
    });
    connect(this->simulation_manager, &SimulationManager::signalEpanetLogAvailabilityChanged,
            this->top_control_bar, &TopControlBar::setEpanetLogAvailable);
    connect(this->top_control_bar, &TopControlBar::signalFullScreenToggle, this, &MainWindow::fullScreenToggle);

    connect(this->hydraulic_data, &HydraulicData::signalSelectedTank, this, &MainWindow::showEntityInspectorForSelection);
    connect(this->hydraulic_data, &HydraulicData::signalSelectedReservoir, this, &MainWindow::showEntityInspectorForSelection);
    connect(this->hydraulic_data, &HydraulicData::signalSelectedJunction, this, &MainWindow::showEntityInspectorForSelection);
    connect(this->hydraulic_data, &HydraulicData::signalSelectedPipe, this, &MainWindow::showEntityInspectorForSelection);
    connect(this->hydraulic_data, &HydraulicData::signalSelectedPump, this, &MainWindow::showEntityInspectorForSelection);
    connect(this->hydraulic_data, &HydraulicData::signalSelectedValve, this, &MainWindow::showEntityInspectorForSelection);
    connect(this->hydraulic_data, &HydraulicData::signalSelectedCustomerPoint, this, &MainWindow::showEntityInspectorForSelection);

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
    if (source_model->viewMode() == MapViewMode::TwoD)
        target_model->setView2dContinuousZoom(source_model->view2dContinuousZoom(), target->size());
    this->syncing_map_movement = false;
}

void MainWindow::checkServerMapInit()
{
    this->time_server_map_success_last = QDateTime::currentDateTime().addSecs(-60);
    this->footer->statusUpdateServerMap(StatusColorCode::Yellow);

    const MapServerClientConfiguration &configuration = mapServerClientConfiguration();
    qInfo() << "Using configured map server URL:" << configuration.base_url;
    this->rest_check_map = new RESTClient(configuration.base_url, configuration.api_key,
                                          configuration.delete_api_key, this);
    connect(this->rest_check_map, &RESTClient::requestFinished, this, [this](const QByteArray &data)
    {
        this->checking_server_map = false;

        if (!data.contains("AOWIS map server"))
        {
            qWarning() << "Configured map server did not return the expected /status response:"
                       << data.left(512);
            return;
        }

        this->time_server_map_success_last = QDateTime::currentDateTime();
    });
    connect(this->rest_check_map, &RESTClient::requestError, this, [this](const QString &error)
    {
        this->checking_server_map = false;
        qWarning() << "Map server status request failed:" << error;
    });

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
    scheduleRightDockResize();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
#ifndef Q_OS_WASM
    if (guiShortcutMatches(event, guiConfiguration().shortcuts.fullscreen))
    {
        fullScreenToggle();
        event->accept();
        return;
    }
#endif

#ifdef Q_OS_WASM
    switch (event->key())
    {
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
    }
#endif

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

        bool visible = this->right_dock_visibility.value(dock);
        if (dock == this->dock_map_editor_guide)
            visible = visible && this->dock_map_editor_guide->shouldBeVisible();

        dock->setVisible(visible);
    }

    this->right_dock_visibility.clear();
    scheduleRightDockResize();
}

void MainWindow::showEntityInspectorForSelection()
{
    if (this->right_dock_area_hidden)
        toggleRightDockArea();

    this->dock_entity_inspector->show();
    this->dock_entity_inspector->raise();
    scheduleRightDockResize();
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

void MainWindow::scheduleRightDockResize()
{
    if (this->right_dock_resize_pending)
        return;

    this->right_dock_resize_pending = true;
    QTimer::singleShot(0, this, [this]
    {
        this->right_dock_resize_pending = false;
        resizeRightDocks();
    });
}

void MainWindow::resizeRightDocks()
{
    if (this->right_dock_area_hidden)
        return;

    const int available_height = this->centralWidget() != nullptr ? this->centralWidget()->height() : this->height();
    if (available_height <= 0)
        return;

    const bool inspector_visible = this->dock_entity_inspector->isVisible() && !this->dock_entity_inspector->isFloating() &&
                                   dockWidgetArea(this->dock_entity_inspector) == Qt::RightDockWidgetArea;
    const bool legend_visible = this->dock_entity_map_legend->isVisible() && !this->dock_entity_map_legend->isFloating() &&
                                dockWidgetArea(this->dock_entity_map_legend) == Qt::RightDockWidgetArea;
    const bool guide_visible = this->dock_map_editor_guide->isVisible() && !this->dock_map_editor_guide->isFloating() &&
                               dockWidgetArea(this->dock_map_editor_guide) == Qt::RightDockWidgetArea;

    const int visible_count = static_cast<int>(inspector_visible) + static_cast<int>(legend_visible) + static_cast<int>(guide_visible);
    if (visible_count == 0)
        return;

    int height_remaining = available_height;
    int height_legend = 0;
    int height_guide = 0;
    int height_inspector = 0;

    if (legend_visible)
    {
        const int height_reserved_for_other_docks = qMax(0, visible_count - 1);
        height_legend = qBound(1, this->dock_entity_map_legend->dockHeightPreferred(), qMax(1, available_height - height_reserved_for_other_docks));
        height_remaining = qMax(0, height_remaining - height_legend);
    }

    if (guide_visible)
    {
        if (inspector_visible)
            height_guide = qMax(1, height_remaining / 4);
        else
            height_guide = qMax(1, height_remaining);

        height_remaining = qMax(0, height_remaining - height_guide);
    }

    if (inspector_visible)
        height_inspector = qMax(1, height_remaining);

    QList<QDockWidget *> docks_to_resize;
    QList<int> dock_heights;
    if (inspector_visible)
    {
        docks_to_resize.append(this->dock_entity_inspector);
        dock_heights.append(height_inspector);
    }

    if (legend_visible)
    {
        docks_to_resize.append(this->dock_entity_map_legend);
        dock_heights.append(height_legend);
    }

    if (guide_visible)
    {
        docks_to_resize.append(this->dock_map_editor_guide);
        dock_heights.append(height_guide);
    }

    if (docks_to_resize.size() < 2)
        return;

    resizeDocks(docks_to_resize, dock_heights, Qt::Vertical);
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
    if (!this->map_mon || !this->map_edit || !this->main_navigation)
        return;

#ifdef Q_OS_WASM
    const bool fullscreen = this->browser_fullscreen_active;
#else
    const bool fullscreen = this->isFullScreen();
#endif

    this->map_edit->setEdgePanningEnabled(fullscreen && this->main_navigation->currentWidget() == this->map_editor);
    this->map_mon->setEdgePanningEnabled(fullscreen && this->main_navigation->currentWidget() == this->map_monitor);
}
