#include "tabs/map_mode_tool_panel.h"
#include "tabs/map_workspace_viewport_page.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

namespace
{
int destructor_errors = 0;
int destructor_calls = 0;

bool require(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "map_workspace_viewport_page: %s\n", message);
    return condition;
}

class LegacyMonitorController final : public QWidget
{
public:
    LegacyMonitorController(QWidget *map, QWidget *parent)
        : QWidget(parent), map_widget(map)
    {
    }

    ~LegacyMonitorController() override
    {
        ++destructor_calls;
        // Models and renderer callbacks are still accessed by the old
        // controller's destructor. The viewport MUST be alive here.
        if (this->map_widget.isNull())
            ++destructor_errors;
    }

private:
    QPointer<QWidget> map_widget;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget root;
    QPointer<QWidget> viewport_pointer;
    QPointer<MapModeToolPanel> tool_pointer;
    {
        QWidget *viewport = new QWidget();
        viewport->setObjectName(QStringLiteral("testMonitorViewport"));
        LegacyMonitorController *legacy = new LegacyMonitorController(viewport, &root);
        viewport->setParent(legacy);
        // The legacy layout starts by owning both sidebar and viewport.
        QHBoxLayout *legacy_layout = new QHBoxLayout(legacy);
        MapModeToolPanel *tools = new MapModeToolPanel(new QLabel(QStringLiteral("tools")),
            220, MapModeToolPanel::ScrollbarWidthPolicy::Fixed, false, legacy);
        legacy_layout->addWidget(tools);
        legacy_layout->addWidget(viewport);

        MapWorkspaceViewportPage *page = new MapWorkspaceViewportPage(
            legacy, tools, viewport, &root);
        QVBoxLayout *root_layout = new QVBoxLayout(&root);
        root_layout->addWidget(page);
        viewport_pointer = viewport;
        tool_pointer = tools;

        if (!require(page->controller() == legacy, "controller identity changed")
            || !require(page->toolPanel() == tools, "tools identity changed")
            || !require(page->viewport() == viewport, "viewport identity changed")
            || !require(legacy->parentWidget() == page, "legacy controller still owns the page")
            || !require(!legacy->isVisible(), "detached controller should stay hidden")
            || !require(tools->parentWidget() == page && viewport->parentWidget() == page,
                        "workspace must own the viewport and controls")
            || !require(!legacy->isAncestorOf(tools) && !legacy->isAncestorOf(viewport),
                        "legacy controller still owns viewport or tools")
            || !require(legacy_layout->count() == 0, "old layout retained migrated widgets"))
            return 1;

        root.show();
        app.processEvents();
        if (!require(tools->isVisible() && viewport->isVisible(),
                     "migrated controls or viewport did not show"))
            return 1;
        tools->setMapFullscreen(true);
        if (!require(!tools->isVisible() && viewport->isVisible(),
                     "fullscreen must hide only sidebar"))
            return 1;
        tools->setMapFullscreen(false);
        if (!require(tools->isVisible(), "sidebar did not return from fullscreen"))
            return 1;

        // Stage 4: changing distribution mode moves only the tools; the
        // viewport remains attached and exposed (no native GPU restart).
        QStackedWidget *mode_tools = new QStackedWidget(page);
        page->layout()->removeWidget(tools);
        MapModeToolPanel *editor_tools = new MapModeToolPanel(
            new QLabel(QStringLiteral("Editor tools")), 220,
            MapModeToolPanel::ScrollbarWidthPolicy::Fixed, false, mode_tools);
        mode_tools->addWidget(tools);
        mode_tools->addWidget(editor_tools);
        static_cast<QHBoxLayout *>(page->layout())->insertWidget(0, mode_tools);
        mode_tools->setCurrentIndex(1);
        app.processEvents();
        if (!require(viewport->isVisible() && editor_tools->isVisible()
                        && !tools->isVisible(),
                     "switching to editor should affect only the tool panel"))
            return 1;
        mode_tools->setCurrentIndex(0);
        app.processEvents();
        if (!require(viewport->isVisible() && tools->isVisible()
                        && !editor_tools->isVisible(),
                     "switching back to monitor must keep viewport exposed"))
            return 1;

        // Stage 7: four modes select four independent sidebars, but the
        // native viewport never changes parent, identity, or visibility.
        QWidget *drain_monitor_tools = new QWidget(mode_tools);
        QWidget *drain_editor_tools = new QWidget(mode_tools);
        QVBoxLayout *drain_monitor_layout = new QVBoxLayout(drain_monitor_tools);
        drain_monitor_layout->addWidget(new QLabel(QStringLiteral("Drainage Monitor")));
        QVBoxLayout *drain_editor_layout = new QVBoxLayout(drain_editor_tools);
        drain_editor_layout->addWidget(new QLabel(QStringLiteral("Drainage Editor")));
        mode_tools->addWidget(drain_monitor_tools);
        mode_tools->addWidget(drain_editor_tools);
        for (int index = 0; index < 4; ++index)
        {
            mode_tools->setCurrentIndex(index);
            app.processEvents();
            if (!require(viewport->isVisible() && viewport->parentWidget() == page
                            && page->viewport() == viewport
                            && mode_tools->currentWidget()->isVisible(),
                         "all four map modes must retain the same visible viewport"))
                return 1;
        }
        mode_tools->setCurrentIndex(0);
        app.processEvents();

        // Stage 6: fullscreen hides the ENTIRE sidebar stack. Hiding only
        // its selected panel leaves a grey strip where QStackedWidget keeps
        // reserving its maximum width. Mode switches must not reveal it.
        page->setSidebarWidget(mode_tools);
        root.resize(900, 500);
        app.processEvents();
        const int windowed_viewport_width = viewport->width();
        page->setMapFullscreen(true);
        mode_tools->setCurrentIndex(1);
        app.processEvents();
        if (!require(viewport->isVisible() && !mode_tools->isVisible()
                        && !tools->isVisible() && !editor_tools->isVisible(),
                     "fullscreen editor switch exposed a sidebar or hid viewport")
            || !require(viewport->width() > windowed_viewport_width + 100,
                     "fullscreen did not release the sidebar width to the map"))
            return 1;
        mode_tools->setCurrentIndex(0);
        app.processEvents();
        if (!require(viewport->isVisible() && !mode_tools->isVisible()
                        && !tools->isVisible() && !editor_tools->isVisible(),
                     "fullscreen monitor switch exposed a sidebar or hid viewport"))
            return 1;
        page->setMapFullscreen(false);
        app.processEvents();
        if (!require(viewport->isVisible() && mode_tools->isVisible()
                        && tools->isVisible() && !editor_tools->isVisible(),
                     "leaving fullscreen failed to restore the active sidebar")
            || !require(viewport->width() < windowed_viewport_width + 20,
                     "leaving fullscreen did not restore the sidebar layout width"))
            return 1;

        delete page;
    }
    if (!require(destructor_calls == 1 && destructor_errors == 0,
                 "controller must be destroyed before the map viewport")
        || !require(viewport_pointer.isNull() && tool_pointer.isNull(),
                    "viewport or tools were not destroyed with their workspace page"))
        return 1;

    std::puts("Map workspace viewport ownership, fullscreen and destruction order passed");
    return 0;
}
