#pragma once

#include "config/unit_presentation_context.h"
#include <aowis/model/units/conversion.h>

#include <QString>
#include <cmath>
#include <optional>
#include <string>

// Map symbology sizes are stored in metres. These values affect tooltip
// presentation only: slider steps and renderer sizes are never converted back.
namespace MapSymbologyLengthUnits
{
struct Presentation
{
    QString unit = QStringLiteral("m");
    double metres_to_display = 1.0;
    int decimals = 0;
};

inline Presentation forContext(const UnitPresentationContext &context)
{
    Presentation result;
    const QString requested = context.unit(QStringLiteral("length"));
    if (requested.isEmpty() || requested == QStringLiteral("m"))
        return result;

    const std::optional<double> converted = aowis::units::convert(
        "length", 1.0, "m", requested.toStdString());
    if (!converted || !std::isfinite(*converted) || *converted <= 0.0)
        return result;

    result.unit = requested;
    result.metres_to_display = *converted;
    // The slider remains an integer-metre control. The tooltip should show
    // a meaningful difference between neighbouring slider positions even
    // for km/mi, without claiming the underlying slider has finer steps.
    result.decimals = result.metres_to_display >= 1.0
        ? (result.metres_to_display == 1.0 ? 0 : 1)
        : qBound(1, int(std::ceil(-std::log10(result.metres_to_display))) + 1, 9);
    return result;
}
}
