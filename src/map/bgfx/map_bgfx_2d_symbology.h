#ifndef MAP_BGFX_2D_SYMBOLOGY_H
#define MAP_BGFX_2D_SYMBOLOGY_H

#include "map/bgfx/map_bgfx_2d_mesh.h"
#include "map/render/map_network_render_symbology.h"
#include "network/network_symbology_rendering.h"

#include <algorithm>
#include <cmath>

// Maps the backend-neutral MapNetworkRenderSymbology onto the bgfx 2D
// attribute stream (colours, flow sign) and uniforms (sizes). Same rules as
// the QRhi scene (map_rhi_scene.cpp: applyLinkColor, applyNodeColor,
// appendIcon, appendHeatmap, flowDirectionColor) so both renderers agree while
// RHI exists.
namespace MapBgfx2dSymbology
{
// Demand-point attachment lines: fixed hairline width, like QRhi (0.75 px
// half width).
constexpr double AttachmentWidthPixels = 1.5;
// Flow chevron stroke width relative to its length (QRhi ratio).
constexpr double FlowStrokeWidthRatio = 0.2;

struct Sizes
{
    double link_width_px = 3.0;
    double attachment_width_px = AttachmentWidthPixels;
    double node_half_px = 6.0;
    double icon_size_px = 24.0;
    double flow_length_px = 10.0;
    double flow_half_stroke_px = 1.0;
    // 0 disables the heatmap pass (no heatmap selected).
    double heatmap_radius_px = 0.0;
    double heatmap_opacity = 0.75;
    // Radius fraction drawn at full strength (QRhi clamps it to 0.9).
    double heatmap_solid_fraction = 0.7;
};

// Whether any heatmap spot can be visible (the pass is skipped otherwise).
inline bool heatmapActive(const MapNetworkRenderSymbology &symbology)
{
    return symbology.visual_heatmap != VisualHeatmap::None
        && !symbology.heatmap_fractions.isEmpty()
        && symbology.heatmap_opacity > 0;
}

// Metre-based sizes are converted with the continuous zoom, so they scale
// smoothly, never per imagery level.
inline Sizes sizes(const MapNetworkRenderSymbology &symbology,
                   double latitude_deg, double continuous_zoom)
{
    const std::function<double(NetworkSymbologySizeUnit, double, double)> size_px =
        [latitude_deg, continuous_zoom](NetworkSymbologySizeUnit unit, double pixels, double meters)
    {
        return unit == NetworkSymbologySizeUnit::Meters
            ? MapBgfx2dChunking::pixelsForMeters(meters, latitude_deg, continuous_zoom)
            : pixels;
    };
    Sizes result;
    // Keep sub-pixel symbols visible as a hairline / dot when zoomed out.
    result.link_width_px = std::max(1.0, size_px(symbology.link_thickness_unit,
        double(symbology.link_thickness_px), symbology.link_thickness_m));
    result.node_half_px = std::max(1.0, 0.5 * size_px(symbology.node_size_unit,
        double(symbology.node_size_px), symbology.node_size_m));
    result.icon_size_px = std::max(1.0, size_px(symbology.icon_size_unit,
        double(symbology.icon_size_px), symbology.icon_size_m));
    result.flow_length_px = std::max(0.0, double(symbology.flow_direction_size_px));
    result.flow_half_stroke_px =
        0.5 * std::max(1.0, result.flow_length_px * FlowStrokeWidthRatio);
    // Heatmap radius (QRhi heatmapRadiusPixels(), but from the continuous
    // zoom, so a metre radius grows smoothly instead of per imagery level).
    if (heatmapActive(symbology))
    {
        result.heatmap_radius_px = symbology.heatmap_radius_unit == HeatmapRadiusUnit::Pixels
            ? std::max(1.0, double(symbology.heatmap_radius_px))
            : std::max(1.0, MapBgfx2dChunking::pixelsForMeters(
                  double(symbology.heatmap_radius_m), latitude_deg, continuous_zoom));
    }
    result.heatmap_opacity = std::clamp(double(symbology.heatmap_opacity) / 100.0, 0.0, 1.0);
    result.heatmap_solid_fraction =
        std::clamp(double(symbology.heatmap_solid_center_percent) / 100.0, 0.0, 0.9);
    return result;
}

// has_icon: mapHasIcon(entity_type), passed in so this header does not need
// the QPainter-based atlas.
inline quint32 color(const MapNetworkRenderSymbology &symbology, MapBgfx2dPrimitive primitive,
                     quint32 render_id, InfrastructureEntity entity_type, bool has_icon)
{
    QRgb argb = networkSymbologyDefaultColor();
    switch (primitive)
    {
        case MapBgfx2dPrimitive::Link:
            argb = symbology.link_colors.value(render_id, networkSymbologyDefaultColor());
            break;
        case MapBgfx2dPrimitive::Attachment:
            // Attachments take the colour of their demand point (QRhi rule).
            argb = symbology.node_colors.value(render_id, networkSymbologyDefaultColor());
            break;
        case MapBgfx2dPrimitive::Node:
            argb = symbology.node_colors.value(render_id, networkSymbologyDefaultColor());
            // The dot is replaced by the icon when icons are shown, and
            // junctions can be switched off: alpha 0, no geometry rebuild.
            if ((entity_type == InfrastructureEntity::Junction && !symbology.show_junctions)
                || (symbology.show_icons && has_icon))
            {
                argb &= 0x00ffffffu;
            }
            break;
        case MapBgfx2dPrimitive::NodeIcon:
        case MapBgfx2dPrimitive::LinkIcon:
        {
            const bool on_link = primitive == MapBgfx2dPrimitive::LinkIcon;
            const bool colorization_active = on_link
                ? symbology.visual_link != VisualLink::None
                : symbology.visual_node != VisualNode::None;
            argb = symbology.icon_default_fill_color;
            if (colorization_active)
            {
                argb = on_link
                    ? symbology.link_colors.value(render_id, networkSymbologyUnavailableColor())
                    : symbology.node_colors.value(render_id, networkSymbologyUnavailableColor());
                if (argb == networkSymbologyUnavailableColor())
                    argb = networkSymbologyIconUnavailableFillColor();
            }
            if (!symbology.show_icons)
                argb &= 0x00ffffffu;
            break;
        }
        case MapBgfx2dPrimitive::Heatmap:
        {
            // Spot colour from the heatmap palette (QRhi appendHeatmap());
            // nodes without a value get no spot (alpha 0).
            const QHash<quint32, double>::const_iterator fraction =
                symbology.heatmap_fractions.constFind(render_id);
            if (symbology.visual_heatmap == VisualHeatmap::None
                || fraction == symbology.heatmap_fractions.cend())
            {
                return 0u;
            }
            argb = networkSymbologyInterpolatedRampColor(
                fraction.value(), symbology.heatmap_palette,
                symbology.heatmap_palette_flipped).rgb();
            break;
        }
    }
    return mapBgfx2dAbgrFromArgb(quint32(argb));
}

// Chevron colour contrasts with the link colour (QRhi flowDirectionColor);
// the sign is the current flow direction, 0 hides the link's chevrons.
inline MapBgfx2dFlowAttribute flow(const MapNetworkRenderSymbology &symbology,
                                   quint32 link_render_id)
{
    MapBgfx2dFlowAttribute attribute;
    const qint8 direction = symbology.flow_directions.value(link_render_id, 0);
    if (!symbology.show_flow_direction || symbology.flow_direction_size_px <= 0 || direction == 0)
        return attribute;
    const QRgb link_color =
        symbology.link_colors.value(link_render_id, networkSymbologyDefaultColor());
    const double luminance = 0.2126 * double(qRed(link_color))
        + 0.7152 * double(qGreen(link_color)) + 0.0722 * double(qBlue(link_color));
    attribute.abgr = mapBgfx2dAbgrFromArgb(
        quint32(luminance >= 150.0 ? qRgb(0, 0, 0) : qRgb(255, 255, 255)));
    attribute.sign = direction > 0 ? 1.0f : -1.0f;
    return attribute;
}
}

#endif // MAP_BGFX_2D_SYMBOLOGY_H
