#pragma once

#include "tabs/map_workspace_mode.h"

#include <QList>
#include <QWidget>

class MapEditorContainer;
class MapMonitorContainer;
class MapWidget;
class MapModel;
class QStackedWidget;
class QTabBar;

// One main-navigation map page. Stage 1 defines the mode contract without
// changing ownership: distribution monitor/editor still own separate maps.
// Stage 2 extracts the tools; Stage 3 transfers surface ownership here.
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

    // Non-owning access to the active navigation model. Drainage modes have
    // no map yet; callers must handle nullptr. This is the MainWindow seam
    // for switching from separate viewports to one shared viewport later.
    MapWidget *activeMap() const;
    MapModel *activeModel() const;

public slots:
    void setCurrentMode(int index);

signals:
    void currentModeChanged(int index);

private:
    QWidget *createModeSelector(Mode mode);
    QWidget *createEmptyModePage(Mode mode);

    MapWidget *monitor_map = nullptr;
    MapWidget *editor_map = nullptr;
    QStackedWidget *pages = nullptr;
    QList<QTabBar *> mode_tabs;
};
