#pragma once

#include <aowis/model/units/quantity.h>

#include "config/unit_table_presentation.h"

#include <QString>

// Only the presentation of these EPANET hydraulic statistics is variable.
// Their source values remain metres of head and cubic metres per hour.
namespace SimulationStatisticsUnits
{
inline QString selectedUnit(const UnitPresentationContext &context, bool head_error)
{
    return UnitTablePresentation::unitSymbol(UnitTablePresentation::selectedUnit(
        context, head_error ? AOWIS_QUANTITY("hydraulic_head") : AOWIS_QUANTITY("volumetric_flow_rate")));
}

inline QString format(const UnitPresentationContext &context, bool head_error, double canonical)
{
    const std::optional<double> displayed = UnitTablePresentation::toDisplay(
        context, head_error ? AOWIS_QUANTITY("hydraulic_head") : AOWIS_QUANTITY("volumetric_flow_rate"), canonical);
    if (!displayed)
        return QStringLiteral("—");
    return UnitTablePresentation::formatNumber(
        context, *displayed, 6, QString(), head_error ? AOWIS_QUANTITY("hydraulic_head") : AOWIS_QUANTITY("volumetric_flow_rate"));
}

inline QString formatWithUnit(const UnitPresentationContext &context, bool head_error,
                              double canonical)
{
    const QString value = format(context, head_error, canonical);
    return value == QStringLiteral("—") ? value : value + QStringLiteral(" ")
        + selectedUnit(context, head_error);
}
}
