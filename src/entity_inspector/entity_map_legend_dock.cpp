#include "entity_map_legend_dock.h"

EntityMapLegendDock::EntityMapLegendDock(HydraulicData *hydraulic_data, QWidget *parent)
    : QDockWidget("Map Symbology Legend", parent),
    hydraulic_data(hydraulic_data)
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
    
    this->group_node->setCollapsed(true);
    this->group_link->setCollapsed(true);
    this->group_heat->setCollapsed(true);
    
    this->layout->addStretch();
}

void EntityMapLegendDock::showMapLegendNode(VisualNode visual_node)
{
    this->visual_node = visual_node;
    setVisibility();
    
    if (visual_node == VisualNode::None)
        this->group_node->setCollapsed(true);
    else
        this->group_node->setCollapsed(false);
}
void EntityMapLegendDock::showMapLegendLink(VisualLink visual_link)
{
    this->visual_link = visual_link;
    setVisibility();
    
    if (visual_link == VisualLink::None)
        this->group_link->setCollapsed(true);
    else
        this->group_link->setCollapsed(false);
}
void EntityMapLegendDock::showMapLegendHeatmap(VisualHeatmap visual_heatmap)
{
    this->visual_heatmap = visual_heatmap;
    setVisibility();
    
    if (visual_heatmap == VisualHeatmap::None)
        this->group_heat->setCollapsed(true);
    else
        this->group_heat->setCollapsed(false);
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
    this->group_node = new GroupBoxCollapsible("Node Legend");
    QGridLayout *grid = new QGridLayout(this->group_node);
    
    
    
    this->layout->addWidget(this->group_node);
}

void EntityMapLegendDock::addGroupLink()
{
    this->group_link = new GroupBoxCollapsible("Link Legend");
    QGridLayout *grid = new QGridLayout(this->group_link);
    
    
    
    this->layout->addWidget(this->group_link);
}

void EntityMapLegendDock::addGroupHeatmap()
{
    this->group_heat = new GroupBoxCollapsible("Heatmap Overlay");
    QGridLayout *grid = new QGridLayout(this->group_heat);
    
    
    
    this->layout->addWidget(this->group_heat);
}
