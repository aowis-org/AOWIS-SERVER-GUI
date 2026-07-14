#include "entity_map_legend_dock.h"

EntityMapLegendDock::EntityMapLegendDock(QWidget *parent)
    : QDockWidget("Map Symbology Legend", parent)
{
    //setMinimumWidth(Sizes::SidebarRightWidth);
    this->resize(Sizes::SidebarRightWidth, this->height());
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    //setAllowedAreas(Qt::RightDockWidgetArea);
    
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);
    
    QWidget *content = new QWidget(this);
    this->layout = new QVBoxLayout(content);
    setWidget(content);
    
    addGroupNode();
    addGroupLink();
    addGroupHeatmap();
    
    this->layout->addStretch();
}

void EntityMapLegendDock::showMapLegendNode(VisualNode visual_node)
{
    this->visual_node = visual_node;
    setVisibility();
}
void EntityMapLegendDock::showMapLegendLink(VisualLink visual_link)
{
    this->visual_link = visual_link;
    setVisibility();
}
void EntityMapLegendDock::showMapLegendHeatmap(VisualHeatmap visual_heatmap)
{
    this->visual_heatmap = visual_heatmap;
    setVisibility();
}

void EntityMapLegendDock::setVisibility()
{
    if (
        (this->visual_link != VisualLink::None) ||
        (this->visual_node != VisualNode::None) ||
        (this->visual_heatmap != VisualHeatmap::None)
    )
        setVisible(true);
    else
        setVisible(false);
}

void EntityMapLegendDock::addGroupNode()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Node Legend");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layout->addWidget(group);
}

void EntityMapLegendDock::addGroupLink()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Link Legend");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layout->addWidget(group);
}

void EntityMapLegendDock::addGroupHeatmap()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Heatmap Legend");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layout->addWidget(group);
}
