#pragma once

#include "tabs/map_workspace_mode.h"

#include <QList>
#include <QWidget>

class MapModeToolPanel;
class MapEditorContainer;
class MapMonitorContainer;
class MapWidget;
class MapModel;
class QStackedWidget;
class QTabBar;

// One main-navigation map page. Stage 3 hosts the monitor's map stack and
// navigation model in this workspace. The editor retains its legacy viewport
// until its interaction controller is migrated in Stage 4.
class MapWorkspaceWidget final : public QWidget
{
    Q_OBJECT

public:
    enum Mode
    {
        DistributionMonitor = 0,
        DistributionEditor = 1,
        DrainageMonitor = 2,
        DrainageEditor = 3
    };
    Q_ENUM(Mode)
    // Keep Qt's meta-enum numeric values aligned with the independent mode
    // contract without asking moc to evaluate namespace constexpr symbols.
    static_assert(DistributionMonitor == MapWorkspace::DistributionMonitor);
    static_assert(DistributionEditor == MapWorkspace::DistributionEditor);
    static_assert(DrainageMonitor == MapWorkspace::DrainageMonitor);
    static_assert(DrainageEditor == MapWorkspace::DrainageEditor);

    explicit MapWorkspaceWidget(MapEditorContainer *editor,
                                MapMonitorContainer *monitor,
                                QWidget *parent = nullptr);

    Mode currentMode() const;
    bool isDistributionEditorActive() const;
    bool isDistributionMonitorActive() const;

    // Non-owning access to the active navigation model. The workspace owns
    // the monitor model and viewport; the editor's model/viewport remain
    // temporary legacy instances. Drainage modes return nullptr.
    MapWidget *activeMap() const;
    MapModel *activeModel() const;
    // Tools are independent of their map renderer. The workspace directly
    // hosts monitor controls; the legacy editor page still hosts its controls.
    MapModeToolPanel *activeToolPanel() const;


public slots:
    void setCurrentMode(int index);

signals:
    void currentModeChanged(int index);

private:
    QWidget *createModeSelector(Mode mode);
    QWidget *createEmptyModePage(Mode mode);

    MapWidget *monitor_map = nullptr;
    MapWidget *editor_map = nullptr;
    MapModeToolPanel *monitor_tools = nullptr;
    MapModeToolPanel *editor_tools = nullptr;
    QStackedWidget *pages = nullptr;
    QList<QTabBar *> mode_tabs;
};
