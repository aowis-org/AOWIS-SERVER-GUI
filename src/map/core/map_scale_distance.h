#pragma once

#include "config/unit_quantity_key.h"

#include "config/unit_presentation_context.h"
#include <aowis/model/units/conversion.h>

#include <QString>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

// Distance values and map geometry remain in metres. Only the scale-bar
// presentation uses the selected unit; no stored model values are changed.
namespace MapScaleDistance
{
struct Scale
{
    double distance_m = 0.0;
    QString label;
};

inline double roundDownNice(double maximum)
{
    if (!(maximum > 0.0) || !std::isfinite(maximum))
        return 0.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(maximum)));
    if (!(magnitude > 0.0) || !std::isfinite(magnitude))
        return 0.0;
    const double normalized = maximum / magnitude;
    const double multiplier = normalized >= 5.0 ? 5.0
        : normalized >= 3.0 ? 3.0 : normalized >= 2.0 ? 2.0 : 1.0;
    return multiplier * magnitude;
}

inline Scale forMaximumDistance(double maximum_distance_m,
                                const UnitPresentationContext &context)
{
    Scale result;
    if (!(maximum_distance_m > 0.0) || !std::isfinite(maximum_distance_m))
        return result;

    QString unit = context.unit(AOWIS_QSTRING_QUANTITY("distance"));
    if (unit.isEmpty())
        unit = QStringLiteral("m");

    // Choose an appropriately sized member of the selected unit family,
    // while respecting explicit km/mi profiles without overriding them.
    if (unit == QStringLiteral("m") && maximum_distance_m >= 1000.0)
        unit = QStringLiteral("km");
    else if (unit == QStringLiteral("ft") && maximum_distance_m >= 1609.344)
        unit = QStringLiteral("mi");

    const std::string unit_id = unit.toStdString();
    const std::optional<double> maximum_in_unit = aowis::units::convert(
        AOWIS_QUANTITY("distance"), maximum_distance_m, "m", unit_id);
    if (!maximum_in_unit || !(*maximum_in_unit > 0.0))
        return result;

    const double rounded = roundDownNice(*maximum_in_unit);
    if (!(rounded > 0.0))
        return result;
    const std::optional<double> rounded_m = aowis::units::convert(
        AOWIS_QUANTITY("distance"), rounded, unit_id, "m");
    if (!rounded_m || !(*rounded_m > 0.0)
        || *rounded_m > maximum_distance_m * (1.0 + 1e-12))
        return result;

    result.distance_m = *rounded_m;
    const double nearest_integer = std::round(rounded);
    const bool integral = std::abs(rounded - nearest_integer)
        <= 1e-10 * std::max(1.0, std::abs(rounded));
    const QString number = context.locale().toString(
        rounded, integral ? 'f' : 'g', integral ? 0 : 3);
    result.label = number + QStringLiteral(" ") + unit;
    return result;
}
}
