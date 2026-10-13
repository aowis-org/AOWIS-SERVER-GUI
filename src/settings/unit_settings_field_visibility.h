#pragma once

#include <aowis/model/units/unit_profile.h>

#include <string_view>

// Only quantities consumed by current GUI editors, tables, legends, or readouts
// belong in the unit Settings tree. Keep all electrical options available for
// future GUI consumers. This is a presentation filter, never a registry or
// profile-storage filter: previously saved choices remain intact.
namespace UnitSettingsFieldVisibility
{
[[nodiscard]] constexpr bool isVisible(const aowis::units::UnitField &field) noexcept
{
    if (field.group == "Power and electrical") return true;

    constexpr std::string_view gui_quantities[] = {
        "length", "elevation", "altitude", "distance", "vertical_offset", "link_diameter",
        "tank_diameter", "darcy_weisbach_roughness_height",
        "area", "volume",
        "volumetric_flow_rate", "base_demand_flow_rate", "customer_demand_flow_rate",
        "velocity", "hydraulic_head", "pressure_head", "water_level",
        "tank_initial_level", "tank_minimum_level", "tank_maximum_level",
        "head_gain", "head_loss", "head_loss_gradient", "elapsed_time",
        "hazen_williams_roughness_coefficient", "chezy_manning_roughness_coefficient",
        "minor_loss_coefficient", "pump_speed_ratio", "emitter_exponent",
        "mixing_fraction", "efficiency", "valve_position",
        "source_trace_percentage", "operating_time_percentage",
        "first_order_bulk_reaction_coefficient", "first_order_wall_reaction_coefficient",
        "zero_order_wall_reaction_coefficient", "leak_area_per_100m_pipe_length",
        "leak_area_expansion_per_pressure_head", "chemical_mass_concentration",
        "chemical_mass_flow_rate", "water_age"
    };
    for (const std::string_view quantity : gui_quantities) {
        if (field.key == quantity) return true;
    }
    return false;
}
}
