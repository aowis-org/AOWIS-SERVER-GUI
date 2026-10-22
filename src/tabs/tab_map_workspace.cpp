#include "tabs/tab_map_workspace.h"

#include "common/_sizes.h"
#include "map/render/map_render_diagnostics.h"
#include "tabs/tab_map_editor_container.h"
#include "tabs/map_mode_tool_panel.h"
#include "tabs/tab_map_monitor_container.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

MapWorkspaceWidget::MapWorkspaceWidget(MapEditorContainer *editor,
                                       MapMonitorContainer *monitor, QWidget *parent)
    : QWidget(parent),
      pages(new QStackedWidget(this))
{
    Q_ASSERT(editor != nullptr);
    Q_ASSERT(monitor != nullptr);
    this->monitor_map = monitor->getMap();
    this->editor_map = editor->getMap();
    this->monitor_tools = monitor->toolPanel();
    this->editor_tools = editor->toolPanel();
    Q_ASSERT(this->monitor_tools != nullptr);
    Q_ASSERT(this->editor_tools != nullptr);
    Q_ASSERT(monitor->mapViewport() != this->monitor_tools);
    Q_ASSERT(editor->mapViewport() != this->editor_tools);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(this->pages);

    // Preserve the existing editor and monitor map/surface parent hierarchy.
    // Only the top-level containers change parent to QStackedWidget.
    this->pages->addWidget(monitor);
    this->pages->addWidget(editor);
    this->pages->addWidget(createEmptyModePage(DrainageMonitor));
    this->pages->addWidget(createEmptyModePage(DrainageEditor));

    this->editor_tools->setModeSelector(createModeSelector(DistributionEditor));
    this->monitor_tools->setModeSelector(createModeSelector(DistributionMonitor));
    this->pages->setCurrentIndex(DistributionMonitor);
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

MapWidget *MapWorkspaceWidget::activeMap() const
{
    switch (currentMode())
    {
    case DistributionMonitor:
        return this->monitor_map;
    case DistributionEditor:
        return this->editor_map;
    case DrainageMonitor:
    case DrainageEditor:
        return nullptr;
    }
    return nullptr;
}

MapModel *MapWorkspaceWidget::activeModel() const
{
    MapWidget *map = activeMap();
    return map != nullptr ? map->model() : nullptr;
}

MapModeToolPanel *MapWorkspaceWidget::activeToolPanel() const
{
    switch (currentMode())
    {
    case DistributionMonitor:
        return this->monitor_tools;
    case DistributionEditor:
        return this->editor_tools;
    case DrainageMonitor:
    case DrainageEditor:
        return nullptr;
    }
    return nullptr;
}

void MapWorkspaceWidget::setCurrentMode(int index)
{
    if (MapWorkspace::modeInfo(index) == nullptr)
        return;
    if (this->pages->currentIndex() == index)
        return;

    {
        // Scope reports synchronous QWidget/tab switching CPU time, not GPU
        // frame latency. A renderer restart is tracked separately.
        const MapRenderDiagnostics::Scope timing(
            MapRenderDiagnostics::Counter::WorkspaceModeSwitch);
        this->pages->setCurrentIndex(index);
        for (QTabBar *tab_bar : this->mode_tabs)
        {
            if (tab_bar->currentIndex() != index)
                tab_bar->setCurrentIndex(index);
        }
        emit currentModeChanged(index);
    }
    if (MapRenderDiagnostics::enabled())
    {
        qInfo() << "map workspace mode" << MapWorkspace::modeInfo(index)->title
                << "(diagnostics: AOWIS_MAP_RENDER_DIAGNOSTICS=1)";
        MapRenderDiagnostics::reportNow();
    }
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
    for (const MapWorkspace::ModeInfo &description : MapWorkspace::Modes)
    {
        const QString title = QString::fromUtf8(description.title);
        const int index = tabs->addTab(QIcon(QString::fromUtf8(description.icon_resource)), QString());
        tabs->setTabToolTip(index, title);
        tabs->setTabWhatsThis(index, title);
    }
    tabs->setCurrentIndex(mode);
    layout->addWidget(tabs);

    QLabel *title = new QLabel(tr(MapWorkspace::Modes[mode].title), selector);
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
