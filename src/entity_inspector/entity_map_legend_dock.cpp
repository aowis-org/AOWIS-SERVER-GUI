#include "entity_map_legend_dock.h"

EntityMapLegendDock::EntityMapLegendDock(QWidget *parent)
    : QDockWidget(parent)
{
    //setMinimumWidth(Sizes::SidebarRightWidth);
    this->resize(Sizes::SidebarRightWidth, this->height());
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    //setAllowedAreas(Qt::RightDockWidgetArea);
    
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);
    
    
        
}

void EntityMapLegendDock::showMapLegendNode(VisualNode visual_node)
{
    this->visual_node = visual_node;
    setVisibility();
    
    qDebug() << "node";
}
void EntityMapLegendDock::showMapLegendLink(VisualLink visual_link)
{
    this->visual_link = visual_link;
    setVisibility();
    
    qDebug() << "link";    
}

void EntityMapLegendDock::setVisibility()
{
    if ((this->visual_link != VisualLink::None) || (this->visual_node != VisualNode::None))
        setVisible(true);
    else
        setVisible(false);
}
