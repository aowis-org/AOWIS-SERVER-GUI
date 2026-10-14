#pragma once

#include "config/unit_quantity_key.h"

#include "config/unit_presentation_context.h"
#include <aowis/model/units/conversion.h>

#include <QString>
#include <cmath>
#include <optional>
#include <string>

// Camera navigation state and slider positions always remain in metres. Unit
// preferences affect these HUD strings only; formatting must never set a model value.
namespace MapHudLengthUnits
{
inline QString format(double meters, const UnitPresentationContext &context,
                      bool camera_distance)
{
    if (!std::isfinite(meters))
        return QStringLiteral("—");

    const QString quantity = camera_distance
        ? AOWIS_QSTRING_QUANTITY("distance") : AOWIS_QSTRING_QUANTITY("vertical_offset");
    QString unit = context.unit(quantity);
    if (unit.isEmpty())
        unit = QStringLiteral("m");

    if (camera_distance)
    {
        if (unit == QStringLiteral("m") && std::abs(meters) >= 10000.0)
            unit = QStringLiteral("km");
        else if (unit == QStringLiteral("ft") && std::abs(meters) >= 1609.344)
            unit = QStringLiteral("mi");
    }

    const std::optional<double> converted = aowis::units::convert(
        quantity.toStdString(), meters, "m", unit.toStdString());
    if (!converted || !std::isfinite(*converted))
        return QStringLiteral("—");

    // Controls are intentionally coarse. Use enough places to express 0.1 m
    // changes in compact km/mi units, without pretending the slider is exact.
    const int decimals = camera_distance
        ? (unit == QStringLiteral("km") || unit == QStringLiteral("mi") ? 1 : 0)
        : (unit == QStringLiteral("km") || unit == QStringLiteral("mi") ? 4 : 1);
    return context.locale().toString(*converted, 'f', decimals)
        + QStringLiteral(" ") + unit;
}

inline QString cameraDistance(double meters, const UnitPresentationContext &context)
{
    return format(meters, context, true);
}

inline QString networkOffset(double meters, const UnitPresentationContext &context)
{
    return format(meters, context, false);
}
}
