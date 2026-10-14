#pragma once

#include <aowis/model/units/quantity.h>

#include "config/unit_display_text.h"
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
// Metadata names the Model quantity; headings never select conversion behavior.
// Currency is an opaque label. Energy prices retain their stored per-kWh basis.
enum class ColumnKind { Value, EnergyPrice, DemandDetails, BulkReaction, WallReaction, RowQuantity };

struct ColumnUnits
{
    std::string_view quantity;
    ColumnKind kind = ColumnKind::Value;
    QString currency;
};

inline QString unitSymbol(QString identifier)
{
    return UnitDisplayText::symbol(identifier);
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
    return aowis::units::convert(quantity, canonical, canonical_unit, target,
                                 context.specificGravity());
}

inline std::optional<double> toCanonical(const UnitPresentationContext &context,
                                         std::string_view quantity, double displayed)
{
    if (quantity.empty()) return displayed;
    const std::string_view canonical_unit = aowis::units::canonicalUnit(quantity);
    const std::string source = selectedUnit(context, quantity).toStdString();
    return aowis::units::convert(quantity, displayed, source, canonical_unit,
                                 context.specificGravity());
}

inline std::optional<double> toDisplayForColumn(const UnitPresentationContext &context,
                                                const ColumnUnits &column, double canonical)
{
    if (column.kind == ColumnKind::EnergyPrice)
        return UnitEnergyPriceConversion::fromStored(
            canonical, selectedUnit(context, AOWIS_QUANTITY("energy")).toStdString());
    return toDisplay(context, column.quantity, canonical);
}

inline std::optional<double> toCanonicalForColumn(const UnitPresentationContext &context,
                                                  const ColumnUnits &column, double displayed)
{
    if (column.kind == ColumnKind::EnergyPrice)
        return UnitEnergyPriceConversion::toStored(
            displayed, selectedUnit(context, AOWIS_QUANTITY("energy")).toStdString());
    return toCanonical(context, column.quantity, displayed);
}

// Reaction coefficients cannot be assigned a fixed table-column unit:
// different rows may have different orders and therefore dimensions.
enum class ReactionKind { Bulk, Wall };

inline std::string_view reactionQuantity(ReactionKind kind, double order)
{
    if (!std::isfinite(order)) return {};
    if (kind == ReactionKind::Bulk) return AOWIS_QUANTITY("first_order_bulk_reaction_coefficient");
    if (std::abs(order - 1.0) <= 1e-12) return AOWIS_QUANTITY("first_order_wall_reaction_coefficient");
    if (std::abs(order) <= 1e-12) return AOWIS_QUANTITY("zero_order_wall_reaction_coefficient");
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
            + unitSymbol(selectedUnit(context, AOWIS_QUANTITY("first_order_bulk_reaction_coefficient")))
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

// A table cell's existing decimal count remains the default. An explicit
// per-quantity policy, or a changed global notation/precision, takes priority.
// Locale and grouping are always supplied by the presentation context.
inline bool usesColumnPrecision(const UnitPresentationContext &context,
                                std::string_view quantity = {})
{
    const QString key = QString::fromUtf8(quantity.data(), static_cast<qsizetype>(quantity.size()));
    const NumericFormatPolicy policy = context.formatPolicy(key);
    const NumericFormatPolicy defaults;
    return !context.hasFormatPolicyOverride(key)
        && policy.notation == defaults.notation && policy.precision == defaults.precision;
}

inline NumericFormatPolicy tableFormatPolicy(const UnitPresentationContext &context,
                                            int decimals, std::string_view quantity = {})
{
    NumericFormatPolicy policy = context.formatPolicy(
        QString::fromUtf8(quantity.data(), static_cast<qsizetype>(quantity.size())));
    if (usesColumnPrecision(context, quantity))
    {
        policy.notation = NumericFormatPolicy::Notation::Fixed;
        policy.precision = std::clamp(decimals, 0, 18);
    }
    return policy;
}

// Read-only formatting, not an editing or canonical serialization path.
// All numeric table formatting goes through NumericText, including the legacy
// canonical fallback used when constructing table rows.
inline QString formatWithPolicy(double value, const QString &suffix,
                                const QLocale &locale, const NumericFormatPolicy &policy,
                                bool compact_fixed)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    const std::optional<QString> number = NumericText::format(value, locale, policy);
    if (!number) return QStringLiteral("—");
    QString text = *number;
    const QLocale display_locale = NumericText::localeFor(locale, policy.grouping);
    // Only small fixed-decimal values could round to zero. Avoid reparsing
    // every ordinary table cell, especially when constructing large tables.
    bool rounded_to_zero = false;
    if (value != 0.0 && policy.notation == NumericFormatPolicy::Notation::Fixed
        && std::abs(value) <= std::pow(10.0, -policy.precision))
    {
        const NumericText::ParseResult represented = NumericText::parse(
            text, display_locale, policy.grouping);
        rounded_to_zero = represented.value && *represented.value == 0.0;
    }
    // A displayed zero must never conceal a finite nonzero measurement.
    if (rounded_to_zero)
    {
        NumericFormatPolicy fallback = policy;
        fallback.notation = NumericFormatPolicy::Notation::Significant;
        fallback.precision = 12;
        text = NumericText::format(value, locale, fallback).value_or(QStringLiteral("—"));
    }
    else if (compact_fixed && policy.notation == NumericFormatPolicy::Notation::Fixed)
    {
        // Keep compact fixed-decimal table presentation with locale-aware
        // trailing-zero trimming (the separator may not be a period).
        const QString separator = QString(display_locale.decimalPoint());
        if (text.contains(separator))
        {
            while (text.endsWith(QLatin1Char('0'))) text.chop(1);
            if (text.endsWith(separator)) text.chop(separator.size());
        }
    }
    return text + UnitDisplayText::symbol(suffix);
}

