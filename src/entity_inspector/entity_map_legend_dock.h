#ifndef ENTITY_MAP_LEGEND_DOCK_H
#define ENTITY_MAP_LEGEND_DOCK_H

#include <QWidget>
#include <QDockWidget>

#include <QVBoxLayout>

#include <QDebug>

#include "../widgets/group_box_collapsible.h"

#include "../_enums_structs.h"
#include "../_sizes.h"

class EntityMapLegendDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit EntityMapLegendDock(QWidget *parent = nullptr);
    
public slots:
    void showMapLegendNode(VisualNode visual_node);
    void showMapLegendLink(VisualLink visual_link);
    void showMapLegendHeatmap(VisualHeatmap visual_heatmap);
    
private:
    QVBoxLayout *layout = nullptr;
    
    VisualNode visual_node = VisualNode::None;
    VisualLink visual_link = VisualLink::None;
    VisualHeatmap visual_heatmap = VisualHeatmap::None;
    
    void setVisibility();
    
    GroupBoxCollapsible *group_node = nullptr;
    GroupBoxCollapsible *group_link = nullptr;
    GroupBoxCollapsible *group_heat = nullptr;
    
    void addGroupNode();
    void addGroupLink();
    void addGroupHeatmap();
    
signals:
    
};

#endif // ENTITY_MAP_LEGEND_DOCK_H
