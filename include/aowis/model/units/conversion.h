#ifndef AOWIS_MODEL_UNITS_CONVERSION_H
#define AOWIS_MODEL_UNITS_CONVERSION_H

#include "unit_profile.h"
#include <cmath>
#include <optional>
#include <string_view>

// Physical unit definitions only. Source-format interpretation belongs in adapters.
// Decimal definitions below are exact by international agreement; floating-point
// evaluation is subject to ordinary IEEE-754 rounding.
namespace aowis::units
{
inline constexpr double metres_per_international_foot = 0.3048;
inline constexpr double metres_per_us_survey_foot = 1200.0 / 3937.0;
inline constexpr double metres_per_inch = 0.0254;
inline constexpr double cubic_metres_per_us_gallon = 0.003785411784;
inline constexpr double cubic_metres_per_imperial_gallon = 0.00454609;
inline constexpr double pascals_per_psi = 6894.757293168361; // Derived from exact lbf/in² definitions.
inline constexpr double pascals_per_kilopascal = 1000.0;
inline constexpr double pascals_per_bar = 100000.0;
inline constexpr double seconds_per_minute = 60.0;
inline constexpr double minutes_per_hour = 60.0;
inline constexpr double hours_per_day = 24.0;
inline constexpr double standard_gravity_m_per_s2 = 9.80665;
inline constexpr double reference_water_density_kg_per_m3 = 1000.0;
// Mechanical horsepower (international): 550 ft*lbf/s, expressed in kW.
inline constexpr double kilowatts_per_mechanical_horsepower = 0.7456998715822702;
inline constexpr double seconds_per_hour = 3600.0;
inline constexpr double seconds_per_day = 86400.0;
inline constexpr double cubic_metres_per_cubic_foot =
    metres_per_international_foot * metres_per_international_foot * metres_per_international_foot;
inline constexpr double cubic_metres_per_acre_foot =
    43560.0 * cubic_metres_per_cubic_foot;

// One millifoot is 0.001 international foot; numerically equal to
// metres per foot when the destination is millimetres.
[[nodiscard]] constexpr double millifeetToMillimetres(double value) noexcept
{
    return value * metres_per_international_foot;
}
[[nodiscard]] constexpr double mechanicalHorsepowerToKilowatts(double value) noexcept
{
    return value * kilowatts_per_mechanical_horsepower;
}
[[nodiscard]] constexpr double feetToMetres(double value) noexcept
{
    return value * metres_per_international_foot;
}
[[nodiscard]] constexpr double usSurveyFeetToMetres(double value) noexcept
{
    return value * metres_per_us_survey_foot;
}
[[nodiscard]] constexpr double inchesToMillimetres(double value) noexcept
{
    return value * (metres_per_inch * 1000.0);
}
[[nodiscard]] constexpr double feetToMillimetres(double value) noexcept
{
    return feetToMetres(value) * 1000.0;
}
[[nodiscard]] constexpr double cubicFeetToCubicMetres(double value) noexcept
{
    return value * cubic_metres_per_cubic_foot;
}
[[nodiscard]] constexpr double usGallonsToCubicMetres(double value) noexcept
{
    return value * cubic_metres_per_us_gallon;
}
[[nodiscard]] constexpr double cubicMetresPerSecondToCubicMetresPerHour(double value) noexcept
{
    return value * seconds_per_hour;
}
[[nodiscard]] constexpr double litresPerSecondToCubicMetresPerHour(double value) noexcept
{
    return value * (seconds_per_hour / 1000.0);
}
[[nodiscard]] constexpr double litresPerMinuteToCubicMetresPerHour(double value) noexcept
{
    return value * (60.0 / 1000.0);
}
[[nodiscard]] constexpr double cubicMetresPerDayToCubicMetresPerHour(double value) noexcept
{
    return value / 24.0;
}
[[nodiscard]] constexpr double millionLitresPerDayToCubicMetresPerHour(double value) noexcept
{
    return value * (1000.0 / 24.0);
}
[[nodiscard]] constexpr double cubicFeetPerSecondToCubicMetresPerHour(double value) noexcept
{
    return value * cubic_metres_per_cubic_foot * seconds_per_hour;
}
[[nodiscard]] constexpr double usGallonsPerMinuteToCubicMetresPerHour(double value) noexcept
{
    return value * cubic_metres_per_us_gallon * 60.0;
}
[[nodiscard]] constexpr double millionUsGallonsPerDayToCubicMetresPerHour(double value) noexcept
{
    return value * 1000000.0 * cubic_metres_per_us_gallon / 24.0;
}
[[nodiscard]] constexpr double millionImperialGallonsPerDayToCubicMetresPerHour(double value) noexcept
{
    return value * 1000000.0 * cubic_metres_per_imperial_gallon / 24.0;
}
[[nodiscard]] constexpr double acreFeetPerDayToCubicMetresPerHour(double value) noexcept
{
    return value * cubic_metres_per_acre_foot / 24.0;
}
[[nodiscard]] constexpr double psiToPascals(double value) noexcept
{
    return value * pascals_per_psi;
}
[[nodiscard]] constexpr double kilopascalsToPascals(double value) noexcept
{
    return value * pascals_per_kilopascal;
}
[[nodiscard]] constexpr double barToPascals(double value) noexcept
{
    return value * pascals_per_bar;
}
// The caller supplies actual fluid density (kg/m³) and gravitational
// acceleration (m/s²); neither is implicitly fixed to an EPANET convention.
[[nodiscard]] constexpr double pascalsToMetresHead(
    double pressure_pascals, double density_kg_per_m3, double gravity_m_per_s2) noexcept
{
    return pressure_pascals / (density_kg_per_m3 * gravity_m_per_s2);
}

// Each scale converts a unit to the common physical reference for its dimension.
// Quantity validation is essential: the same scale does not make unrelated
// quantities interchangeable (e.g. length and hydraulic head).
struct UnitScale {
    std::string_view unit;
    double to_reference;
};
inline constexpr UnitScale unit_scales[] = {
    {"m", 1.0},
    {"ft", metres_per_international_foot},
    {"km", 1000.0},
    {"mi", 1609.344},
    {"mm", 0.001},
    {"cm", 0.01},
    {"in", metres_per_inch},
    {"millift", 0.0003048},
    {"deg", 1.0},
    {"m2", 1.0},
    {"ft2", metres_per_international_foot * metres_per_international_foot},
    {"ha", 10000.0},
    {"acre", 4046.8564224},
    {"m3", 1.0},
    {"L", 0.001},
    {"ft3", cubic_metres_per_cubic_foot},
    {"US gal", cubic_metres_per_us_gallon},
    {"Imp gal", cubic_metres_per_imperial_gallon},
    {"m3/h", 1.0 / seconds_per_hour},
    {"L/s", 0.001},
    {"L/min", 0.001 / seconds_per_minute},
    {"ML/d", 1000.0 / seconds_per_day},
    {"m3/d", 1.0 / seconds_per_day},
    {"m3/s", 1.0},
    {"ft3/s", cubic_metres_per_cubic_foot},
    {"US gal/min", cubic_metres_per_us_gallon / seconds_per_minute},
    {"MUSgal/d", 1000000.0 * cubic_metres_per_us_gallon / seconds_per_day},
    {"MImpgal/d", 1000000.0 * cubic_metres_per_imperial_gallon / seconds_per_day},
    {"acre.ft/d", cubic_metres_per_acre_foot / seconds_per_day},
    {"m/s", 1.0},
    {"ft/s", metres_per_international_foot},
    {"m2/s", 1.0},
    {"cm2/s", 0.0001},
    {"m/km", 0.001},
    {"ft/1000ft", 0.001},
    {"kPa", 1000.0},
    {"Pa", 1.0},
    {"bar", pascals_per_bar},
    {"psi", pascals_per_psi},
    {"MPa", 1000000.0},
    {"s", 1.0},
    {"min", seconds_per_minute},
    {"h", seconds_per_hour},
    {"d", seconds_per_day},
    {"1", 1.0},
    {"%", 0.01},
    {"/d", 1.0 / seconds_per_day},
    {"/h", 1.0 / seconds_per_hour},
    {"/s", 1.0},
    {"m/d", 1.0 / seconds_per_day},
    {"ft/d", metres_per_international_foot / seconds_per_day},
    // Zero-order wall reactions: mass per surface area per time, NOT length/time.
    {"mg/m2/d", 0.000001 / seconds_per_day},
    {"mg/m2/h", 0.000001 / seconds_per_hour},
    {"mg/m2/s", 0.000001},
    {"g/m2/d", 0.001 / seconds_per_day},
    {"mm2/(100.m)", 0.001 * 0.001 / 100.0},
    {"in2/(100.ft)", metres_per_inch * metres_per_inch / (100.0 * metres_per_international_foot)},
    {"mm2/m", 0.001 * 0.001},
    {"in2/ft", metres_per_inch * metres_per_inch / metres_per_international_foot},
    {"mg/L", 0.001},
    {"g/m3", 0.001},
    {"ug/L", 0.000001},
    {"mmol/L", 1.0},
    {"mol/m3", 1.0},
    {"mg/m2", 0.000001},
    {"g/m2", 0.001},
    {"mmol/m2", 0.001},
    {"mol/m2", 1.0},
    {"mg/min", 0.000001 / seconds_per_minute},
    {"g/min", 0.001 / seconds_per_minute},
    {"g/h", 0.001 / seconds_per_hour},
    {"mmol/min", 0.001 / seconds_per_minute},
    {"mol/min", 1.0 / seconds_per_minute},
    {"kW", 1000.0},
    {"W", 1.0},
    {"hp", kilowatts_per_mechanical_horsepower * 1000.0},
    {"kW.h", 3600000.0},
    {"J", 1.0},
    {"MJ", 1000000.0},
    {"kW.h/m3", 3600000.0},
    {"kW.h/ft3", 3600000.0 / cubic_metres_per_cubic_foot},
    {"A", 1.0},
    {"mA", 0.001},
    {"V", 1.0},
    {"mV", 0.001},
    {"kV", 1000.0},
    {"Ohm", 1.0},
    {"kOhm", 1000.0},
    {"F", 1.0},
    {"uF", 0.000001},
};

[[nodiscard]] constexpr std::optional<double> unitScale(std::string_view unit) noexcept
{
    for (const UnitScale &entry : unit_scales)
        if (entry.unit == unit) return entry.to_reference;
    return std::nullopt;
}

// Returns nullopt for unsupported quantities/units and non-finite results.
// Units must be explicitly allowed for the given registered quantity.
[[nodiscard]] inline std::optional<double> convert(
    std::string_view quantity, double value,
    std::string_view from_unit, std::string_view to_unit,
    double specific_gravity = 1.0) noexcept
{
    if (!isAllowedUnit(quantity, from_unit) || !isAllowedUnit(quantity, to_unit)
        || !std::isfinite(value)) return std::nullopt;
    if (quantity == "pressure_head" &&
        (!std::isfinite(specific_gravity) || specific_gravity <= 0.0)) return std::nullopt;
    if (from_unit == to_unit) return value;
    const std::optional<double> from = unitScale(from_unit);
    const std::optional<double> to = unitScale(to_unit);
    if (!from || !to || *to == 0.0) return std::nullopt;
    // Hydraulic head is stored in metres, but may be *presented* as physical
    // pressure. Convert using fluid density and standard gravity, never by
    // mixing the length and pressure dimensions' reference scales directly.
    double from_scale = *from;
    double to_scale = *to;
    if (quantity == "pressure_head") {
        const double metres_per_pascal = pascalsToMetresHead(
            1.0, specific_gravity * reference_water_density_kg_per_m3,
            standard_gravity_m_per_s2);
        if (from_unit != "m" && from_unit != "ft") from_scale *= metres_per_pascal;
        if (to_unit != "m" && to_unit != "ft") to_scale *= metres_per_pascal;
    }
    const double result = (value * from_scale) / to_scale;
    if (!std::isfinite(result)) return std::nullopt;
    return result;
}

[[nodiscard]] inline std::optional<double> toCanonical(
    std::string_view quantity, double value, std::string_view source_unit) noexcept
{
    return convert(quantity, value, source_unit, canonicalUnit(quantity));
}

[[nodiscard]] inline std::optional<double> fromCanonical(
    std::string_view quantity, double value, std::string_view destination_unit) noexcept
{
    return convert(quantity, value, canonicalUnit(quantity), destination_unit);
}

[[nodiscard]] inline std::optional<double> fromCanonical(
    std::string_view quantity, double value, const UnitProfile &profile) noexcept
{
    return fromCanonical(quantity, value, resolvedUnit(profile, quantity));
}

[[nodiscard]] inline std::optional<double> toCanonical(
    std::string_view quantity, double value, const UnitProfile &profile) noexcept
{
    return toCanonical(quantity, value, resolvedUnit(profile, quantity));
}

// The presentation fields must correspond exactly to canonical quantities,
// and every canonical unit must remain selectable in its field.
[[nodiscard]] constexpr bool allProfileFieldsMatchCanonicalRegistry() noexcept
{
    if (unit_fields.size() != canonical_quantities.size()) return false;
    for (std::size_t index = 0; index < unit_fields.size(); ++index) {
        const UnitField &field = unit_fields[index];
        const CanonicalQuantity &canonical = canonical_quantities[index];
        if (field.key != canonical.quantity || !isAllowedUnit(field.key, canonical.unit))
            return false;
    }
    return true;
}

// Compile-time guard: additions to the selectable-unit catalog must have scales.
[[nodiscard]] constexpr bool allProfileUnitsConvertible() noexcept
{
    for (const UnitField &field : unit_fields) {
        std::string_view choices = field.choices;
        while (!choices.empty()) {
            const std::size_t end = choices.find(';');
            if (!unitScale(choices.substr(0, end))) return false;
            if (end == std::string_view::npos) break;
            choices.remove_prefix(end + 1);
        }
    }
    return true;
}
static_assert(allProfileFieldsMatchCanonicalRegistry(),
              "Unit-profile fields must match the canonical registry and offer their canonical units");
static_assert(allProfileUnitsConvertible(), "Unmapped selectable unit in AOWIS registry");

}

#endif
