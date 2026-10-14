#pragma once

#include "common/_enums_structs.h"

#include <aowis/model/units/quantity.h>

#include <array>
#include <cstddef>
#include <string_view>

// Shared metric identities for the map legend dock and HUD. Labels are purely
// presentational; unit selection is keyed directly by the Model quantity.
namespace MapLegendMetrics
{
enum class Scope { Node, Link, Heatmap };
enum class RoughnessStyle { HazenWilliams, DarcyWeisbach, ChezyManning };

struct Descriptor
{
    Scope scope;
    int visual;
    RoughnessStyle roughness_style;
    const char *label;
    std::string_view quantity;
};

inline constexpr std::array descriptors = std::to_array<Descriptor>({
    {Scope::Node, static_cast<int>(VisualNode::None), RoughnessStyle::HazenWilliams, "None", {}},
    {Scope::Node, static_cast<int>(VisualNode::Elevation), RoughnessStyle::HazenWilliams, "Elevation", AOWIS_QUANTITY("elevation")},
    {Scope::Node, static_cast<int>(VisualNode::BaseDemand), RoughnessStyle::HazenWilliams, "Base Demand", AOWIS_QUANTITY("base_demand_flow_rate")},
    {Scope::Node, static_cast<int>(VisualNode::TotalDemand), RoughnessStyle::HazenWilliams, "Total Demand", AOWIS_QUANTITY("volumetric_flow_rate")},
    {Scope::Node, static_cast<int>(VisualNode::DemandDeficit), RoughnessStyle::HazenWilliams, "Demand Deficit", AOWIS_QUANTITY("volumetric_flow_rate")},
    {Scope::Node, static_cast<int>(VisualNode::EmitterFlow), RoughnessStyle::HazenWilliams, "Emitter Flow", AOWIS_QUANTITY("volumetric_flow_rate")},
    {Scope::Node, static_cast<int>(VisualNode::Leakage), RoughnessStyle::HazenWilliams, "Leakage", AOWIS_QUANTITY("volumetric_flow_rate")},
    {Scope::Node, static_cast<int>(VisualNode::Head), RoughnessStyle::HazenWilliams, "Head", AOWIS_QUANTITY("hydraulic_head")},
    {Scope::Node, static_cast<int>(VisualNode::Pressure), RoughnessStyle::HazenWilliams, "Pressure", AOWIS_QUANTITY("pressure_head")},
    {Scope::Node, static_cast<int>(VisualNode::Chlorine), RoughnessStyle::HazenWilliams, "Cl₂", AOWIS_QUANTITY("chemical_mass_concentration")},
    {Scope::Node, static_cast<int>(VisualNode::RiverWater), RoughnessStyle::HazenWilliams, "River Water", AOWIS_QUANTITY("source_trace_percentage")},
    {Scope::Node, static_cast<int>(VisualNode::LakeWater), RoughnessStyle::HazenWilliams, "Lake Water", AOWIS_QUANTITY("source_trace_percentage")},
    {Scope::Node, static_cast<int>(VisualNode::WaterAge), RoughnessStyle::HazenWilliams, "Water Age", AOWIS_QUANTITY("water_age")},
    {Scope::Link, static_cast<int>(VisualLink::None), RoughnessStyle::HazenWilliams, "None", {}},
    {Scope::Link, static_cast<int>(VisualLink::Diameter), RoughnessStyle::HazenWilliams, "Diameter", AOWIS_QUANTITY("link_diameter")},
    {Scope::Link, static_cast<int>(VisualLink::Length), RoughnessStyle::HazenWilliams, "Length", AOWIS_QUANTITY("length")},
    {Scope::Link, static_cast<int>(VisualLink::Roughness), RoughnessStyle::HazenWilliams, "Hazen-Williams C", {}},
    {Scope::Link, static_cast<int>(VisualLink::FlowRate), RoughnessStyle::HazenWilliams, "Flow Rate", AOWIS_QUANTITY("volumetric_flow_rate")},
    {Scope::Link, static_cast<int>(VisualLink::Velocity), RoughnessStyle::HazenWilliams, "Velocity", AOWIS_QUANTITY("velocity")},
    {Scope::Link, static_cast<int>(VisualLink::HeadLoss), RoughnessStyle::HazenWilliams, "Head Loss", AOWIS_QUANTITY("head_loss")},
    {Scope::Link, static_cast<int>(VisualLink::Leakage), RoughnessStyle::HazenWilliams, "Leakage", AOWIS_QUANTITY("volumetric_flow_rate")},
    {Scope::Link, static_cast<int>(VisualLink::Chlorine), RoughnessStyle::HazenWilliams, "Cl₂", AOWIS_QUANTITY("chemical_mass_concentration")},
    {Scope::Link, static_cast<int>(VisualLink::RiverWater), RoughnessStyle::HazenWilliams, "River Water", AOWIS_QUANTITY("source_trace_percentage")},
    {Scope::Link, static_cast<int>(VisualLink::LakeWater), RoughnessStyle::HazenWilliams, "Lake Water", AOWIS_QUANTITY("source_trace_percentage")},
    {Scope::Link, static_cast<int>(VisualLink::WaterAge), RoughnessStyle::HazenWilliams, "Water Age", AOWIS_QUANTITY("water_age")},
    {Scope::Link, static_cast<int>(VisualLink::Roughness), RoughnessStyle::DarcyWeisbach, "Absolute Roughness ε", AOWIS_QUANTITY("darcy_weisbach_roughness_height")},
    {Scope::Link, static_cast<int>(VisualLink::Roughness), RoughnessStyle::ChezyManning, "Manning Roughness n", {}},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::None), RoughnessStyle::HazenWilliams, "None", {}},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::Elevation), RoughnessStyle::HazenWilliams, "Elevation", AOWIS_QUANTITY("elevation")},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::BaseDemand), RoughnessStyle::HazenWilliams, "Base Demand", AOWIS_QUANTITY("base_demand_flow_rate")},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::TotalDemand), RoughnessStyle::HazenWilliams, "Total Demand", AOWIS_QUANTITY("volumetric_flow_rate")},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::DemandDeficit), RoughnessStyle::HazenWilliams, "Demand Deficit", AOWIS_QUANTITY("volumetric_flow_rate")},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::EmitterFlow), RoughnessStyle::HazenWilliams, "Emitter Flow", AOWIS_QUANTITY("volumetric_flow_rate")},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::Leakage), RoughnessStyle::HazenWilliams, "Leakage", AOWIS_QUANTITY("volumetric_flow_rate")},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::Head), RoughnessStyle::HazenWilliams, "Head", AOWIS_QUANTITY("hydraulic_head")},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::Pressure), RoughnessStyle::HazenWilliams, "Pressure", AOWIS_QUANTITY("pressure_head")},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::Chlorine), RoughnessStyle::HazenWilliams, "Cl₂", AOWIS_QUANTITY("chemical_mass_concentration")},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::RiverWater), RoughnessStyle::HazenWilliams, "River Water", AOWIS_QUANTITY("source_trace_percentage")},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::LakeWater), RoughnessStyle::HazenWilliams, "Lake Water", AOWIS_QUANTITY("source_trace_percentage")},
    {Scope::Heatmap, static_cast<int>(VisualHeatmap::WaterAge), RoughnessStyle::HazenWilliams, "Water Age", AOWIS_QUANTITY("water_age")},
});

