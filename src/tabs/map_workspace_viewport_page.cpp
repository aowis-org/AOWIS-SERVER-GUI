#include "tabs/map_workspace_viewport_page.h"

#include "tabs/map_mode_tool_panel.h"

#include <QHBoxLayout>
#include <QLayout>

MapWorkspaceViewportPage::MapWorkspaceViewportPage(QWidget *controller,
                                                   MapModeToolPanel *tools,
                                                   QWidget *viewport, QWidget *parent)
    : QWidget(parent), mode_controller(controller), mode_tools(tools),
      sidebar_widget(tools), map_viewport(viewport)
{
    Q_ASSERT(this->mode_controller != nullptr);
    Q_ASSERT(this->mode_tools != nullptr);
    Q_ASSERT(this->map_viewport != nullptr);
    Q_ASSERT(this->mode_tools != this->map_viewport);
    Q_ASSERT(this->mode_controller->isAncestorOf(this->mode_tools));
    Q_ASSERT(this->mode_controller->isAncestorOf(this->map_viewport));

    // Detach only the *outer* layout items. The map's QStackedLayout and
    // native bgfx window remain untouched. This happens before the first
    // workspace show / bgfx initialization, never during a mode switch.
    if (QLayout *old_layout = this->mode_controller->layout())
    {
        old_layout->removeWidget(this->mode_tools);
        old_layout->removeWidget(this->map_viewport);
    }

    // A hidden controller remains the event filter and signal receiver.
    // The page destroys it explicitly before the viewport at teardown.
    this->mode_controller->setParent(this);
    this->mode_controller->hide();

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(this->mode_tools);
    layout->addWidget(this->map_viewport, 1);

    // QWidget::setParent() hides a moved child. Mark both as shown while the
    // workspace is still hidden, so the first activation really displays
    // them without needing a renderer-specific show/restart workaround.
    this->mode_tools->show();
    this->map_viewport->show();
}

MapWorkspaceViewportPage::~MapWorkspaceViewportPage()
{
    // Controller cleanup clears callback resolvers on the shared viewport.
    // Do not rely on QObject child insertion/deletion ordering during close.
    delete this->mode_controller;
    this->mode_controller = nullptr;
}

void MapWorkspaceViewportPage::setSidebarWidget(QWidget *sidebar)
{
    Q_ASSERT(sidebar != nullptr);
    Q_ASSERT(sidebar != this->map_viewport);
    Q_ASSERT(sidebar->parentWidget() == this);
    this->sidebar_widget = sidebar;
}

void MapWorkspaceViewportPage::setMapFullscreen(bool fullscreen)
{
    Q_ASSERT(this->sidebar_widget != nullptr);
    this->sidebar_widget->setVisible(!fullscreen);
}

QWidget *MapWorkspaceViewportPage::controller() const
{
    return this->mode_controller;
}

MapModeToolPanel *MapWorkspaceViewportPage::toolPanel() const
{
    return this->mode_tools;
}

QWidget *MapWorkspaceViewportPage::viewport() const
{
    return this->map_viewport;
}
