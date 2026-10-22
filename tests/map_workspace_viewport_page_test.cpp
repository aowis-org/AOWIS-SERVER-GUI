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
