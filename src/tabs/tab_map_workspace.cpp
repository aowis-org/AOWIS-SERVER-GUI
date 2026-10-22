#include "tabs/tab_map_workspace.h"

#include "common/_sizes.h"
#include "tabs/tab_map_editor_container.h"
#include "tabs/tab_map_monitor_container.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

namespace
{
struct ModeDescription
{
    const char *title;
    const char *icon;
};

constexpr ModeDescription descriptions[] = {
    {"Distribution Editor", ":/icon/water_distribution_editor.png"},
    {"Distribution Monitor", ":/icon/water_distribution_monitor.png"},
    {"Drainage Editor", ":/icon/water_drainage_editor.png"},
    {"Drainage Monitor", ":/icon/water_drainage_monitor.png"}
};
}

MapWorkspaceWidget::MapWorkspaceWidget(MapEditorContainer *editor,
                                       MapMonitorContainer *monitor, QWidget *parent)
    : QWidget(parent),
      pages(new QStackedWidget(this))
{
    Q_ASSERT(editor != nullptr);
    Q_ASSERT(monitor != nullptr);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(this->pages);

    // Preserve the existing editor and monitor map/surface parent hierarchy.
    // Only the top-level containers change parent to QStackedWidget.
    this->pages->addWidget(editor);
    this->pages->addWidget(monitor);
    this->pages->addWidget(createEmptyModePage(DrainageEditor));
    this->pages->addWidget(createEmptyModePage(DrainageMonitor));

    editor->setModeSelector(createModeSelector(DistributionEditor));
    monitor->setModeSelector(createModeSelector(DistributionMonitor));
    this->pages->setCurrentIndex(DistributionEditor);
}

MapWorkspaceWidget::Mode MapWorkspaceWidget::currentMode() const
{
    return static_cast<Mode>(this->pages->currentIndex());
}

bool MapWorkspaceWidget::isDistributionEditorActive() const
{
    return currentMode() == DistributionEditor;
}

bool MapWorkspaceWidget::isDistributionMonitorActive() const
{
    return currentMode() == DistributionMonitor;
}

void MapWorkspaceWidget::setCurrentMode(int index)
{
    if (index < DistributionEditor || index > DrainageMonitor)
        return;
    if (this->pages->currentIndex() == index)
        return;

    this->pages->setCurrentIndex(index);
    for (QTabBar *tab_bar : this->mode_tabs)
    {
        if (tab_bar->currentIndex() != index)
            tab_bar->setCurrentIndex(index);
    }
    emit currentModeChanged(index);
}

QWidget *MapWorkspaceWidget::createModeSelector(Mode mode)
{
    QWidget *selector = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(selector);
    layout->setContentsMargins(3, 4, 3, 5);
    layout->setSpacing(2);

    // Four icon tabs fit the existing 210-240 px map sidebar on desktop and
    // WASM. The full, human-readable mode name stays visible just below.
    QTabBar *tabs = new QTabBar(selector);
    tabs->setObjectName(QStringLiteral("mapModeTabs"));
    tabs->setAccessibleName(tr("Map modes"));
    tabs->setExpanding(true);
    tabs->setUsesScrollButtons(false);
    tabs->setIconSize(QSize(32, 32));
    for (const ModeDescription &description : descriptions)
    {
        const QString title = QString::fromUtf8(description.title);
        const int index = tabs->addTab(QIcon(QString::fromUtf8(description.icon)), QString());
        tabs->setTabToolTip(index, title);
        tabs->setTabWhatsThis(index, title);
    }
    tabs->setCurrentIndex(mode);
    layout->addWidget(tabs);

    QLabel *title = new QLabel(tr(descriptions[mode].title), selector);
    title->setAlignment(Qt::AlignCenter);
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    layout->addWidget(title);

    this->mode_tabs.append(tabs);
    connect(tabs, &QTabBar::currentChanged, this, &MapWorkspaceWidget::setCurrentMode);
    return selector;
}

QWidget *MapWorkspaceWidget::createEmptyModePage(Mode mode)
{
    QWidget *page = new QWidget(this->pages);
    QHBoxLayout *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget *sidebar = new QWidget(page);
    sidebar->setFixedWidth(Sizes::SidebarMapEditLeftWidth);
    QVBoxLayout *sidebar_layout = new QVBoxLayout(sidebar);
    sidebar_layout->setContentsMargins(0, 0, 0, 0);
    sidebar_layout->setSpacing(0);
    sidebar_layout->addWidget(createModeSelector(mode));
    sidebar_layout->addStretch(1);
    layout->addWidget(sidebar);

    // Drainage tools and their map views will be integrated separately.
    layout->addWidget(new QWidget(page), 1);
    return page;
}
