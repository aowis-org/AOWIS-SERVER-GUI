#pragma once

#include "config/unit_table_presentation.h"

#include <QString>

// Only the presentation of these EPANET hydraulic statistics is variable.
// Their source values remain metres of head and cubic metres per hour.
namespace SimulationStatisticsUnits
{
inline QString selectedUnit(const UnitPresentationContext &context, bool head_error)
{
    return UnitTablePresentation::unitSymbol(UnitTablePresentation::selectedUnit(
        context, head_error ? "hydraulic_head" : "volumetric_flow_rate"));
}

inline QString format(const UnitPresentationContext &context, bool head_error, double canonical)
{
    const std::optional<double> displayed = UnitTablePresentation::toDisplay(
        context, head_error ? "hydraulic_head" : "volumetric_flow_rate", canonical);
    if (!displayed)
        return QStringLiteral("—");
    return UnitTablePresentation::formatNumber(*displayed, 6);
}

inline QString formatWithUnit(const UnitPresentationContext &context, bool head_error,
                              double canonical)
{
    const QString value = format(context, head_error, canonical);
    return value == QStringLiteral("—") ? value : value + QStringLiteral(" ")
        + selectedUnit(context, head_error);
}
}
