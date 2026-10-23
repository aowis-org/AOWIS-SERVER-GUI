#include "tabs/map_mode_tab_bar.h"

#include <QApplication>
#include <QIcon>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

namespace
{
bool require(bool valid, const char *message)
{
    if (!valid)
        std::fprintf(stderr, "map_mode_tab_bar: %s\n", message);
    return valid;
}

void populate(QTabBar *tabs, const QIcon &icon)
{
    tabs->setExpanding(true);
    tabs->setUsesScrollButtons(false);
    tabs->setIconSize(QSize(26, 26));
    for (int index = 0; index < 5; ++index)
        tabs->addTab(icon, QString());
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QPixmap pixmap(26, 26);
    pixmap.fill(Qt::white);
    const QIcon icon(pixmap);

    QWidget sidebar;
    QVBoxLayout *layout = new QVBoxLayout(&sidebar);
    layout->setContentsMargins(3, 4, 3, 5);
    MapModeTabBar *tabs = new MapModeTabBar(&sidebar);
    populate(tabs, icon);
    layout->addWidget(tabs);
    layout->addStretch();

#ifdef Q_OS_WIN
    if (!require(tabs->sizeHint().width() <= 200,
                 "five Windows icon tabs must fit inside the 210px sidebar")
        || !require(sidebar.sizeHint().width() <= 210,
                    "Windows tab size hints widened the map sidebar"))
        return 1;
#else
    // Linux retains the original QTabBar size hints and native appearance.
    QTabBar original;
    populate(&original, icon);
    if (!require(tabs->sizeHint() == original.sizeHint(),
                 "non-Windows tab size hints must remain unchanged")
        || !require(tabs->minimumSizeHint() == original.minimumSizeHint(),
                    "non-Windows minimum size hints must remain unchanged"))
        return 1;
#endif

    // The smallest map sidebar is 210px wide; all five icons must stay
    // reachable without arrows, clipping or widening the sidebar.
    sidebar.resize(210, 100);
    sidebar.show();
    app.processEvents();
#ifdef Q_OS_WIN
    if (!require(tabs->width() <= 204,
                 "Windows tab bar exceeded its sidebar's available width"))
        return 1;
    for (int index = 0; index < tabs->count(); ++index)
    {
        const QRect bounds = tabs->tabRect(index);
        if (!require(bounds.width() >= 26 && bounds.left() >= 0
                         && bounds.right() < tabs->width(),
                     "a Windows map mode tab is clipped or too narrow for its icon")
            || !require(tabs->tabAt(bounds.center()) == index,
                        "a Windows map mode tab is not clickable"))
            return 1;
    }
#endif

    std::puts("Map mode tab sizing contract passed");
    return 0;
}
