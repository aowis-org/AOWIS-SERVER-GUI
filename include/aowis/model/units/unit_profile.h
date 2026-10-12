#ifndef AOWIS_MODEL_UNITS_UNIT_PROFILE_H
#define AOWIS_MODEL_UNITS_UNIT_PROFILE_H

#include "canonical_registry.h"
#include <array>
#include <utility>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Qt-independent presentation-unit metadata and profile definitions.
// None of these profiles alter canonical values in the hydraulic model.
namespace aowis::units {
struct UnitField {
    std::string_view group;
    std::string_view key;
    std::string_view choices; // Semicolon-separated UCUM/display identifiers.
};

inline constexpr std::array unit_fields = {
    UnitField{"Lengths and geometry", "length", "m;ft;km;mi"},
    UnitField{"Lengths and geometry", "elevation", "m;ft"},
    UnitField{"Lengths and geometry", "altitude", "m;ft"},
    UnitField{"Lengths and geometry", "distance", "m;ft;km;mi"},
    UnitField{"Lengths and geometry", "vertical_offset", "m;ft"},
    UnitField{"Lengths and geometry", "link_diameter", "mm;in;cm;m;ft"},
    UnitField{"Lengths and geometry", "tank_diameter", "m;ft"},
    UnitField{"Lengths and geometry", "darcy_weisbach_roughness_height", "mm;in;millift;ft"},
    UnitField{"Lengths and geometry", "area", "m2;ft2;ha;acre"},
    UnitField{"Lengths and geometry", "volume", "m3;L;ft3;US gal;Imp gal"},
    UnitField{"Lengths and geometry", "tank_minimum_volume", "m3;L;ft3;US gal;Imp gal"},
    UnitField{"Coordinates", "latitude", "deg"},
    UnitField{"Coordinates", "longitude", "deg"},
    UnitField{"Coordinates", "projected_easting", "m;ft"},
    UnitField{"Coordinates", "projected_northing", "m;ft"},
    UnitField{"Coordinates", "local_x", "m;ft"},
    UnitField{"Coordinates", "local_y", "m;ft"},
    UnitField{"Flow and transport", "volumetric_flow_rate", "m3/h;L/s;L/min;ML/d;m3/d;m3/s;ft3/s;US gal/min;MUSgal/d;MImpgal/d;acre.ft/d"},
    UnitField{"Flow and transport", "base_demand_flow_rate", "m3/h;L/s;L/min;ML/d;m3/d;m3/s;ft3/s;US gal/min;MUSgal/d;MImpgal/d;acre.ft/d"},
    UnitField{"Flow and transport", "customer_demand_flow_rate", "m3/h;L/s;L/min;ML/d;m3/d;m3/s;ft3/s;US gal/min;MUSgal/d;MImpgal/d;acre.ft/d"},
    UnitField{"Flow and transport", "velocity", "m/s;ft/s"},
    UnitField{"Flow and transport", "molecular_diffusivity", "m2/s;cm2/s"},
    UnitField{"Flow and transport", "longitudinal_dispersion_coefficient", "m2/s;cm2/s"},
    UnitField{"Hydraulics and pressure", "hydraulic_head", "m;ft"},
    UnitField{"Hydraulics and pressure", "pressure_head", "m;ft;kPa;Pa;bar;psi"},
    UnitField{"Hydraulics and pressure", "water_level", "m;ft"},
    UnitField{"Hydraulics and pressure", "tank_initial_level", "m;ft"},
    UnitField{"Hydraulics and pressure", "tank_minimum_level", "m;ft"},
    UnitField{"Hydraulics and pressure", "tank_maximum_level", "m;ft"},
    UnitField{"Hydraulics and pressure", "head_gain", "m;ft"},
    UnitField{"Hydraulics and pressure", "head_loss", "m;ft"},
    UnitField{"Hydraulics and pressure", "head_loss_gradient", "m/km;ft/1000ft"},
    UnitField{"Hydraulics and pressure", "pressure", "kPa;Pa;bar;psi"},
    UnitField{"Hydraulics and pressure", "stress", "MPa;kPa;psi"},
    UnitField{"Time", "elapsed_time", "s;min;h;d"},
    UnitField{"Time", "duration", "s;min;h;d"},
    UnitField{"Time", "time_of_day", "s;min;h"},
    UnitField{"Dimensionless", "hazen_williams_roughness_coefficient", "1"},
    UnitField{"Dimensionless", "chezy_manning_roughness_coefficient", "1"},
    UnitField{"Dimensionless", "minor_loss_coefficient", "1"},
    UnitField{"Dimensionless", "darcy_weisbach_friction_factor", "1"},
    UnitField{"Dimensionless", "pump_speed_ratio", "1"},
    UnitField{"Dimensionless", "pattern_multiplier", "1"},
    UnitField{"Dimensionless", "demand_multiplier", "1"},
    UnitField{"Dimensionless", "pressure_exponent", "1"},
    UnitField{"Dimensionless", "emitter_exponent", "1"},
    UnitField{"Dimensionless", "reaction_order", "1"},
    UnitField{"Dimensionless", "specific_gravity", "1"},
    UnitField{"Dimensionless", "relative_viscosity", "1"},
    UnitField{"Dimensionless", "relative_diffusivity", "1"},
    UnitField{"Dimensionless", "peclet_number", "1"},
    UnitField{"Dimensionless", "mixing_fraction", "1"},
    UnitField{"Dimensionless", "hydraulic_accuracy", "1"},
    UnitField{"Dimensionless", "hydraulic_damping_limit", "1"},
    UnitField{"Dimensionless", "relative_error", "1"},
    UnitField{"Dimensionless", "flow_balance_ratio", "1"},
    UnitField{"Dimensionless", "quality_mass_balance_ratio", "1"},
    UnitField{"Percentages", "efficiency", "%;1"},
    UnitField{"Percentages", "relative_flow", "%;1"},
    UnitField{"Percentages", "valve_position", "%;1"},
    UnitField{"Percentages", "source_trace_percentage", "%;1"},
    UnitField{"Percentages", "operating_time_percentage", "%;1"},
    UnitField{"Percentages", "demand_reduction_percentage", "%;1"},
    UnitField{"Percentages", "leakage_loss_percentage", "%;1"},
    UnitField{"Reactions and leakage", "first_order_bulk_reaction_coefficient", "/d;/h;/s"},
    UnitField{"Reactions and leakage", "first_order_wall_reaction_coefficient", "m/d;ft/d"},
    UnitField{"Reactions and leakage", "zero_order_wall_reaction_coefficient", "mg/m2/d;mg/m2/h;mg/m2/s;g/m2/d"},
    UnitField{"Reactions and leakage", "leak_area_per_100m_pipe_length", "mm2/(100.m);in2/(100.ft)"},
    UnitField{"Reactions and leakage", "leak_area_expansion_per_pressure_head", "mm2/m;in2/ft"},
    UnitField{"Water quality", "chemical_mass_concentration", "mg/L;g/m3;ug/L"},
    UnitField{"Water quality", "chemical_amount_concentration", "mmol/L;mol/m3"},
    UnitField{"Water quality", "chemical_surface_mass_density", "mg/m2;g/m2"},
    UnitField{"Water quality", "chemical_surface_amount_density", "mmol/m2;mol/m2"},
    UnitField{"Water quality", "chemical_mass_flow_rate", "mg/min;g/min;g/h"},
    UnitField{"Water quality", "chemical_amount_flow_rate", "mmol/min;mol/min"},
    UnitField{"Water quality", "water_age", "h;d;min;s"},
    UnitField{"Power and electrical", "power", "kW;W;hp"},
    UnitField{"Power and electrical", "energy", "kW.h;J;MJ"},
    UnitField{"Power and electrical", "energy_intensity", "kW.h/m3;kW.h/ft3"},
    UnitField{"Power and electrical", "electric_current", "A;mA"},
    UnitField{"Power and electrical", "voltage", "V;mV;kV"},
    UnitField{"Power and electrical", "electrical_resistance", "Ohm;kOhm"},
    UnitField{"Power and electrical", "capacitance", "F;uF"},
};

inline constexpr std::array<std::string_view, 11> flow_template_names = {{
    "CMH", "LPS", "LPM", "MLD", "CMD", "CMS", "CFS", "GPM", "MGD", "IMGD", "AFD"
}};
inline constexpr std::array<std::string_view, 11> flow_template_units = {{
    "m3/h", "L/s", "L/min", "ML/d", "m3/d", "m3/s", "ft3/s", "US gal/min", "MUSgal/d", "MImpgal/d", "acre.ft/d"
}};

[[nodiscard]] constexpr const UnitField *findUnitField(std::string_view key) noexcept
{
    for (const UnitField &field : unit_fields)
        if (field.key == key) return &field;
    return nullptr;
}

[[nodiscard]] constexpr bool isAllowedUnit(std::string_view quantity, std::string_view unit) noexcept
{
    const UnitField *field = findUnitField(quantity);
    if (field == nullptr || unit.empty()) return false;
    std::string_view remaining = field->choices;
    while (!remaining.empty()) {
        const std::size_t pos = remaining.find(';');
        const std::string_view item = remaining.substr(0, pos);
        if (item == unit) return true;
        if (pos == std::string_view::npos) break;
        remaining.remove_prefix(pos + 1);
    }
    return false;
}

using UnitSelections = std::map<std::string, std::string, std::less<>>;

struct UnitProfile {
    std::string id; // Stable identifier; independent of the user-visible name.
    std::string name;
    UnitSelections overrides; // Missing entries fall back to canonical units.
    bool builtin = false;
};

[[nodiscard]] inline UnitSelections canonicalSelections()
{
    UnitSelections result;
    for (const UnitField &field : unit_fields)
        result.emplace(field.key, canonicalUnit(field.key));
    return result;
}

// Legacy profiles predate these independently configurable display quantities.
// Missing overrides inherit their corresponding general presentation choice.
// Explicit overrides (including the canonical unit) always take precedence.
[[nodiscard]] constexpr std::string_view inheritedUnitQuantity(std::string_view quantity) noexcept
{
    if (quantity == "base_demand_flow_rate" || quantity == "customer_demand_flow_rate")
        return "volumetric_flow_rate";
    if (quantity == "tank_initial_level" || quantity == "tank_minimum_level"
        || quantity == "tank_maximum_level")
        return "water_level";
    if (quantity == "tank_minimum_volume") return "volume";
    return {};
}

[[nodiscard]] inline std::string_view resolvedUnit(const UnitProfile &profile, std::string_view quantity)
{
    // Minimum tank volume is a volume measurement, not a separate display
    // preference. Ignore any legacy override while retaining its registry key
    // for imported source metadata and persisted-profile compatibility.
    if (quantity == "tank_minimum_volume") return resolvedUnit(profile, "volume");
    // Older imported profiles placed pressure preferences in the unused
    // physical-pressure field. Honor them until explicitly overridden.
    if (quantity == "pressure_head" && profile.overrides.find("pressure_head") == profile.overrides.end()) {
        const UnitSelections::const_iterator legacy = profile.overrides.find("pressure");
        if (legacy != profile.overrides.end() && isAllowedUnit("pressure_head", legacy->second))
            return legacy->second;
    }
    const UnitSelections::const_iterator it = profile.overrides.find(quantity);
    if (it != profile.overrides.end() && isAllowedUnit(quantity, it->second)) return it->second;
    const std::string_view inherited = inheritedUnitQuantity(quantity);
    if (!inherited.empty()) return resolvedUnit(profile, inherited);
    return canonicalUnit(quantity);
}

[[nodiscard]] inline bool setProfileUnit(UnitProfile &profile, std::string_view quantity, std::string_view unit)
{
    if (profile.builtin || !isAllowedUnit(quantity, unit)) return false;
    // A derived display choice must be able to explicitly select canonical units,
    // even if its legacy fallback is noncanonical (e.g. flow L/s, demand m3/h).
    // Legacy physical-pressure overrides act as a fallback for pressure head;
    // an explicit canonical-metre selection must still defeat that fallback.
    const bool legacy_pressure = quantity == "pressure_head"
        && profile.overrides.find("pressure") != profile.overrides.end();
    if (canonicalUnit(quantity) == unit && inheritedUnitQuantity(quantity).empty()
        && !legacy_pressure)
        profile.overrides.erase(std::string(quantity));
    else
        profile.overrides[std::string(quantity)] = std::string(unit);
    return true;
}

[[nodiscard]] inline UnitProfile canonicalProfile()
{
    return {"canonical", "AOWIS Canonical", {}, true};
}

[[nodiscard]] inline UnitProfile flowTemplateProfile(std::size_t index)
{
    if (index >= flow_template_names.size()) return {};
    UnitProfile result;
    result.id = std::string(flow_template_names[index]);
    result.name = result.id;
    result.builtin = true;
    const std::string_view flow = flow_template_units[index];
    if (flow != canonicalUnit("volumetric_flow_rate"))
        result.overrides.emplace("volumetric_flow_rate", flow);
    if (index >= 6) {
        // US/Imperial hydraulic display choices, not a blanket conversion of
        // every quantity that happens to permit international feet.
        // Projected/local coordinate units come from their CRS, which can
        // specify US survey feet rather than international feet.
        constexpr std::pair<std::string_view, std::string_view> imperial_units[] = {
            {"length", "ft"},
            {"elevation", "ft"},
            {"distance", "ft"},
            {"vertical_offset", "ft"},
            {"link_diameter", "in"},
            {"tank_diameter", "ft"},
            // EPANET's US Darcy-Weisbach roughness convention is 0.001 ft.
            {"darcy_weisbach_roughness_height", "millift"},
            {"area", "ft2"},
            {"volume", "ft3"},
            {"tank_minimum_volume", "ft3"},
            {"velocity", "ft/s"},
            {"hydraulic_head", "ft"},
            {"pressure_head", "psi"},
            {"water_level", "ft"},
            {"tank_initial_level", "ft"},
            {"tank_minimum_level", "ft"},
            {"tank_maximum_level", "ft"},
            {"head_gain", "ft"},
            {"head_loss", "ft"},
            {"head_loss_gradient", "ft/1000ft"},
            {"pressure", "psi"},
            {"stress", "psi"},
            {"first_order_wall_reaction_coefficient", "ft/d"},
            {"leak_area_per_100m_pipe_length", "in2/(100.ft)"},
            {"leak_area_expansion_per_pressure_head", "in2/ft"},
            {"power", "hp"},
            {"energy_intensity", "kW.h/ft3"},
        };
        for (const std::pair<std::string_view, std::string_view> &choice : imperial_units)
            result.overrides.emplace(choice.first, choice.second);
    }
    return result;
}

[[nodiscard]] inline std::vector<UnitProfile> predefinedProfiles()
{
    std::vector<UnitProfile> result;
    result.reserve(flow_template_names.size() + 1);
    result.push_back(canonicalProfile());
    for (std::size_t i = 0; i < flow_template_names.size(); ++i)
        result.push_back(flowTemplateProfile(i));
    return result;
}
// Runtime collection is independent of Qt and of any persistence backend.
class UnitProfileCollection {
public:
    UnitProfileCollection() : profiles_(predefinedProfiles()) {}
    [[nodiscard]] const std::vector<UnitProfile> &profiles() const noexcept { return this->profiles_; }
    [[nodiscard]] const std::string &activeId() const noexcept { return this->active_id_; }
    [[nodiscard]] const UnitProfile *find(std::string_view id) const noexcept {
        for (const UnitProfile &profile : this->profiles_)
            if (profile.id == id) return &profile;
        return nullptr;
    }
    [[nodiscard]] bool select(std::string_view id) {
        if (this->find(id) == nullptr) return false;
        this->active_id_ = std::string(id);
        return true;
    }
    [[nodiscard]] bool add(UnitProfile profile) {
        if (profile.builtin || profile.id.empty() || profile.name.empty() || this->find(profile.id) != nullptr)
            return false;
        for (const UnitProfile &existing : this->profiles_)
            if (existing.name == profile.name) return false;
        for (const UnitSelections::value_type &selection : profile.overrides)
            if (!isAllowedUnit(selection.first, selection.second)) return false;
        this->profiles_.push_back(std::move(profile));
        return true;
    }
    [[nodiscard]] bool rename(std::string_view id, std::string name) {
        if (name.empty()) return false;
        for (const UnitProfile &existing : this->profiles_)
            if (existing.name == name && existing.id != id) return false;
        for (UnitProfile &profile : this->profiles_) {
            if (profile.id != id || profile.builtin) continue;
            profile.name = std::move(name);
            return true;
        }
        return false;
    }
    [[nodiscard]] bool remove(std::string_view id) {
        for (std::vector<UnitProfile>::iterator it = this->profiles_.begin();
             it != this->profiles_.end(); ++it) {
            if (it->id != id || it->builtin) continue;
            this->profiles_.erase(it);
            if (this->active_id_ == id) this->active_id_ = "canonical";
            return true;
        }
        return false;
    }
    [[nodiscard]] bool setUnit(std::string_view id, std::string_view quantity, std::string_view unit) {
        for (UnitProfile &profile : this->profiles_)
            if (profile.id == id) return setProfileUnit(profile, quantity, unit);
        return false;
    }
private:
    std::vector<UnitProfile> profiles_;
    std::string active_id_ = "canonical";
};

} // namespace aowis::units

#endif // AOWIS_MODEL_UNITS_UNIT_PROFILE_H
