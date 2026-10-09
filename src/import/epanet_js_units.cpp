#include "import/epanet_js_units.h"

#include <cmath>
#include <aowis/model/units/conversion.h>

namespace EpanetJsUnits
{
namespace units = aowis::units;

QString normalizedUnit(QString unit)
{
    unit = unit.trimmed();
    // Preserve the case of metric prefixes: M (mega) is not m (milli).
    // Normalize recognized flow-unit spellings before generic case folding.
    if (unit == QStringLiteral("Ml/d") || unit == QStringLiteral("ML/d")
        || unit == QStringLiteral("Mgal/d") || unit == QStringLiteral("MGal/d"))
        return unit == QStringLiteral("Ml/d") || unit == QStringLiteral("ML/d")
            ? QStringLiteral("mld") : QStringLiteral("mgd");
    unit = unit.toLower();
    unit.remove(QLatin1Char(' '));
    unit.replace(QStringLiteral("³"), QStringLiteral("^3"));
    return unit;
}

std::optional<double> flowToM3PerH(double value, const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("m^3/h") || normalized == QStringLiteral("m3/h") ||
        normalized == QStringLiteral("cmh"))
        return value;
    if (normalized == QStringLiteral("m^3/s") || normalized == QStringLiteral("m3/s") ||
        normalized == QStringLiteral("cms"))
        return units::cubicMetresPerSecondToCubicMetresPerHour(value);
    if (normalized == QStringLiteral("l/s") || normalized == QStringLiteral("lps"))
        return units::litresPerSecondToCubicMetresPerHour(value);
    if (normalized == QStringLiteral("l/min") || normalized == QStringLiteral("lpm"))
        return units::litresPerMinuteToCubicMetresPerHour(value);
    if (normalized == QStringLiteral("m^3/d") || normalized == QStringLiteral("m3/d") ||
        normalized == QStringLiteral("cmd"))
        return units::cubicMetresPerDayToCubicMetresPerHour(value);
    if (normalized == QStringLiteral("mld"))
        return units::millionLitresPerDayToCubicMetresPerHour(value);
    if (normalized == QStringLiteral("ft^3/s") || normalized == QStringLiteral("ft3/s") ||
        normalized == QStringLiteral("cfs"))
        return units::cubicFeetPerSecondToCubicMetresPerHour(value);
    if (normalized == QStringLiteral("gpm") || normalized == QStringLiteral("gal/min")
        || normalized == QStringLiteral("usgal/min"))
        return units::usGallonsPerMinuteToCubicMetresPerHour(value);
    if (normalized == QStringLiteral("mgd"))
        return units::millionUsGallonsPerDayToCubicMetresPerHour(value);
    if (normalized == QStringLiteral("imgd"))
        return units::millionImperialGallonsPerDayToCubicMetresPerHour(value);
    if (normalized == QStringLiteral("acre-ft/d") || normalized == QStringLiteral("afd"))
        return units::acreFeetPerDayToCubicMetresPerHour(value);
    return std::nullopt;
}

std::optional<double> lengthToM(double value, const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("m") || normalized == QStringLiteral("meter") ||
        normalized == QStringLiteral("metre"))
        return value;
    if (normalized == QStringLiteral("ft") || normalized == QStringLiteral("foot") ||
        normalized == QStringLiteral("feet"))
        return units::feetToMetres(value);
    return std::nullopt;
}

std::optional<double> volumeToM3(double value, const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("m^3") || normalized == QStringLiteral("m3"))
        return value;
    if (normalized == QStringLiteral("ft^3") || normalized == QStringLiteral("ft3"))
        return units::cubicFeetToCubicMetres(value);
    if (normalized == QStringLiteral("gal") || normalized == QStringLiteral("usgal"))
        return units::usGallonsToCubicMetres(value);
    return std::nullopt;
}


std::optional<double> diameterToMm(double value, const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("mm") || normalized == QStringLiteral("millimeter")
        || normalized == QStringLiteral("millimetre"))
        return value;
    if (normalized == QStringLiteral("cm"))
        return value * 10.0;
    if (normalized == QStringLiteral("m") || normalized == QStringLiteral("meter")
        || normalized == QStringLiteral("metre"))
        return value * 1000.0;
    if (normalized == QStringLiteral("in") || normalized == QStringLiteral("inch")
        || normalized == QStringLiteral("inches"))
        return units::inchesToMillimetres(value);
    if (normalized == QStringLiteral("ft") || normalized == QStringLiteral("foot")
        || normalized == QStringLiteral("feet"))
        return units::feetToMillimetres(value);
    return std::nullopt;
}


