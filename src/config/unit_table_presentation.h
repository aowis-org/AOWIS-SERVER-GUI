#pragma once

#include "config/unit_presentation_context.h"
#include "config/unit_energy_price_conversion.h"
#include "config/unit_bulk_reaction_display.h"

#include <aowis/model/units/canonical_registry.h>
#include <aowis/model/units/conversion.h>

#include <QString>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

// Quantity-aware, read-only table presentation. The original sortable/filterable
// values remain canonical; conversion happens only when a cell is requested.
namespace UnitTablePresentation
{
inline std::string_view quantityForTitle(const QString &title)
{
    if (title.endsWith(QStringLiteral("[m³/h]"))) {
        if (title.startsWith(QStringLiteral("Customer Demand Base")))
            return "customer_demand_flow_rate";
        if (title.startsWith(QStringLiteral("Demand Base")))
            return "base_demand_flow_rate";
        return "volumetric_flow_rate";
    }
    if (title.endsWith(QStringLiteral("[m³]"))) {
        if (title.startsWith(QStringLiteral("Volume Minimum")))
            return "tank_minimum_volume";
        return "volume";
    }
    if (title.endsWith(QStringLiteral("[m²]"))) return "area";
    if (title.endsWith(QStringLiteral("[mm²/100m]"))) return "leak_area_per_100m_pipe_length";
    if (title.endsWith(QStringLiteral("[mm²/m head]"))) return "leak_area_expansion_per_pressure_head";
    if (title.endsWith(QStringLiteral("[m/km]"))) return "head_loss_gradient";
    if (title.endsWith(QStringLiteral("[m/s]"))) return "velocity";
    if (title.endsWith(QStringLiteral("[kW]"))) return "power";
    if (title.endsWith(QStringLiteral("[kWh/m³]"))) return "energy_intensity";
    if (title.endsWith(QStringLiteral("[mg/L]"))) return "chemical_mass_concentration";
    if (title.endsWith(QStringLiteral("[mg/min]"))) return "chemical_mass_flow_rate";
    if (title.endsWith(QStringLiteral("[h]"))) return "water_age";
    if (title.endsWith(QStringLiteral("[mm]"))) {
        if (title.contains(QStringLiteral("Roughness DW")))
            return "darcy_weisbach_roughness_height";
        return "link_diameter";
    }
    if (title.endsWith(QStringLiteral("[m]"))) {
        if (title.contains(QStringLiteral("Offset"))) return "vertical_offset";
        if (title.contains(QStringLiteral("Diameter"))) return "tank_diameter";
        if (title.startsWith(QStringLiteral("Level Initial"))) return "tank_initial_level";
        if (title.startsWith(QStringLiteral("Level Minimum"))) return "tank_minimum_level";
        if (title.startsWith(QStringLiteral("Level Maximum"))) return "tank_maximum_level";
        if (title.startsWith(QStringLiteral("Level"))) return "water_level";
        if (title.contains(QStringLiteral("Pressure"))) return "pressure_head";
        if (title.contains(QStringLiteral("Head Loss"))) return "head_loss";
        if (title.contains(QStringLiteral("Head Gain"))) return "head_gain";
        if (title.contains(QStringLiteral("Head"))) return "hydraulic_head";
        if (title.contains(QStringLiteral("Elevation"))) return "elevation";
        return "length";
    }
    if (title.endsWith(QStringLiteral("[%]"))) {
        if (title.contains(QStringLiteral("Trace"))) return "source_trace_percentage";
        if (title.contains(QStringLiteral("Time Online"))) return "operating_time_percentage";
        if (title.contains(QStringLiteral("Efficiency"))) return "efficiency";
        // Pipe attachment position is a geometry fraction, not a valve position.
    }
    return {};
}

inline QString unitSymbol(QString identifier)
{
    identifier.replace(QStringLiteral("mm2"), QStringLiteral("mm²"));
    identifier.replace(QStringLiteral("m3"), QStringLiteral("m³"));
    identifier.replace(QStringLiteral("m2"), QStringLiteral("m²"));
    identifier.replace(QStringLiteral("ug"), QStringLiteral("µg"));
    identifier.replace(QStringLiteral("kW.h"), QStringLiteral("kWh"));
    return identifier;
}

inline QString selectedUnit(const UnitPresentationContext &context,
                            std::string_view quantity)
{
    const QString key = QString::fromUtf8(quantity.data(), static_cast<int>(quantity.size()));
    const QString chosen = context.unit(key);
    if (!chosen.isEmpty())
        return chosen;
    return QString::fromUtf8(aowis::units::canonicalUnit(quantity).data(),
                             static_cast<int>(aowis::units::canonicalUnit(quantity).size()));
}

inline std::optional<double> toDisplay(const UnitPresentationContext &context,
                                       std::string_view quantity, double canonical)
{
    if (quantity.empty()) return canonical;
    const std::string_view canonical_unit = aowis::units::canonicalUnit(quantity);
    const std::string target = selectedUnit(context, quantity).toStdString();
    return aowis::units::convert(quantity, canonical, canonical_unit, target);
}

inline std::optional<double> toCanonical(const UnitPresentationContext &context,
                                         std::string_view quantity, double displayed)
{
    if (quantity.empty()) return displayed;
    const std::string_view canonical_unit = aowis::units::canonicalUnit(quantity);
    const std::string source = selectedUnit(context, quantity).toStdString();
    return aowis::units::convert(quantity, displayed, source, canonical_unit);
}

// Pump prices are inverse-energy quantities, not energies. Keep the currency
// literal and replace only the energy denominator in the header.
inline bool isEnergyPriceTitle(const QString &title)
{
    return title.startsWith(QStringLiteral("Energy Price ["))
        && title.endsWith(QStringLiteral("/kWh]"));
}

inline std::optional<double> toDisplayForTitle(const UnitPresentationContext &context,
                                                const QString &title, double canonical)
{
    if (isEnergyPriceTitle(title))
        return UnitEnergyPriceConversion::fromStored(
            canonical, selectedUnit(context, "energy").toStdString());
    return toDisplay(context, quantityForTitle(title), canonical);
}

inline std::optional<double> toCanonicalForTitle(const UnitPresentationContext &context,
                                                  const QString &title, double displayed)
{
    if (isEnergyPriceTitle(title))
        return UnitEnergyPriceConversion::toStored(
            displayed, selectedUnit(context, "energy").toStdString());
    return toCanonical(context, quantityForTitle(title), displayed);
}

// Reaction coefficients cannot be assigned a fixed table-column unit:
// different rows may have different orders and therefore dimensions.
enum class ReactionKind { Bulk, Wall };

inline std::string_view reactionQuantity(ReactionKind kind, double order)
{
    if (!std::isfinite(order)) return {};
    if (kind == ReactionKind::Bulk) return "first_order_bulk_reaction_coefficient";
    if (std::abs(order - 1.0) <= 1e-12) return "first_order_wall_reaction_coefficient";
    if (std::abs(order) <= 1e-12) return "zero_order_wall_reaction_coefficient";
    return {}; // EPANET wall reactions only allow orders zero and one.
}

inline QString reactionUnit(const UnitPresentationContext &context,
                           ReactionKind kind, double order)
{
    const std::string_view quantity = reactionQuantity(kind, order);
    if (quantity.empty()) return QString();
    if (kind == ReactionKind::Bulk)
        return UnitBulkReactionDisplay::suffix(
            order, selectedUnit(context, quantity)).trimmed();
    return unitSymbol(selectedUnit(context, quantity));
}

inline std::optional<double> reactionValue(const UnitPresentationContext &context,
                                           ReactionKind kind, double order,
                                           double canonical)
{
    const std::string_view quantity = reactionQuantity(kind, order);
    if (quantity.empty()) return std::nullopt;
    return toDisplay(context, quantity, canonical);
}

inline QString reactionColumnTooltip(const UnitPresentationContext &context,
                                     ReactionKind kind)
{
    if (kind == ReactionKind::Bulk)
        return QStringLiteral("Bulk reaction coefficient: units depend on reaction order. "
                              "Concentration basis is mg/L; selected time basis: ")
            + unitSymbol(selectedUnit(context, "first_order_bulk_reaction_coefficient"))
            + QStringLiteral(". Each row displays its own unit.");
    return QStringLiteral("Wall reaction coefficient: units depend on reaction order. "
                          "First order: ")
        + reactionUnit(context, ReactionKind::Wall, 1.0)
        + QStringLiteral("; zero order: ")
        + reactionUnit(context, ReactionKind::Wall, 0.0)
        + QStringLiteral(". Each row displays its own unit.");
}

inline QString reactionCellTooltip(const UnitPresentationContext &context,
                                  ReactionKind kind, double order)
{
    if (reactionQuantity(kind, order).empty())
        return QStringLiteral("Invalid or unsupported reaction order: ")
            + QString::number(order, 'g', 12);
    return QStringLiteral("Reaction order: ") + QString::number(order, 'g', 12)
        + QStringLiteral("\nDisplay unit: ") + reactionUnit(context, kind, order);
}

// Format only a presentation value; stored doubles are never rounded here.
inline QString formatNumber(double value, int decimals, const QString &suffix = QString())
{
    if (!std::isfinite(value))
        return QStringLiteral("—");
    QString text = QString::number(value, 'f', decimals);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    // Rounding is presentation-only, but a nonzero measurement must not be
    // displayed as an exact zero. Use significant digits when fixed precision
    // would hide the entire magnitude (including small negative values).
    if (value != 0.0 && (text == QStringLiteral("0") || text == QStringLiteral("-0")))
        return QString::number(value, 'g', 12) + suffix;
    if (text == QStringLiteral("-0"))
        text = QStringLiteral("0");
    return text + suffix;
}

inline bool isUnitDependentTextColumn(const QString &title)
{
    // Category-flow text is recomputed from canonical quantities on demand.
    // Text filters for this column must be reevaluated when units change.
    return title == QStringLiteral("Demand Details");
}

inline QString formatReactionCoefficient(const UnitPresentationContext &context,
                                         ReactionKind kind, double order,
                                         double canonical, int decimals = 8)
{
    const std::optional<double> displayed = reactionValue(context, kind, order, canonical);
    if (!displayed) return QStringLiteral("—");
    const QString suffix = QStringLiteral(" ") + reactionUnit(context, kind, order);
    const double magnitude = std::abs(*displayed);
    if (magnitude > 0.0 && magnitude < std::pow(10.0, -decimals))
    {
        const int needed = std::max(decimals,
            static_cast<int>(std::ceil(-std::log10(magnitude))) + 3);
        if (needed > 18)
            return QString::number(*displayed, 'g', 12) + suffix;
        return formatNumber(*displayed, needed, suffix);
    }
    return formatNumber(*displayed, decimals, suffix);
}

inline QString formatDemandRate(const UnitPresentationContext &context, double canonical,
                                 int decimals = 3,
                                 std::string_view quantity = "base_demand_flow_rate")
{
    const std::optional<double> displayed = toDisplay(context, quantity, canonical);
    if (!displayed)
        return QStringLiteral("—");
    return formatNumber(*displayed, decimals,
                        QStringLiteral(" ") + unitSymbol(selectedUnit(context, quantity)));
}

inline QString formatEnergyPrice(const UnitPresentationContext &context, double canonical,
                                  int minimum_decimals = 6)
{
    const std::optional<double> displayed = toDisplayForTitle(
        context, QStringLiteral("Energy Price [/kWh]"), canonical);
    if (!displayed)
        return QStringLiteral("—");
    int decimals = minimum_decimals;
    if (*displayed > 0.0 && *displayed < 1.0)
        decimals = std::max(decimals, static_cast<int>(std::ceil(-std::log10(*displayed))) + 3);
    if (decimals > 18)
        return QString::number(*displayed, 'g', 12);
    return formatNumber(*displayed, decimals);
}

inline QString formattedTitle(const QString &title, const UnitPresentationContext &context)
{
    if (isEnergyPriceTitle(title))
        return title.left(title.size() - 5)
            + QStringLiteral("/") + unitSymbol(selectedUnit(context, "energy"))
            + QStringLiteral("]");
    const std::string_view quantity = quantityForTitle(title);
    if (quantity.empty()) return title;
    const int start = title.lastIndexOf(QStringLiteral(" ["));
    if (start < 0) return title;
    return title.left(start) + QStringLiteral(" [")
        + unitSymbol(selectedUnit(context, quantity)) + QStringLiteral("]");
}
} // namespace UnitTablePresentation
