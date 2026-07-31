#include "map_editor_guide_dock.h"

#include "../_sizes.h"

MapEditorGuideDock::MapEditorGuideDock(QWidget *parent)
    : QDockWidget("Map Editor Guide", parent)
{
    this->resize(Sizes::SidebarRightWidth, this->height());
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    this->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    this->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    this->setWidget(new QWidget(this));
}

void MapEditorGuideDock::setMapEditorActive(bool active)
{
    this->setVisible(active);
}
