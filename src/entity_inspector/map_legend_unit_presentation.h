#pragma once

#include "config/unit_presentation_context.h"
#include "config/unit_table_presentation.h"

#include <aowis/model/units/conversion.h>

#include <QString>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

// Map symbology ranges are canonical. Only the numbers shown beside their
// color ramps are converted: the normalized colors/geometry remain unchanged.
namespace MapLegendUnitPresentation
{
struct Range
{
    double minimum = 0.0;
    double maximum = 0.0;
    QString unit;
};

inline std::string_view quantityForMetric(const QString &metric)
{
    if (metric == QStringLiteral("Elevation")) return "elevation";
    if (metric == QStringLiteral("Head")) return "hydraulic_head";
    if (metric == QStringLiteral("Pressure")) return "pressure_head";
    if (metric == QStringLiteral("Head Loss")) return "head_loss";
    if (metric == QStringLiteral("Diameter")) return "link_diameter";
    if (metric == QStringLiteral("Length")) return "length";
    if (metric == QStringLiteral("Absolute Roughness ε"))
        return "darcy_weisbach_roughness_height";
    if (metric == QStringLiteral("Base Demand")) return "base_demand_flow_rate";
    if (metric == QStringLiteral("Flow Rate") || metric == QStringLiteral("Total Demand") || metric == QStringLiteral("Demand Deficit")
        || metric == QStringLiteral("Emitter Flow") || metric == QStringLiteral("Leakage"))
        return "volumetric_flow_rate";
    if (metric == QStringLiteral("Velocity")) return "velocity";
    if (metric == QStringLiteral("Cl₂") || metric == QStringLiteral("Chlorine"))
        return "chemical_mass_concentration";
    if (metric == QStringLiteral("Water Age")) return "water_age";
    if (metric == QStringLiteral("River Water") || metric == QStringLiteral("Lake Water"))
        return "source_trace_percentage";
    return {};
}

inline QString canonicalSymbol(const QString &metric)
{
    const std::string_view quantity = quantityForMetric(metric);
    if (quantity.empty()) return {};
    const std::string_view canonical = aowis::units::canonicalUnit(quantity);
    return UnitTablePresentation::unitSymbol(
        QString::fromUtf8(canonical.data(), static_cast<int>(canonical.size())));
}

inline Range range(const UnitPresentationContext &context, const QString &metric,
                   double minimum, double maximum)
{
    const std::string_view quantity = quantityForMetric(metric);
    Range result{minimum, maximum, canonicalSymbol(metric)};
    if (quantity.empty()) return result;

    const std::string_view source = aowis::units::canonicalUnit(quantity);
    QString chosen = UnitTablePresentation::selectedUnit(context, quantity);
    if (metric == QStringLiteral("Length")
        && std::isfinite(minimum) && std::isfinite(maximum))
    {
        const double largest = std::max(std::abs(minimum), std::abs(maximum));
        if (chosen == QStringLiteral("m") && largest >= 1000.0)
            chosen = QStringLiteral("km");
        else if (chosen == QStringLiteral("ft") && largest >= 1609.344)
            chosen = QStringLiteral("mi");
    }
    const std::string target = chosen.toStdString();
    const std::optional<double> lower = aowis::units::convert(
        quantity, minimum, source, target, context.specificGravity());
    const std::optional<double> upper = aowis::units::convert(
        quantity, maximum, source, target, context.specificGravity());
    if (!lower || !upper) return result; // Never attach a wrong unit to raw data.

    result.minimum = *lower;
    result.maximum = *upper;
    result.unit = UnitTablePresentation::unitSymbol(chosen);
    return result;
}

inline QString unit(const UnitPresentationContext &context, const QString &metric)
{
    const std::string_view quantity = quantityForMetric(metric);
    if (quantity.empty()) return {};
    return UnitTablePresentation::unitSymbol(
        UnitTablePresentation::selectedUnit(context, quantity));
}
}
