#include "tabs/map_mode_tool_panel.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <cstdio>

namespace
{
bool require(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "map_mode_tool_panel: %s\n", message);
    return condition;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget container;
    QWidget *viewport = new QWidget(&container);
    QHBoxLayout *layout = new QHBoxLayout(&container);
    QWidget *monitor_controls = new QWidget();
    QWidget *editor_controls = new QWidget();
    MapModeToolPanel monitor(monitor_controls, 220,
        MapModeToolPanel::ScrollbarWidthPolicy::IncludeVisibleScrollbar, false, &container);
    MapModeToolPanel editor(editor_controls, 240,
        MapModeToolPanel::ScrollbarWidthPolicy::Fixed, true, &container);
    layout->addWidget(&monitor);
    layout->addWidget(&editor);
    layout->addWidget(viewport);
    QWidget *monitor_selector = new QLabel(QStringLiteral("Distribution Monitor"));
    QWidget *editor_selector = new QLabel(QStringLiteral("Distribution Editor"));
    monitor.setModeSelector(monitor_selector);
    editor.setModeSelector(editor_selector);

    // QScrollArea may evaluate its hidden viewport before the first show;
    // force the no-scrollbar state for a deterministic initial-width check.
    monitor.scrollArea()->verticalScrollBar()->setRange(0, 0);
    if (!require(monitor.controls() == monitor_controls, "monitor controls changed")
        || !require(editor.controls() == editor_controls, "editor controls changed")
        || !require(monitor.modeSelector() == monitor_selector, "monitor selector missing")
        || !require(editor.modeSelector() == editor_selector, "editor selector missing")
        || !require(monitor.scrollArea()->widget() == monitor_controls,
                    "monitor controls are not in scroll area")
        || !require(editor.scrollArea()->widget() == editor_controls,
                    "editor controls are not in scroll area")
        || !require(monitor_selector->parentWidget() == &monitor,
                    "mode selector is not a fixed panel child")
        || !require(editor_selector->parentWidget() == &editor,
                    "editor selector is not a fixed panel child")
        || !require(monitor.parentWidget() == &container
                    && editor.parentWidget() == &container
                    && viewport->parentWidget() == &container,
                    "panels and viewport must be siblings")
        || !require(!viewport->isAncestorOf(&monitor) && !viewport->isAncestorOf(&editor),
                    "map viewport owns a tool panel")
        || !require(monitor.scrollArea()->minimumWidth() == 220,
                    "monitor initial width differs")
        || !require(editor.scrollArea()->minimumWidth() == 240,
                    "editor initial width differs"))
        return 1;

    // A scrollbar changes the monitor width, but not the editor width.
    monitor.scrollArea()->verticalScrollBar()->setRange(0, 100);
    editor.scrollArea()->verticalScrollBar()->setRange(0, 100);
    if (!require(monitor.scrollArea()->minimumWidth() > 220,
                 "monitor must reserve scrollbar width")
        || !require(editor.scrollArea()->minimumWidth() == 240,
                    "editor width must remain fixed"))
        return 1;
    monitor.scrollArea()->verticalScrollBar()->setRange(0, 0);
    if (!require(monitor.scrollArea()->minimumWidth() == 220,
                 "monitor width not restored"))
        return 1;

    container.show();
    app.processEvents();
    editor.setMapFullscreen(true);
    if (!require(!editor.isVisible(), "editor tool panel visible in fullscreen")
        || !require(monitor.isVisible(), "other tool panel affected by fullscreen"))
        return 1;
    editor.setMapFullscreen(false);
    if (!require(editor.isVisible(), "editor tools not restored"))
        return 1;

    std::puts("Map mode tool panel ownership and fullscreen tests passed");
    return 0;
}