[[nodiscard]] constexpr const Descriptor &lookup(
    Scope scope, int visual, RoughnessStyle roughness = RoughnessStyle::HazenWilliams)
{
    for (const Descriptor &entry : descriptors)
    {
        if (entry.scope == scope && entry.visual == visual
            && (scope != Scope::Link || visual != static_cast<int>(VisualLink::Roughness)
                || entry.roughness_style == roughness))
            return entry;
    }
    // Unrecognized visuals behave like None rather than mapping to a wrong unit.
    for (const Descriptor &entry : descriptors)
    {
        if (entry.scope == scope && entry.visual == 0)
            return entry;
    }
    return descriptors.front();
}

[[nodiscard]] constexpr const Descriptor &node(VisualNode visual)
{
    return lookup(Scope::Node, static_cast<int>(visual));
}

[[nodiscard]] constexpr const Descriptor &link(
    VisualLink visual, RoughnessStyle roughness = RoughnessStyle::HazenWilliams)
{
    return lookup(Scope::Link, static_cast<int>(visual), roughness);
}

[[nodiscard]] constexpr const Descriptor &heatmap(VisualHeatmap visual)
{
    return lookup(Scope::Heatmap, static_cast<int>(visual));
}

// Detect omissions or accidental duplicate definitions when editing the table.
[[nodiscard]] consteval bool tableIsCompleteAndUnique()
{
    for (std::size_t index = 0; index < descriptors.size(); ++index)
    {
        const Descriptor &entry = descriptors[index];
        for (std::size_t other = index + 1; other < descriptors.size(); ++other)
        {
            const Descriptor &candidate = descriptors[other];
            if (entry.scope == candidate.scope && entry.visual == candidate.visual
                && entry.roughness_style == candidate.roughness_style)
                return false;
        }
    }
    for (int value = int(VisualNode::None); value <= int(VisualNode::WaterAge); ++value)
    {
        if (node(static_cast<VisualNode>(value)).visual != value
            || heatmap(static_cast<VisualHeatmap>(value)).visual != value)
            return false;
    }
    for (int value = int(VisualLink::None); value <= int(VisualLink::WaterAge); ++value)
    {
        if (link(static_cast<VisualLink>(value)).visual != value)
            return false;
    }
    return link(VisualLink::Roughness, RoughnessStyle::DarcyWeisbach).roughness_style
               == RoughnessStyle::DarcyWeisbach
        && link(VisualLink::Roughness, RoughnessStyle::ChezyManning).roughness_style
               == RoughnessStyle::ChezyManning;
}

static_assert(tableIsCompleteAndUnique(), "Map legend metric definitions must be complete and unique");
static_assert(node(VisualNode::Pressure).quantity == AOWIS_QUANTITY("pressure_head"));
static_assert(link(VisualLink::Roughness, RoughnessStyle::DarcyWeisbach).quantity
              == AOWIS_QUANTITY("darcy_weisbach_roughness_height"));
}
