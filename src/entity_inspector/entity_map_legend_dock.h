#ifndef ENTITY_MAP_LEGEND_DOCK_H
#define ENTITY_MAP_LEGEND_DOCK_H

#include <QComboBox>
#include <QDockWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "../widgets/group_box_collapsible.h"
#include "../hydraulic_data.h"

#include "../_enums_structs.h"
#include "../_sizes.h"

class MapSymbologyRampWidget;


class EntityMapLegendHud : public QWidget
{
    Q_OBJECT
public:
    explicit EntityMapLegendHud(HydraulicData *hydraulic_data, QWidget *parent = nullptr);

    void setMapMonitorActive(bool active);
    void setNodeVisual(VisualNode visual_node);
    void setLinkVisual(VisualLink visual_link);
    void setHeatmapVisual(VisualHeatmap visual_heatmap);

signals:
    void signalNodeVisualSelected(VisualNode visual_node);
    void signalLinkVisualSelected(VisualLink visual_link);
    void signalHeatmapVisualSelected(VisualHeatmap visual_heatmap);

private:
    HydraulicData *hydraulic_data = nullptr;
    QVBoxLayout *layout = nullptr;
    QComboBox *combo_node = nullptr;
    QComboBox *combo_link = nullptr;
    QComboBox *combo_heatmap = nullptr;
    MapSymbologyRampWidget *legend_node = nullptr;
    MapSymbologyRampWidget *legend_link = nullptr;
    MapSymbologyRampWidget *legend_heatmap = nullptr;
    VisualNode visual_node = VisualNode::None;
    VisualLink visual_link = VisualLink::None;
    VisualHeatmap visual_heatmap = VisualHeatmap::None;
    bool map_monitor_active = false;

    void updateNodeLegend();
    void updateLinkLegend();
    void updateHeatmapLegend();
    void updateVisibility();
};

class EntityMapLegendDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit EntityMapLegendDock(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
    int dockHeightPreferred() const;
    void configureAsHud();

public slots:
    void showMapLegendNode(VisualNode visual_node);
    void showMapLegendLink(VisualLink visual_link);
    void showMapLegendHeatmap(VisualHeatmap visual_heatmap);
    void setMapMonitorActive(bool active);

signals:
    void signalDockHeightPreferredChanged(int height);

private:
    HydraulicData *hydraulic_data = nullptr;
    QWidget *content = nullptr;
    QVBoxLayout *layout = nullptr;

    VisualNode visual_node = VisualNode::None;
    VisualLink visual_link = VisualLink::None;
    VisualHeatmap visual_heatmap = VisualHeatmap::None;
    bool map_monitor_active = false;

    GroupBoxCollapsible *group_node = nullptr;
    GroupBoxCollapsible *group_link = nullptr;
    GroupBoxCollapsible *group_heat = nullptr;

    MapSymbologyRampWidget *legend_node = nullptr;
    MapSymbologyRampWidget *legend_link = nullptr;
    MapSymbologyRampWidget *legend_heat = nullptr;
    int dock_height_preferred = 0;

    void setVisibility();
    void addGroupNode();
    void addGroupLink();
    void addGroupHeatmap();
    void updateNodeLegend();
    void updateLinkLegend();
    void updateHeatmapLegend();
    void scheduleDockHeightUpdate();
    void updateDockHeight();
};

#endif // ENTITY_MAP_LEGEND_DOCK_H
