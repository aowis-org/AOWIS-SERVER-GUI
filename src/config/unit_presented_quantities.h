#pragma once

#include <aowis/model/units/quantity.h>

#include <aowis/model/units/unit_profile.h>

#include <string_view>

// Quantities presented by current GUI editors, tables, legends or readouts;
// used by Settings and by import profile matching. Keep all electrical
// options available for future GUI consumers. This is a presentation filter,
// never a registry or profile-storage filter: saved choices remain intact.
namespace UnitPresentedQuantities
{
[[nodiscard]] constexpr bool isPresented(const aowis::units::UnitField &field) noexcept
{
    if (field.group == "Power and electrical") return true;

    constexpr std::string_view gui_quantities[] = {
        AOWIS_QUANTITY("length"), AOWIS_QUANTITY("elevation"), AOWIS_QUANTITY("altitude"), AOWIS_QUANTITY("distance"),
        AOWIS_QUANTITY("vertical_offset"), AOWIS_QUANTITY("link_diameter"),
        AOWIS_QUANTITY("tank_diameter"), AOWIS_QUANTITY("darcy_weisbach_roughness_height"),
        AOWIS_QUANTITY("area"), AOWIS_QUANTITY("volume"),
        AOWIS_QUANTITY("volumetric_flow_rate"), AOWIS_QUANTITY("base_demand_flow_rate"),
        AOWIS_QUANTITY("customer_demand_flow_rate"),
        AOWIS_QUANTITY("velocity"), AOWIS_QUANTITY("hydraulic_head"), AOWIS_QUANTITY("pressure_head"), AOWIS_QUANTITY("water_level"),
        AOWIS_QUANTITY("tank_initial_level"), AOWIS_QUANTITY("tank_minimum_level"), AOWIS_QUANTITY("tank_maximum_level"),
        AOWIS_QUANTITY("head_gain"), AOWIS_QUANTITY("head_loss"), AOWIS_QUANTITY("head_loss_gradient"), AOWIS_QUANTITY("elapsed_time"),
        AOWIS_QUANTITY("hazen_williams_roughness_coefficient"),
        AOWIS_QUANTITY("chezy_manning_roughness_coefficient"),
        AOWIS_QUANTITY("minor_loss_coefficient"), AOWIS_QUANTITY("pump_speed_ratio"), AOWIS_QUANTITY("emitter_exponent"),
        AOWIS_QUANTITY("mixing_fraction"), AOWIS_QUANTITY("efficiency"), AOWIS_QUANTITY("valve_position"),
        AOWIS_QUANTITY("source_trace_percentage"), AOWIS_QUANTITY("operating_time_percentage"),
        AOWIS_QUANTITY("first_order_bulk_reaction_coefficient"),
        AOWIS_QUANTITY("first_order_wall_reaction_coefficient"),
        AOWIS_QUANTITY("zero_order_wall_reaction_coefficient"),
        AOWIS_QUANTITY("leak_area_per_100m_pipe_length"),
        AOWIS_QUANTITY("leak_area_expansion_per_pressure_head"),
        AOWIS_QUANTITY("chemical_mass_concentration"),
        AOWIS_QUANTITY("chemical_mass_flow_rate"), AOWIS_QUANTITY("water_age")
    };
    for (const std::string_view quantity : gui_quantities) {
        if (field.key == quantity) return true;
    }
    return false;
}
}