inline QString formatNumber(const UnitPresentationContext &context,
                            double value, int decimals, const QString &suffix = QString(),
                            std::string_view quantity = {})
{
    return formatWithPolicy(value, suffix, context.locale(),
                            tableFormatPolicy(context, decimals, quantity),
                            usesColumnPrecision(context, quantity));
}

// Compatibility for precomputed canonical fallback strings. The live table
// DisplayRole uses the overload above with the current presentation context.
inline QString formatNumber(double value, int decimals, const QString &suffix = QString())
{
    const NumericFormatPolicy policy{NumericFormatPolicy::Notation::Fixed,
                                     std::clamp(decimals, 0, 18), false, true};
    return formatWithPolicy(value, suffix, QLocale::c(), policy, true);
}

// Integer counts retain integral notation regardless of an engineering
// quantity's significant-digit policy; locale/grouping still apply.
inline QString formatCount(const UnitPresentationContext &context, int count)
{
    const NumericFormatPolicy policy{NumericFormatPolicy::Notation::Fixed, 0,
                                     context.formatPolicy().grouping, true};
    return NumericText::format(static_cast<double>(count), context.locale(), policy)
        .value_or(QStringLiteral("—"));
}

inline bool isUnitDependentTextColumn(const ColumnUnits &column)
{
    return column.kind == ColumnKind::DemandDetails;
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
        return formatNumber(context, *displayed, std::min(needed, 18), suffix,
                            reactionQuantity(kind, order));
    }
    return formatNumber(context, *displayed, decimals, suffix,
                        reactionQuantity(kind, order));
}

inline QString formatDemandRate(const UnitPresentationContext &context, double canonical,
                                 int decimals = 3,
                                 std::string_view quantity = AOWIS_QUANTITY("base_demand_flow_rate"))
{
    const std::optional<double> displayed = toDisplay(context, quantity, canonical);
    if (!displayed)
        return QStringLiteral("—");
    return formatNumber(context, *displayed, decimals,
                        QStringLiteral(" ") + unitSymbol(selectedUnit(context, quantity)), quantity);
}

inline QString formatEnergyPrice(const UnitPresentationContext &context, double canonical,
                                  int minimum_decimals = 6)
{
    const std::optional<double> displayed = toDisplayForColumn(
        context, {{}, ColumnKind::EnergyPrice}, canonical);
    if (!displayed)
        return QStringLiteral("—");
    int decimals = minimum_decimals;
    if (*displayed > 0.0 && *displayed < 1.0)
        decimals = std::max(decimals, static_cast<int>(std::ceil(-std::log10(*displayed))) + 3);
    return formatNumber(context, *displayed, std::min(decimals, 18), QString(),
                        AOWIS_QUANTITY("energy"));
}

inline QString formattedTitle(const QString &title, const UnitPresentationContext &context,
                              const ColumnUnits &column)
{
    if (column.quantity.empty() && column.kind != ColumnKind::EnergyPrice)
        return title;
    // Remove an existing presentation suffix only; its contents have no semantics.
    const int start = title.lastIndexOf(QStringLiteral(" ["));
    const QString base = start >= 0 && title.endsWith(QLatin1Char(']'))
        ? title.left(start) : title;
    const QString unit = column.kind == ColumnKind::EnergyPrice
        ? column.currency + QStringLiteral("/") + unitSymbol(selectedUnit(context, AOWIS_QUANTITY("energy")))
        : unitSymbol(selectedUnit(context, column.quantity));
    return base + QStringLiteral(" [") + unit + QStringLiteral("]");
}
} // namespace UnitTablePresentation
