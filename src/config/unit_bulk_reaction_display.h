#pragma once

#include "numeric_text.h"
#include <QString>
#include <cmath>

// The stored bulk-reaction coefficient uses (mg/L)^(1-order)/day.
// A negative order is EPANET Michaelis-Menten kinetics (mg/L/day).
// The profile's /d, /h, /s selection changes ONLY the inverse-time
// dimension; concentration stays in AOWIS's canonical mg/L basis.
namespace UnitBulkReactionDisplay
{
[[nodiscard]] inline QString suffix(double order, const QString &inverse_time)
{
    if (!std::isfinite(order))
        return NumericText::suffix(QStringLiteral("invalid reaction order"));
    if (std::abs(order - 1.0) <= 1e-12)
        return NumericText::suffix(inverse_time);
    if (order < 0.0 || std::abs(order) <= 1e-12)
        return NumericText::suffix(QStringLiteral("(mg/L)") + inverse_time);
    const QString exponent = QString::number(1.0 - order, 'g', 12);
    return NumericText::suffix(QStringLiteral("(mg/L)^") + exponent + inverse_time);
}
}
