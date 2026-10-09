#include "import/epanet_js_units.h"

#include <cmath>

namespace EpanetJsUnits
{
QString normalizedUnit(QString unit)
{
    unit = unit.trimmed().toLower();
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
        return value * 3600.0;
    if (normalized == QStringLiteral("l/s") || normalized == QStringLiteral("lps"))
        return value * 3.6;
    if (normalized == QStringLiteral("l/min") || normalized == QStringLiteral("lpm"))
        return value * 0.06;
    if (normalized == QStringLiteral("m^3/d") || normalized == QStringLiteral("m3/d") ||
        normalized == QStringLiteral("cmd"))
        return value / 24.0;
    if (normalized == QStringLiteral("mld"))
        return value * (1000.0 / 24.0);
    if (normalized == QStringLiteral("ft^3/s") || normalized == QStringLiteral("ft3/s") ||
        normalized == QStringLiteral("cfs"))
        return value * 101.9406477312;
    if (normalized == QStringLiteral("gpm") || normalized == QStringLiteral("gal/min")
        || normalized == QStringLiteral("usgal/min"))
        return value * 0.22712470704;
    if (normalized == QStringLiteral("mgd"))
        return value * 157.725491;
    if (normalized == QStringLiteral("imgd"))
        return value * 189.42041666666667;
    if (normalized == QStringLiteral("acre-ft/d") || normalized == QStringLiteral("afd"))
        return value * 51.39507656448;
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
        return value * 0.3048;
    return std::nullopt;
}

std::optional<double> volumeToM3(double value, const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("m^3") || normalized == QStringLiteral("m3"))
        return value;
    if (normalized == QStringLiteral("ft^3") || normalized == QStringLiteral("ft3"))
        return value * 0.028316846592;
    if (normalized == QStringLiteral("gal") || normalized == QStringLiteral("usgal"))
        return value * 0.003785411784;
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
        return value * 25.4;
    if (normalized == QStringLiteral("ft") || normalized == QStringLiteral("foot")
        || normalized == QStringLiteral("feet"))
        return value * 304.8;
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
        return value * 0.3048;
    if (normalized == QStringLiteral("ft") || normalized == QStringLiteral("foot")
        || normalized == QStringLiteral("feet"))
        return value * 304.8;
    if (normalized.isEmpty())
    {
        if (flowUnitUsesMetricLength(flow_unit))
            return value;
        if (flowUnitUsesFootLength(flow_unit))
            return value * 0.3048;
    }
    return std::nullopt;
}

std::optional<double> chemicalConcentrationScaleToMgPerL(const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("mg/l"))
        return 1.0;
    if (normalized == QStringLiteral("ug/l") || normalized == QStringLiteral("µg/l"))
        return 0.001;
    return std::nullopt;
}

std::optional<double> pressureToHeadM(
    double value,
    const QString &unit,
    double specific_gravity)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("m") || normalized == QStringLiteral("mwc") ||
        normalized == QStringLiteral("mh2o"))
        return value;
    if (normalized == QStringLiteral("ft") || normalized == QStringLiteral("feet"))
        return value * 0.3048;
    if (!std::isfinite(specific_gravity) || specific_gravity <= 0.0)
        return std::nullopt;
    if (normalized == QStringLiteral("psi"))
        return value / (0.4333 * specific_gravity) * 0.3048;
    if (normalized == QStringLiteral("kpa"))
        return value / (6.895 * 0.4333 * specific_gravity) * 0.3048;
    if (normalized == QStringLiteral("bar"))
        return value / (0.068948 * 0.4333 * specific_gravity) * 0.3048;
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
