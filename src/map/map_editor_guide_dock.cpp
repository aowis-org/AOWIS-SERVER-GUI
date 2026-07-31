#include "map_editor_guide_dock.h"

#include "../_sizes.h"

#include <QCloseEvent>

MapEditorGuideDock::MapEditorGuideDock(QWidget *parent)
    : QDockWidget("Map Editor Guide", parent)
{
    resize(Sizes::SidebarRightWidth, height());
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    setWidget(new QWidget(this));
}

void MapEditorGuideDock::setMapEditorActive(bool active)
{
    this->map_editor_active = active;
    updateVisibility();
}

void MapEditorGuideDock::setRequestedVisible(bool visible)
{
    this->requested_visible = visible;
    updateVisibility();
}

void MapEditorGuideDock::closeEvent(QCloseEvent *event)
{
    this->requested_visible = false;
    emit requestedVisibilityChanged(false);
    QDockWidget::closeEvent(event);
}

void MapEditorGuideDock::updateVisibility()
{
    setVisible(this->map_editor_active && this->requested_visible);
}
