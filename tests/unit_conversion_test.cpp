#include <aowis/model/units/conversion.h>
#include <aowis/model/units/unit_profile.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <limits>
#include <functional>

namespace u = aowis::units;

int main()
{
    const std::function<void(bool)> check = [](bool condition) {
        if (!condition) {
            std::cerr << "Unit conversion regression check failed\n";
            std::exit(EXIT_FAILURE);
        }
    };
    const std::function<bool(double, double)> near = [](double actual, double expected) {
        return std::abs(actual - expected) <= 1e-12 * std::max(1.0, std::abs(expected));
    };
    check(near(u::feetToMetres(1.0), 0.3048));
    check(near(u::inchesToMillimetres(1.0), 25.4));
    check(near(u::cubicFeetToCubicMetres(1.0), 0.028316846592));
    check(near(u::usGallonsToCubicMetres(1.0), 0.003785411784));
    check(near(u::millionUsGallonsPerDayToCubicMetresPerHour(1.0), 157.725491));
    check(near(u::millionImperialGallonsPerDayToCubicMetresPerHour(1.0), 189.42041666666667));
    check(near(u::cubicFeetPerSecondToCubicMetresPerHour(1.0), 101.9406477312));
    check(near(u::usGallonsPerMinuteToCubicMetresPerHour(1.0), 0.22712470704));
    check(near(u::litresPerSecondToCubicMetresPerHour(1.0), 3.6));
    check(near(u::pascalsToMetresHead(9810.0, 1000.0, 9.81), 1.0));
    check(near(u::usSurveyFeetToMetres(3937.0), 1200.0));
    check(near(u::millifeetToMillimetres(1.0), 0.3048));
    // EPANET-JS roughness can be supplied in ft or millift; both are
    // independent display choices for canonical millimetre roughness.
    check(u::canonicalUnit("darcy_weisbach_roughness_height") == "mm");
    check(u::isAllowedUnit("darcy_weisbach_roughness_height", "ft"));
    check(u::isAllowedUnit("darcy_weisbach_roughness_height", "millift"));
    check(near(*u::convert("darcy_weisbach_roughness_height", 1.0, "ft", "mm"), 304.8));
    check(near(*u::convert("darcy_weisbach_roughness_height", 1.0, "millift", "mm"), 0.3048));
    u::UnitProfile roughness_profile{"roughness-ft", "Roughness in feet", {}, false};
    check(u::setProfileUnit(roughness_profile, "darcy_weisbach_roughness_height", "ft"));
    check(u::resolvedUnit(roughness_profile, "darcy_weisbach_roughness_height") == "ft");
    check(u::resolvedUnit(u::flowTemplateProfile(6),
                          "darcy_weisbach_roughness_height") == "millift");
    check(near(u::mechanicalHorsepowerToKilowatts(1.0), 0.7456998715822702));
    // Unit-profile metadata is canonical, complete, and Qt independent.
    check(u::unit_fields.size() == u::canonical_quantities.size());
    check(u::allProfileFieldsMatchCanonicalRegistry());
    for (const u::UnitField &field : u::unit_fields)
        check(u::isAllowedUnit(field.key, u::canonicalUnit(field.key)));
    std::vector<u::UnitProfile> presets = u::predefinedProfiles();
    check(presets.size() == 12);
    check(u::resolvedUnit(presets.at(0), "volumetric_flow_rate") == "m3/h");
    check(u::resolvedUnit(presets.at(1), "volumetric_flow_rate") == "m3/h");
    check(u::resolvedUnit(presets.at(8), "link_diameter") == "in");
    // The EPANET-JS diameter importer accepts feet. Imported source units
    // must be representable in the independent link-diameter display choice.
    check(u::isAllowedUnit("link_diameter", "ft"));
    check(near(*u::convert("link_diameter", 1.0, "ft", "mm"), 304.8));
    check(near(*u::convert("link_diameter", 304.8, "mm", "ft"), 1.0));
    u::UnitProfile diameter_profile{"diameter-ft", "Feet diameter", {}, false};
    check(u::setProfileUnit(diameter_profile, "link_diameter", "ft"));
    check(u::resolvedUnit(diameter_profile, "link_diameter") == "ft");
    check(u::resolvedUnit(presets.at(8), "pressure") == "psi");
    check(u::resolvedUnit(presets.at(8), "pressure_head") == "psi");
    check(u::isAllowedUnit("pressure_head", "kPa"));
    check(u::isAllowedUnit("pressure_head", "bar"));
    check(u::isAllowedUnit("pressure_head", "psi"));
    check(u::isAllowedUnit("pressure_head", "ft"));
    // Hydraulic head is still canonical metres. Pressure display uses physical
    // density x gravity, with explicit project-specific gravity when needed.
    const double one_metre_psi = 1000.0 * u::standard_gravity_m_per_s2 / u::pascals_per_psi;
    check(near(*u::convert("pressure_head", 1.0, "m", "psi"), one_metre_psi));
    check(near(*u::convert("pressure_head", 1.0, "m", "psi", 1.2), one_metre_psi * 1.2));
    check(near(*u::convert("pressure_head", 1.0, "m", "kPa", 1.2), 11.76798));
    check(near(*u::convert("pressure_head", 1.0, "m", "bar", 1.2), 0.1176798));
    check(near(*u::convert("pressure_head", one_metre_psi * 1.2, "psi", "m", 1.2), 1.0));
    check(near(*u::convert("pressure_head", 1.0, "ft", "kPa", 1.2),
               0.3048 * 11.76798));
    check(near(*u::convert("pressure_head", 1.0, "psi", "bar", 1.2),
               u::pascals_per_psi / u::pascals_per_bar));
    check(!u::convert("pressure_head", 1.0, "m", "psi", 0.0));
    check(!u::convert("pressure_head", 1.0, "m", "psi", -1.0));
    check(!u::convert("pressure_head", 1.0, "m", "psi",
                       std::numeric_limits<double>::quiet_NaN()));
    u::UnitProfile legacy_pressure{"legacy-pressure", "Legacy pressure", {{"pressure", "psi"}}, false};
    check(u::resolvedUnit(legacy_pressure, "pressure_head") == "psi");
    check(u::setProfileUnit(legacy_pressure, "pressure_head", "m"));
    check(u::resolvedUnit(legacy_pressure, "pressure_head") == "m");
    check(u::setProfileUnit(legacy_pressure, "pressure_head", "bar"));
    check(u::resolvedUnit(legacy_pressure, "pressure_head") == "bar");
    u::UnitProfile custom{"test", "Custom", {}, false};
    check(u::setProfileUnit(custom, "pressure", "psi"));
    check(u::resolvedUnit(custom, "pressure") == "psi");
    check(!u::setProfileUnit(custom, "pressure", "ft"));
    check(!u::setProfileUnit(presets.at(0), "pressure", "psi"));
    check(u::setProfileUnit(custom, "pressure", "kPa"));
    check(custom.overrides.empty());
    u::UnitProfileCollection collection;
    check(collection.profiles().size() == 12);
    check(collection.activeId() == "canonical");
    check(!collection.select("nonexistent"));
    check(collection.add({"custom-1", "Field choice", {{"pressure", "psi"}}, false}));
    check(!collection.add({"custom-1", "Duplicate", {}, false}));
    check(!collection.add({"invalid", "Bad unit", {{"pressure", "ft"}}, false}));
    check(collection.select("custom-1"));
    check(collection.rename("custom-1", "Renamed choice"));
    check(collection.setUnit("custom-1", "pressure", "bar"));
    check(u::resolvedUnit(*collection.find("custom-1"), "pressure") == "bar");
    check(!collection.setUnit("canonical", "pressure", "psi"));
    check(!collection.remove("canonical"));
    check(collection.remove("custom-1"));
    check(collection.activeId() == "canonical");
    // Check every primitive and representative inverse conversions.
    check(near(u::feetToMillimetres(1.0), 304.8));
    check(near(u::acreFeetPerDayToCubicMetresPerHour(1.0),
               43560.0 * 0.028316846592 / 24.0));
    check(near(u::litresPerMinuteToCubicMetresPerHour(1.0), 0.06));
    check(near(u::cubicMetresPerSecondToCubicMetresPerHour(1.0), 3600.0));
    check(near(u::cubicMetresPerDayToCubicMetresPerHour(1.0), 1.0 / 24.0));
    check(near(u::millionLitresPerDayToCubicMetresPerHour(1.0), 1000.0 / 24.0));
    check(near(u::psiToPascals(1.0), 6894.757293168361));
    check(near(u::kilopascalsToPascals(1.0), 1000.0));
    check(near(u::barToPascals(1.0), 100000.0));
    check(near(u::usGallonsToCubicMetres(264.1720523581484), 1.0));
    check(near(u::feetToMetres(1.0 / u::metres_per_international_foot), 1.0));
    check(near(u::inchesToMillimetres(1.0 / 25.4), 1.0));
    check(u::allProfileUnitsConvertible());
    for (const u::UnitField &field : u::unit_fields) {
        std::string_view choices = field.choices;
        while (!choices.empty()) {
            const std::size_t end = choices.find(';');
            const std::string_view unit = choices.substr(0, end);
            const std::optional<double> forward = u::fromCanonical(field.key, 123.456789, unit);
            check(forward.has_value());
            const std::optional<double> reverse = u::toCanonical(field.key, *forward, unit);
            check(reverse.has_value() && near(*reverse, 123.456789));
            if (end == std::string_view::npos) break;
            choices.remove_prefix(end + 1);
        }
    }
    // EPANET-JS can express water age in seconds; its source preference must
    // remain selectable without changing the canonical hours used by AOWIS.
    check(u::canonicalUnit("water_age") == "h");
    check(u::isAllowedUnit("water_age", "s"));
    check(near(*u::convert("water_age", 3600.0, "s", "h"), 1.0));
    check(near(*u::convert("water_age", 1.0, "h", "s"), 3600.0));
    u::UnitProfile seconds_age_profile{"age-seconds", "Age in seconds", {}, false};
    check(u::setProfileUnit(seconds_age_profile, "water_age", "s"));
    check(u::resolvedUnit(seconds_age_profile, "water_age") == "s");
    // Semantic display quantities share physical conversions but not preferences.
    check(u::canonicalUnit("base_demand_flow_rate") == "m3/h");
    check(u::canonicalUnit("customer_demand_flow_rate") == "m3/h");
    check(u::canonicalUnit("tank_minimum_volume") == "m3");
    check(u::canonicalUnit("tank_initial_level") == "m");
    check(u::isAllowedUnit("base_demand_flow_rate", "L/s"));
    check(u::isAllowedUnit("customer_demand_flow_rate", "US gal/min"));
    check(near(*u::convert("base_demand_flow_rate", 3.6, "m3/h", "L/s"), 1.0));
    check(near(*u::convert("customer_demand_flow_rate", 1.0, "m3/h", "US gal/min"),
               1.0 / u::usGallonsPerMinuteToCubicMetresPerHour(1.0)));
    u::UnitProfile legacy_flow{"legacy-flow", "Legacy flow", {}, false};
    check(u::setProfileUnit(legacy_flow, "volumetric_flow_rate", "L/s"));
    check(u::resolvedUnit(legacy_flow, "base_demand_flow_rate") == "L/s");
    check(u::resolvedUnit(legacy_flow, "customer_demand_flow_rate") == "L/s");
    check(u::setProfileUnit(legacy_flow, "base_demand_flow_rate", "m3/h"));
    check(u::resolvedUnit(legacy_flow, "base_demand_flow_rate") == "m3/h");
    check(u::resolvedUnit(legacy_flow, "customer_demand_flow_rate") == "L/s");
    check(u::setProfileUnit(legacy_flow, "customer_demand_flow_rate", "US gal/min"));
    check(u::resolvedUnit(legacy_flow, "customer_demand_flow_rate") == "US gal/min");
    check(legacy_flow.overrides.at("base_demand_flow_rate") == "m3/h");
    check(u::setProfileUnit(legacy_flow, "water_level", "ft"));
    check(u::resolvedUnit(legacy_flow, "tank_initial_level") == "ft");
    check(u::setProfileUnit(legacy_flow, "tank_minimum_level", "m"));
    check(u::resolvedUnit(legacy_flow, "tank_minimum_level") == "m");
    check(u::resolvedUnit(legacy_flow, "tank_maximum_level") == "ft");
    check(u::setProfileUnit(legacy_flow, "volume", "ft3"));
    check(u::resolvedUnit(legacy_flow, "tank_minimum_volume") == "ft3");
    // Legacy overrides may still exist, but cannot make a tank's minimum
    // volume display differently from other volumes.
    check(u::setProfileUnit(legacy_flow, "tank_minimum_volume", "m3"));
    check(u::resolvedUnit(legacy_flow, "tank_minimum_volume") == "ft3");
    check(u::setProfileUnit(legacy_flow, "volume", "m3"));
    check(u::resolvedUnit(legacy_flow, "tank_minimum_volume") == "m3");
    check(u::setProfileUnit(legacy_flow, "volume", "ft3"));
    check(u::resolvedUnit(legacy_flow, "tank_minimum_volume") == "ft3");
    const u::UnitField *area_field = u::findUnitField("area");
    const u::UnitField *volume_field = u::findUnitField("volume");
    check(area_field && area_field->group == "Lengths and geometry");
    check(volume_field && volume_field->group == "Lengths and geometry");
    check(u::resolvedUnit(u::flowTemplateProfile(1), "base_demand_flow_rate") == "L/s");
    check(u::resolvedUnit(u::flowTemplateProfile(1), "customer_demand_flow_rate") == "L/s");
    check(u::resolvedUnit(u::flowTemplateProfile(8), "tank_initial_level") == "ft");
    // US/Imperial flow presets use consistent non-metric hydraulic units.
    const std::pair<std::string_view, std::string_view> imperial_expected[] = {
        {"velocity", "ft/s"},
        {"area", "ft2"},
        {"volume", "ft3"},
        {"tank_minimum_volume", "ft3"},
        {"head_loss_gradient", "ft/1000ft"},
        {"first_order_wall_reaction_coefficient", "ft/d"},
        {"leak_area_per_100m_pipe_length", "in2/(100.ft)"},
        {"leak_area_expansion_per_pressure_head", "in2/ft"},
        {"energy_intensity", "kW.h/ft3"},
        {"stress", "psi"},
    };
    for (std::size_t index = 6; index < u::flow_template_names.size(); ++index) {
        const u::UnitProfile preset = u::flowTemplateProfile(index);
        for (const std::pair<std::string_view, std::string_view> &expected : imperial_expected) {
            check(u::resolvedUnit(preset, expected.first) == expected.second);
            check(u::isAllowedUnit(expected.first, expected.second));
            check(u::fromCanonical(expected.first, 10.0, preset).has_value());
        }
        // The projection / local coordinate system, not hydraulic flow units,
        // determines whether metre, international foot, or survey foot applies.
        const std::string_view coordinates[] = {
            "projected_easting", "projected_northing", "local_x", "local_y"
        };
        for (const std::string_view coordinate : coordinates) {
            check(preset.overrides.find(coordinate) == preset.overrides.end());
            check(u::resolvedUnit(preset, coordinate) == "m");
        }
        check(preset.overrides.find("altitude") == preset.overrides.end());
    }
    // Metric flow presets must not acquire Imperial geometry or stress.
    for (std::size_t index = 0; index < 6; ++index) {
        const u::UnitProfile preset = u::flowTemplateProfile(index);
        check(u::resolvedUnit(preset, "velocity") == "m/s");
        check(u::resolvedUnit(preset, "volume") == "m3");
        check(u::resolvedUnit(preset, "stress") == "MPa");
    }
    // Wall kinetics have different physical dimensions for order 0 and 1.
    check(near(*u::convert("zero_order_wall_reaction_coefficient", 24.0,
                           "mg/m2/d", "mg/m2/h"), 1.0));
    check(near(*u::convert("zero_order_wall_reaction_coefficient", 1000.0,
                           "mg/m2/d", "g/m2/d"), 1.0));
    check(near(*u::convert("first_order_wall_reaction_coefficient", 1.0,
                           "m/d", "ft/d"), 1.0 / u::metres_per_international_foot));
    check(!u::convert("zero_order_wall_reaction_coefficient", 1.0, "m/d", "mg/m2/d"));
    check(!u::convert("first_order_wall_reaction_coefficient", 1.0, "mg/m2/d", "m/d"));
    check(near(*u::convert("pressure", 1.0, "bar", "kPa"), 100.0));
    check(near(*u::convert("volume", 1.0, "US gal", "L"), 3.785411784));
    check(near(*u::convert("efficiency", 85.0, "%", "1"), 0.85));
    check(near(*u::convert("head_loss_gradient", 2.0, "m/km", "ft/1000ft"), 2.0));
    check(near(*u::convert("chemical_mass_concentration", 1.0, "mg/L", "g/m3"), 1.0));
    check(near(*u::convert("leak_area_per_100m_pipe_length", 1.0,
                           "in2/(100.ft)", "mm2/(100.m)"),
               645.16 / 0.3048));
    check(!u::convert("pressure", 1.0, "m", "kPa"));
    check(!u::convert("missing", 1.0, "m", "m"));
    check(!u::convert("pressure", std::numeric_limits<double>::infinity(), "kPa", "psi"));
    check(!u::convert("pressure", std::numeric_limits<double>::max(), "kPa", "Pa"));
    check(near(*u::fromCanonical("pressure", 100.0, u::flowTemplateProfile(8)),
               100000.0 / u::pascals_per_psi));

}
