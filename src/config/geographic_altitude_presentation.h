#pragma once

#include "config/unit_presentation_context.h"
#include <aowis/model/units/conversion.h>

#include <cmath>
#include <optional>

// Geographic/GPS or DEM altitude is not a hydraulic node elevation, and it
// never inherits pipe length, CRS horizontal-axis units or network offset.
namespace GeographicAltitudePresentation
{
[[nodiscard]] inline QString format(const UnitPresentationContext &context,
                                   const std::optional<double> &altitude_m,
                                   const NumericFormatPolicy &policy)
{
    if (!altitude_m || !std::isfinite(*altitude_m))
        return QString(QChar(0x2014));

    QString unit = context.unit(QStringLiteral("altitude"));
    if (unit.isEmpty()) unit = QStringLiteral("m");
    const std::optional<double> displayed = aowis::units::convert(
        "altitude", *altitude_m, "m", unit.toStdString());
    if (!displayed) return QString(QChar(0x2014));
    const std::optional<QString> formatted = NumericText::measurement(
        displayed, unit, context.locale(), policy);
    return formatted.value_or(QString(QChar(0x2014)));
}

[[nodiscard]] inline QString format(const UnitPresentationContext &context,
                                   const std::optional<double> &altitude_m)
{
    return format(context, altitude_m, context.formatPolicy(QStringLiteral("altitude")));
}
}
