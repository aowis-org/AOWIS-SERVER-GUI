#pragma once

#include <QWidget>

class MapModeToolPanel;

// Workspace-owned page for an existing map viewport and its independent tools.
// During migration the controller still owns all its signal/interaction logic,
// but does not own the tool panel, viewport or renderer widget hierarchy.
//
// The controller is parented FIRST: QWidget destroys children in insertion
// order, so its destructor can clear callbacks into the still-live viewport.
// Do not create the controller's parent/viewport relationship in reverse.
class MapWorkspaceViewportPage final : public QWidget
{
public:
    MapWorkspaceViewportPage(QWidget *controller, MapModeToolPanel *tools,
                             QWidget *viewport, QWidget *parent = nullptr);

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
