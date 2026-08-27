#ifndef MAP_RHI_SYMBOLOGY_H
#define MAP_RHI_SYMBOLOGY_H

#include "../network_symbology.h"

#include <QColor>
#include <QHash>
#include <QtGlobal>

class HydraulicData;

struct MapRhiSymbology
{
    int node_size_percent = 100;
    int icon_size_percent = 100;
    bool show_icons = true;
    int link_thickness_px = 3;
    bool show_flow_direction = true;
    int flow_direction_size_px = 10;
    VisualHeatmap visual_heatmap = VisualHeatmap::None;
    int heatmap_opacity = 75;
    HeatmapRadiusUnit heatmap_radius_unit = HeatmapRadiusUnit::Meters;
    int heatmap_radius_m = 400;
    int heatmap_radius_px = 50;
    int heatmap_solid_center_percent = 70;
    NetworkSymbologyPalette heatmap_palette = NetworkSymbologyPalette::Viridis;
    bool heatmap_palette_flipped = false;
    QHash<quint32, double> heatmap_fractions;
    QHash<quint32, QRgb> node_colors;
    QHash<quint32, QRgb> link_colors;
    QHash<quint32, qint8> flow_directions;
};

MapRhiSymbology resolveMapRhiSymbology(
    const HydraulicData &hydraulic_data,
    const NetworkSymbologySettings &settings,
    const NetworkSymbologyRanges &ranges);

#endif // MAP_RHI_SYMBOLOGY_H
