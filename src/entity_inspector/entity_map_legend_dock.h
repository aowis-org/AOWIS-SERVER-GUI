#ifndef ENTITY_MAP_LEGEND_DOCK_H
#define ENTITY_MAP_LEGEND_DOCK_H

#include <QComboBox>
#include <QDockWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "widgets/group_box_collapsible.h"
#include "network/hydraulic_data.h"
#include "network/network_symbology.h"

#include "common/_enums_structs.h"
#include "common/_sizes.h"

class MapSymbologyRampWidget;


class EntityMapLegendDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit EntityMapLegendDock(HydraulicData *hydraulic_data, QWidget *parent = nullptr);
    int dockHeightPreferred() const;

public slots:
    void showMapLegendNode(VisualNode visual_node);
    void showMapLegendLink(VisualLink visual_link);
    void showMapLegendHeatmap(VisualHeatmap visual_heatmap);
    void setMapMonitorActive(bool active);
    void setNodePalette(NetworkSymbologyPalette palette, bool flipped);
    void setLinkPalette(NetworkSymbologyPalette palette, bool flipped);
    void setHeatmapPalette(NetworkSymbologyPalette palette, bool flipped);

signals:
    void signalDockHeightPreferredChanged(int height);
    void signalNodePaletteSelected(NetworkSymbologyPalette palette, bool flipped);
    void signalLinkPaletteSelected(NetworkSymbologyPalette palette, bool flipped);
    void signalHeatmapPaletteSelected(NetworkSymbologyPalette palette, bool flipped);

private:
    HydraulicData *hydraulic_data = nullptr;
    QWidget *content = nullptr;
    QVBoxLayout *layout = nullptr;

    VisualNode visual_node = VisualNode::None;
    VisualLink visual_link = VisualLink::None;
    VisualHeatmap visual_heatmap = VisualHeatmap::None;
    NetworkSymbologyPalette node_palette = NetworkSymbologyDefaultNodePalette;
    bool node_palette_flipped = false;
    NetworkSymbologyPalette link_palette = NetworkSymbologyDefaultLinkPalette;
    bool link_palette_flipped = false;
    NetworkSymbologyPalette heatmap_palette = NetworkSymbologyDefaultHeatmapPalette;
    bool heatmap_palette_flipped = false;
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
