#include <aowis/model/units/conversion.h>
#include <aowis/model/units/unit_profile.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <limits>

namespace u = aowis::units;

int main()
{
    const auto check = [](bool condition) {
        if (!condition) {
            std::cerr << "Unit conversion regression check failed\n";
            std::exit(EXIT_FAILURE);
        }
    };
    const auto near = [](double actual, double expected) {
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
    check(near(u::mechanicalHorsepowerToKilowatts(1.0), 0.7456998715822702));
    // Unit-profile metadata is canonical, complete, and Qt independent.
    check(u::unit_fields.size() == u::canonical_quantities.size());
    for (const u::UnitField &field : u::unit_fields)
        check(u::isAllowedUnit(field.key, u::canonicalUnit(field.key)));
    std::vector<u::UnitProfile> presets = u::predefinedProfiles();
    check(presets.size() == 12);
    check(u::resolvedUnit(presets.at(0), "volumetric_flow_rate") == "m3/h");
    check(u::resolvedUnit(presets.at(1), "volumetric_flow_rate") == "m3/h");
    check(u::resolvedUnit(presets.at(8), "link_diameter") == "in");
    check(u::resolvedUnit(presets.at(8), "pressure") == "psi");
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
            const auto end = choices.find(';');
            const std::string_view unit = choices.substr(0, end);
            const auto forward = u::fromCanonical(field.key, 123.456789, unit);
            check(forward.has_value());
            const auto reverse = u::toCanonical(field.key, *forward, unit);
            check(reverse.has_value() && near(*reverse, 123.456789));
            if (end == std::string_view::npos) break;
            choices.remove_prefix(end + 1);
        }
    }
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
