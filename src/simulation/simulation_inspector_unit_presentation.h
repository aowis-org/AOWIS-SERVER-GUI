#pragma once

#include "config/unit_table_presentation.h"

#include <string_view>

// Read-only formatting of canonical simulation-result values. Do not store
// displayed measurements back into HydraulicData or solver result timelines.
namespace SimulationInspectorUnitPresentation
{
inline QString format(const UnitPresentationContext &context, std::string_view quantity,
                      double canonical, int decimals = 3)
{
    const std::optional<double> converted =
        UnitTablePresentation::toDisplay(context, quantity, canonical);
    if (!converted.has_value())
        return QStringLiteral("—");

    const QString symbol = UnitTablePresentation::unitSymbol(
        UnitTablePresentation::selectedUnit(context, quantity));
    return UnitTablePresentation::formatNumber(
        context, *converted, decimals, (symbol.isEmpty() || symbol == QStringLiteral("1"))
            ? QString() : QStringLiteral(" ") + symbol, quantity);
}
}
