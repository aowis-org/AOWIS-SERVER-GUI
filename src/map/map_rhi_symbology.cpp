#include "map_rhi_symbology.h"

#include "../hydraulic_data.h"
#include "../network_render_snapshot.h"
#include "../network_symbology_rendering.h"
#include "../network_symbology_values.h"

MapRhiSymbology resolveMapRhiSymbology(
    const HydraulicData &hydraulic_data,
    const NetworkSymbologySettings &settings,
    const NetworkSymbologyRanges &ranges)
{
    const NetworkSymbologySettings bounded_settings = settings.bounded();
    const NetworkRenderSnapshot &snapshot = hydraulic_data.networkRenderSnapshot();
    const NetworkHydraulic &network_hydraulic = hydraulic_data.networkHydraulic();

    QHash<QUuid, double> water_age_node_values;
    QHash<QUuid, double> water_age_link_values;
    const WaterQualitySimulationResult *quality_result =
        hydraulic_data.currentWaterQualitySimulationResult(WaterQualityAnalysisType::WaterAge);
    if (quality_result != nullptr)
    {
        if (bounded_settings.visual_node == VisualNode::WaterAge)
            water_age_node_values = waterAgeNodeSymbologyValues(*quality_result);
        if (bounded_settings.visual_link == VisualLink::WaterAge)
            water_age_link_values = waterAgeLinkSymbologyValues(*quality_result);
    }

    QHash<QUuid, double> hydraulic_node_values;
    QHash<QUuid, double> hydraulic_link_values;
    QHash<QUuid, qint8> hydraulic_flow_directions;
    const HydraulicSimulationResult *hydraulic_result = hydraulic_data.currentSimulationResult();
    if (hydraulic_result != nullptr)
    {
        if (nodeVisualUsesHydraulicSimulationResult(bounded_settings.visual_node))
        {
            hydraulic_node_values = hydraulicNodeSymbologyValues(
                *hydraulic_result, bounded_settings.visual_node);
        }
        if (linkVisualUsesHydraulicSimulationResult(bounded_settings.visual_link))
        {
            hydraulic_link_values = hydraulicLinkSymbologyValues(
                *hydraulic_result, bounded_settings.visual_link);
        }
        if (bounded_settings.show_flow_direction)
            hydraulic_flow_directions = hydraulicLinkFlowDirections(*hydraulic_result);
    }

    const QHash<QUuid, double> node_values =
        bounded_settings.visual_node == VisualNode::WaterAge
            ? water_age_node_values
            : nodeVisualUsesHydraulicSimulationResult(bounded_settings.visual_node)
                ? hydraulic_node_values
                : networkNodeSymbologyValues(network_hydraulic, bounded_settings.visual_node);
    const QHash<QUuid, double> link_values =
        bounded_settings.visual_link == VisualLink::WaterAge
            ? water_age_link_values
            : linkVisualUsesHydraulicSimulationResult(bounded_settings.visual_link)
                ? hydraulic_link_values
                : networkLinkSymbologyValues(network_hydraulic, bounded_settings.visual_link);

    MapRhiSymbology result;
    result.node_size_percent = bounded_settings.node_size_percent;
    result.icon_size_percent = bounded_settings.icon_size_percent;
    result.link_thickness_px = bounded_settings.link_thickness_px;
    result.show_flow_direction = bounded_settings.show_flow_direction;
    result.flow_direction_size_px = bounded_settings.flow_direction_size_px;

    if (result.show_flow_direction)
    {
        result.flow_directions.reserve(snapshot.links.size());
        for (const NetworkRenderLink &link : snapshot.links)
        {
            const QHash<QUuid, qint8>::const_iterator iterator =
                hydraulic_flow_directions.constFind(link.uuid);
            if (iterator != hydraulic_flow_directions.cend() && iterator.value() != 0)
                result.flow_directions.insert(link.render_id, iterator.value());
        }
    }

    result.node_colors.reserve(snapshot.nodes.size());
    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        QRgb color = networkSymbologyDefaultColor();
        if (bounded_settings.visual_node != VisualNode::None)
        {
            const QHash<QUuid, double>::const_iterator iterator = node_values.constFind(node.uuid);
            color = iterator == node_values.cend()
                ? networkSymbologyUnavailableColor()
                : networkSymbologyColor(
                    iterator.value(), ranges.node_minimum, ranges.node_maximum);
        }
        result.node_colors.insert(node.render_id, color);
    }

    result.link_colors.reserve(snapshot.links.size());
    for (const NetworkRenderLink &link : snapshot.links)
    {
        QRgb color = networkSymbologyDefaultColor();
        if (bounded_settings.visual_link != VisualLink::None)
        {
            const QHash<QUuid, double>::const_iterator iterator = link_values.constFind(link.uuid);
            color = iterator == link_values.cend()
                ? networkSymbologyUnavailableColor()
                : networkSymbologyColor(
                    iterator.value(), ranges.link_minimum, ranges.link_maximum);
        }
        result.link_colors.insert(link.render_id, color);
    }

    return result;
}
