#ifndef ENTITY_MAP_LEGEND_DOCK_H
#define ENTITY_MAP_LEGEND_DOCK_H

#include <QDockWidget>
#include <QVBoxLayout>
#include <QWidget>

class QActionGroup;
class QToolButton;

#include "../widgets/group_box_collapsible.h"
#include "../hydraulic_data.h"

#include "../_enums_structs.h"
#include "../_sizes.h"

class MapSymbologyRampWidget;

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
    void signalHudNodeVisualSelected(VisualNode visual_node);
    void signalHudLinkVisualSelected(VisualLink visual_link);
    void signalHudHeatmapVisualSelected(VisualHeatmap visual_heatmap);

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
    bool hud_mode = false;
    QToolButton *hud_symbology_button = nullptr;
    QActionGroup *hud_node_actions = nullptr;
    QActionGroup *hud_link_actions = nullptr;
    QActionGroup *hud_heatmap_actions = nullptr;

    void setVisibility();
    void addGroupNode();
    void addGroupLink();
    void addGroupHeatmap();
    void updateNodeLegend();
    void updateLinkLegend();
    void updateHeatmapLegend();
    void scheduleDockHeightUpdate();
    void updateDockHeight();
    void createHudSymbologyMenu();
    void syncHudNodeSelection();
    void syncHudLinkSelection();
    void syncHudHeatmapSelection();
};

#endif // ENTITY_MAP_LEGEND_DOCK_H