bool flowUnitUsesMetricLength(const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    return normalized == QStringLiteral("l/s") || normalized == QStringLiteral("lps") ||
        normalized == QStringLiteral("l/min") || normalized == QStringLiteral("lpm") ||
        normalized == QStringLiteral("mld") || normalized == QStringLiteral("m^3/h") ||
        normalized == QStringLiteral("m3/h") || normalized == QStringLiteral("cmh") ||
        normalized == QStringLiteral("m^3/d") || normalized == QStringLiteral("m3/d") ||
        normalized == QStringLiteral("cmd") || normalized == QStringLiteral("m^3/s") ||
        normalized == QStringLiteral("m3/s") || normalized == QStringLiteral("cms");
}

bool flowUnitUsesFootLength(const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    return normalized == QStringLiteral("ft^3/s") || normalized == QStringLiteral("ft3/s") ||
        normalized == QStringLiteral("cfs") || normalized == QStringLiteral("gpm") ||
        normalized == QStringLiteral("gal/min") || normalized == QStringLiteral("usgal/min") ||
        normalized == QStringLiteral("mgd") || normalized == QStringLiteral("imgd") ||
        normalized == QStringLiteral("acre-ft/d") || normalized == QStringLiteral("afd");
}

std::optional<double> darcyRoughnessToMm(
    double value,
    const QString &roughness_unit,
    const QString &flow_unit)
{
    const QString normalized = normalizedUnit(roughness_unit);
    if (normalized == QStringLiteral("mm") || normalized == QStringLiteral("millimeter")
        || normalized == QStringLiteral("millimetre"))
        return value;
    if (normalized == QStringLiteral("millift") || normalized == QStringLiteral("millifeet")
        || normalized == QStringLiteral("milli-ft") || normalized == QStringLiteral("0.001ft"))
        return units::millifeetToMillimetres(value);
    if (normalized == QStringLiteral("ft") || normalized == QStringLiteral("foot")
        || normalized == QStringLiteral("feet"))
        return units::feetToMillimetres(value);
    if (normalized.isEmpty())
    {
        if (flowUnitUsesMetricLength(flow_unit))
            return value;
        if (flowUnitUsesFootLength(flow_unit))
            return units::millifeetToMillimetres(value);
    }
    return std::nullopt;
}

std::optional<double> chemicalConcentrationScaleToMgPerL(const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("mg/l"))
        return 1.0;
    if (normalized == QStringLiteral("ug/l") || normalized == QStringLiteral("µg/l") || normalized == QStringLiteral("μg/l"))
        return 0.001;
    return std::nullopt;
}

// Physical pressure head with reference water density 1000 kg/m^3 and
// standard gravity. EPANET-specific legacy pressure conventions remain
// the responsibility of their respective format adapters.
std::optional<double> pressureToHeadM(
    double value,
    const QString &unit,
    double specific_gravity)
{
    const QString normalized = normalizedUnit(unit);
    if (!std::isfinite(specific_gravity) || specific_gravity <= 0.0)
        return std::nullopt;
    if (normalized == QStringLiteral("m") || normalized == QStringLiteral("mwc") ||
        normalized == QStringLiteral("mh2o"))
        return value / specific_gravity;
    if (normalized == QStringLiteral("ft") || normalized == QStringLiteral("feet"))
        return units::feetToMetres(value) / specific_gravity;
    if (normalized == QStringLiteral("psi"))
        return units::pascalsToMetresHead(units::psiToPascals(value),
                                           units::reference_water_density_kg_per_m3 * specific_gravity,
                                           units::standard_gravity_m_per_s2);
    if (normalized == QStringLiteral("kpa"))
        return units::pascalsToMetresHead(units::kilopascalsToPascals(value),
                                           units::reference_water_density_kg_per_m3 * specific_gravity,
                                           units::standard_gravity_m_per_s2);
    if (normalized == QStringLiteral("bar"))
        return units::pascalsToMetresHead(units::barToPascals(value),
                                           units::reference_water_density_kg_per_m3 * specific_gravity,
                                           units::standard_gravity_m_per_s2);
    return std::nullopt;
}

std::optional<double> emitterCoefficientToCanonical(
    double source_coefficient,
    double pressure_exponent,
    const QString &flow_unit,
    const QString &pressure_unit,
    double specific_gravity)
{
    if (!std::isfinite(source_coefficient) || source_coefficient < 0.0
        || !std::isfinite(pressure_exponent) || pressure_exponent <= 0.0)
        return std::nullopt;
    if (source_coefficient == 0.0)
        return 0.0;

    const std::optional<double> flow_scale = flowToM3PerH(1.0, flow_unit);
    const std::optional<double> pressure_scale = pressureToHeadM(
        1.0, pressure_unit, specific_gravity);
    if (!flow_scale.has_value() || !pressure_scale.has_value()
        || !std::isfinite(*pressure_scale) || *pressure_scale <= 0.0)
        return std::nullopt;

    const double denominator = std::pow(*pressure_scale, pressure_exponent);
    if (!std::isfinite(denominator) || denominator <= 0.0)
        return std::nullopt;
    return source_coefficient * *flow_scale / denominator;
}

}
