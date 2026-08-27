#ifndef NETWORK_SYMBOLOGY_H
#define NETWORK_SYMBOLOGY_H

#include <QtGlobal>

#include "_enums_structs.h"

enum class HeatmapRadiusUnit
{
    Meters,
    Pixels
};

enum class NetworkSymbologyPalette
{
    Viridis,
    Plasma,
    Inferno,
    Turbo,
    CoolWarm,
    Cividis,
    Magma,
    Batlow,
    RedBlue
};

struct NetworkSymbologySettings
{
    VisualNode visual_node = VisualNode::None;
    NetworkSymbologyPalette node_palette = NetworkSymbologyPalette::Viridis;
    bool node_palette_flipped = false;
    int node_size_percent = 100;
    int icon_size_percent = 100;
    VisualLink visual_link = VisualLink::None;
    NetworkSymbologyPalette link_palette = NetworkSymbologyPalette::Viridis;
    bool link_palette_flipped = false;
    int link_thickness_px = 3;
    bool show_flow_direction = true;
    int flow_direction_size_px = 10;
    VisualHeatmap visual_heatmap = VisualHeatmap::None;
    NetworkSymbologyPalette heatmap_palette = NetworkSymbologyPalette::Viridis;
    bool heatmap_palette_flipped = false;
    int heatmap_opacity = 75;
    HeatmapRadiusUnit heatmap_radius_unit = HeatmapRadiusUnit::Meters;
    int heatmap_radius_m = 400;
    int heatmap_radius_px = 50;
    int heatmap_solid_center_percent = 70;

    NetworkSymbologySettings bounded() const
    {
        NetworkSymbologySettings result = *this;
        result.node_size_percent = qBound(50, this->node_size_percent, 250);
        result.icon_size_percent = qBound(50, this->icon_size_percent, 250);
        result.link_thickness_px = qBound(1, this->link_thickness_px, 12);
        if (this->flow_direction_size_px <= 0)
        {
            result.flow_direction_size_px = 0;
            result.show_flow_direction = false;
        }
        else
        {
            result.flow_direction_size_px = qBound(6, this->flow_direction_size_px, 24);
        }
        result.heatmap_opacity = qBound(0, this->heatmap_opacity, 100);
        result.heatmap_radius_m = qBound(10, this->heatmap_radius_m, 1000);
        result.heatmap_radius_px = qBound(5, this->heatmap_radius_px, 250);
        result.heatmap_solid_center_percent =
            qBound(0, this->heatmap_solid_center_percent, 100);
        return result;
    }
};

struct NetworkSymbologyRanges
{
    double node_minimum = 0.0;
    double node_maximum = 0.0;
    double link_minimum = 0.0;
    double link_maximum = 0.0;
    double heatmap_minimum = 0.0;
    double heatmap_maximum = 0.0;
};

#endif // NETWORK_SYMBOLOGY_H
