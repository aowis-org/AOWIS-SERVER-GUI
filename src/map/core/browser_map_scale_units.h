#pragma once

#include "config/unit_quantity_key.h"

#include "config/unit_presentation_context.h"
#include <aowis/model/units/conversion.h>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>
#include <limits>
#include <functional>

namespace BrowserMapScaleUnits
{
// Build once per presentation generation. The browser selects the largest
// readable 1/2/5 distance fitting its viewport, without calling back on pan.
inline QJsonArray choices(const UnitPresentationContext &context)
{
    QString unit = context.unit(AOWIS_QSTRING_QUANTITY("distance"));
    if (unit.isEmpty())
        unit = QStringLiteral("m");
    QString compact;
    if (unit == QStringLiteral("m"))
        compact = QStringLiteral("km");
    else if (unit == QStringLiteral("ft"))
        compact = QStringLiteral("mi");

    double compact_threshold = std::numeric_limits<double>::infinity();
    if (!compact.isEmpty())
    {
        const std::optional<double> threshold = aowis::units::convert(
            AOWIS_QUANTITY("distance"), 1.0, compact.toStdString(), "m");
        if (threshold)
            compact_threshold = *threshold;
    }

    QJsonArray result;
    const std::function<void(const QString &, double, double)> append =
        [&context, &result](const QString &symbol, double minimum, double maximum) {
        for (int exponent = -12; exponent <= 12; ++exponent)
        {
            for (const double factor : {1.0, 2.0, 5.0})
            {
                const double value = factor * std::pow(10.0, exponent);
                const std::optional<double> meters = aowis::units::convert(
                    AOWIS_QUANTITY("distance"), value, symbol.toStdString(), "m");
                if (!meters || !std::isfinite(*meters) || *meters <= 0.0
                    || *meters < minimum || *meters >= maximum)
                    continue;
                result.append(QJsonObject{
                    {QStringLiteral("meters"), *meters},
                    {QStringLiteral("label"), context.locale().toString(value, 'g', 12)
                        + QStringLiteral(" ") + symbol}});
            }
        }
    };
    append(unit, 0.0, compact_threshold);
    if (!compact.isEmpty())
        append(compact, compact_threshold, std::numeric_limits<double>::infinity());
    return result;
}
}
