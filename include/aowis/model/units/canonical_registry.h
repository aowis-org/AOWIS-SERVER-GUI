#ifndef AOWIS_MODEL_UNITS_CANONICAL_REGISTRY_H
#define AOWIS_MODEL_UNITS_CANONICAL_REGISTRY_H

#include <array>
#include <string_view>

// Authoritative AOWIS quantity identifiers and canonical UCUM unit identifiers.
// Presentation profiles and alternative unit choices belong to the GUI.
namespace aowis::units
{
struct CanonicalQuantity
{
    std::string_view quantity;
    std::string_view unit;
};

inline constexpr std::array canonical_quantities = {
    CanonicalQuantity{"length", "m"},
    CanonicalQuantity{"elevation", "m"},
    CanonicalQuantity{"altitude", "m"},
    CanonicalQuantity{"distance", "m"},
    CanonicalQuantity{"vertical_offset", "m"},
    CanonicalQuantity{"link_diameter", "mm"},
    CanonicalQuantity{"tank_diameter", "m"},
    CanonicalQuantity{"darcy_weisbach_roughness_height", "mm"},
    CanonicalQuantity{"area", "m2"},
    CanonicalQuantity{"volume", "m3"},
    CanonicalQuantity{"tank_minimum_volume", "m3"},
    CanonicalQuantity{"latitude", "deg"},
    CanonicalQuantity{"longitude", "deg"},
    CanonicalQuantity{"projected_easting", "m"},
    CanonicalQuantity{"projected_northing", "m"},
    CanonicalQuantity{"local_x", "m"},
    CanonicalQuantity{"local_y", "m"},
    CanonicalQuantity{"volumetric_flow_rate", "m3/h"},
    CanonicalQuantity{"base_demand_flow_rate", "m3/h"},
    CanonicalQuantity{"customer_demand_flow_rate", "m3/h"},
    CanonicalQuantity{"velocity", "m/s"},
    CanonicalQuantity{"molecular_diffusivity", "m2/s"},
    CanonicalQuantity{"longitudinal_dispersion_coefficient", "m2/s"},
    CanonicalQuantity{"hydraulic_head", "m"},
    CanonicalQuantity{"pressure_head", "m"},
    CanonicalQuantity{"water_level", "m"},
    CanonicalQuantity{"tank_initial_level", "m"},
    CanonicalQuantity{"tank_minimum_level", "m"},
    CanonicalQuantity{"tank_maximum_level", "m"},
    CanonicalQuantity{"head_gain", "m"},
    CanonicalQuantity{"head_loss", "m"},
    CanonicalQuantity{"head_loss_gradient", "m/km"},
    CanonicalQuantity{"pressure", "kPa"},
    CanonicalQuantity{"stress", "MPa"},
    CanonicalQuantity{"elapsed_time", "s"},
    CanonicalQuantity{"duration", "s"},
    CanonicalQuantity{"time_of_day", "s"},
    CanonicalQuantity{"hazen_williams_roughness_coefficient", "1"},
    CanonicalQuantity{"chezy_manning_roughness_coefficient", "1"},
    CanonicalQuantity{"minor_loss_coefficient", "1"},
    CanonicalQuantity{"darcy_weisbach_friction_factor", "1"},
    CanonicalQuantity{"pump_speed_ratio", "1"},
    CanonicalQuantity{"pattern_multiplier", "1"},
    CanonicalQuantity{"demand_multiplier", "1"},
    CanonicalQuantity{"pressure_exponent", "1"},
    CanonicalQuantity{"emitter_exponent", "1"},
    CanonicalQuantity{"reaction_order", "1"},
    CanonicalQuantity{"specific_gravity", "1"},
    CanonicalQuantity{"relative_viscosity", "1"},
    CanonicalQuantity{"relative_diffusivity", "1"},
    CanonicalQuantity{"peclet_number", "1"},
    CanonicalQuantity{"mixing_fraction", "1"},
    CanonicalQuantity{"hydraulic_accuracy", "1"},
    CanonicalQuantity{"hydraulic_damping_limit", "1"},
    CanonicalQuantity{"relative_error", "1"},
    CanonicalQuantity{"flow_balance_ratio", "1"},
    CanonicalQuantity{"quality_mass_balance_ratio", "1"},
    CanonicalQuantity{"efficiency", "%"},
    CanonicalQuantity{"relative_flow", "%"},
    CanonicalQuantity{"valve_position", "%"},
    CanonicalQuantity{"source_trace_percentage", "%"},
    CanonicalQuantity{"operating_time_percentage", "%"},
    CanonicalQuantity{"demand_reduction_percentage", "%"},
    CanonicalQuantity{"leakage_loss_percentage", "%"},
    CanonicalQuantity{"first_order_bulk_reaction_coefficient", "/d"},
    CanonicalQuantity{"first_order_wall_reaction_coefficient", "m/d"},
    CanonicalQuantity{"zero_order_wall_reaction_coefficient", "mg/m2/d"},
    CanonicalQuantity{"leak_area_per_100m_pipe_length", "mm2/(100.m)"},
    CanonicalQuantity{"leak_area_expansion_per_pressure_head", "mm2/m"},
    CanonicalQuantity{"chemical_mass_concentration", "mg/L"},
    CanonicalQuantity{"chemical_amount_concentration", "mmol/L"},
    CanonicalQuantity{"chemical_surface_mass_density", "mg/m2"},
    CanonicalQuantity{"chemical_surface_amount_density", "mmol/m2"},
    CanonicalQuantity{"chemical_mass_flow_rate", "mg/min"},
    CanonicalQuantity{"chemical_amount_flow_rate", "mmol/min"},
    CanonicalQuantity{"water_age", "h"},
    CanonicalQuantity{"power", "kW"},
    CanonicalQuantity{"energy", "kW.h"},
    CanonicalQuantity{"energy_intensity", "kW.h/m3"},
    CanonicalQuantity{"electric_current", "A"},
    CanonicalQuantity{"voltage", "V"},
    CanonicalQuantity{"electrical_resistance", "Ohm"},
    CanonicalQuantity{"capacitance", "F"},
};

[[nodiscard]] constexpr std::string_view canonicalUnit(std::string_view quantity) noexcept
{
    for (const CanonicalQuantity &entry : canonical_quantities) {
        if (entry.quantity == quantity)
            return entry.unit;
    }
    return {};
}
} // namespace aowis::units

#endif // AOWIS_MODEL_UNITS_CANONICAL_REGISTRY_H
