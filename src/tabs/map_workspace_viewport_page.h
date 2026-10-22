#pragma once

#include <QWidget>

class MapModeToolPanel;

// Workspace-owned page for an existing map viewport and its independent tools.
// The hidden monitor controller owns input callbacks, but neither the mode
// tool panels nor the viewport are children of that controller.
//
// The controller is destroyed explicitly before QWidget tears down the
// viewport children, so cleanup callbacks never touch an already-deleted map.
class MapWorkspaceViewportPage final : public QWidget
{
public:
    MapWorkspaceViewportPage(QWidget *controller, MapModeToolPanel *tools,
                             QWidget *viewport, QWidget *parent = nullptr);
    ~MapWorkspaceViewportPage() override;

    QWidget *controller() const;
    MapModeToolPanel *toolPanel() const;
    QWidget *viewport() const;

    // The outer sidebar widget may be a QStackedWidget containing multiple
    // tool panels. Hide that container, not only its selected panel, so the
    // map receives the full width during fullscreen without reparenting.
    void setSidebarWidget(QWidget *sidebar);
    void setMapFullscreen(bool fullscreen);

private:
    QWidget *mode_controller = nullptr;
    MapModeToolPanel *mode_tools = nullptr;
    QWidget *sidebar_widget = nullptr;
    QWidget *map_viewport = nullptr;
};
