#pragma once

#include <QList>
#include <QWidget>

class MapEditorContainer;
class MapMonitorContainer;
class QStackedWidget;
class QTabBar;

// One main-navigation map page, with independent distribution editor/monitor
// and future drainage pages. The mode tabs live above the left-hand tools;
// each existing map renderer keeps its original QWidget/native-window hierarchy.
class MapWorkspaceWidget final : public QWidget
{
    Q_OBJECT

public:
    enum Mode
    {
        DistributionEditor = 0,
        DistributionMonitor = 1,
        DrainageEditor = 2,
        DrainageMonitor = 3
    };
    Q_ENUM(Mode)

    explicit MapWorkspaceWidget(MapEditorContainer *editor,
                                MapMonitorContainer *monitor,
                                QWidget *parent = nullptr);

    Mode currentMode() const;
    bool isDistributionEditorActive() const;
    bool isDistributionMonitorActive() const;

public slots:
    void setCurrentMode(int index);

signals:
    void currentModeChanged(int index);

private:
    QWidget *createModeSelector(Mode mode);
    QWidget *createEmptyModePage(Mode mode);

    QStackedWidget *pages = nullptr;
    QList<QTabBar *> mode_tabs;
};
