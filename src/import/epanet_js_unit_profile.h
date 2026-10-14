#pragma once

#include <aowis/model/units/unit_profile.h>
#include "config/unit_presented_quantities.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <initializer_list>

// EPANET-JS spellings and quantity mapping belong at the import boundary.
// The Model registry remains authoritative for permitted presentation units.
namespace EpanetJsUnitProfile {
// Normalize only spellings that the numerical epanet-js importer accepts.
// In particular, Ml/d (megalitres/day) is NOT ml/d (millilitres/day).
// Do not lowercase metric prefixes before resolving the source unit.
inline QString normalizeUnit(const QString &source) {
    const QString unit = source.trimmed();
    const QString lower = unit.toLower();
    if (unit == QStringLiteral("Ml/d") || unit == QStringLiteral("ML/d")
        || lower == QStringLiteral("mld")) return QStringLiteral("ML/d");
    if (unit == QStringLiteral("Mgal/d") || unit == QStringLiteral("MGal/d")
        || lower == QStringLiteral("mgd")) return QStringLiteral("MUSgal/d");
    if (lower == QStringLiteral("imgal/d") || lower == QStringLiteral("imgd"))
        return QStringLiteral("MImpgal/d");
    if (lower == QStringLiteral("gal/min") || lower == QStringLiteral("usgal/min")
        || lower == QStringLiteral("gpm")) return QStringLiteral("US gal/min");
    if (lower == QStringLiteral("ft^3/s") || lower == QStringLiteral("ft3/s")
        || lower == QStringLiteral("cfs")) return QStringLiteral("ft3/s");
    if (lower == QStringLiteral("acft/d") || lower == QStringLiteral("acre-ft/d")
        || lower == QStringLiteral("afd")) return QStringLiteral("acre.ft/d");
    if (lower == QStringLiteral("l/s") || lower == QStringLiteral("lps")) return QStringLiteral("L/s");
    if (lower == QStringLiteral("l/min") || lower == QStringLiteral("lpm")) return QStringLiteral("L/min");
    if (lower == QStringLiteral("m^3/h") || lower == QStringLiteral("m3/h")
        || lower == QStringLiteral("cmh")) return QStringLiteral("m3/h");
    if (lower == QStringLiteral("m^3/d") || lower == QStringLiteral("m3/d")
        || lower == QStringLiteral("cmd")) return QStringLiteral("m3/d");
    if (lower == QStringLiteral("m^3/s") || lower == QStringLiteral("m3/s")
        || lower == QStringLiteral("cms")) return QStringLiteral("m3/s");
    if (lower == QStringLiteral("m^3") || lower == QStringLiteral("m3")) return QStringLiteral("m3");
    if (lower == QStringLiteral("ft^3") || lower == QStringLiteral("ft3")) return QStringLiteral("ft3");
    if (lower == QStringLiteral("mwc") || lower == QStringLiteral("mh2o"))
        return QStringLiteral("m");
    if (lower == QStringLiteral("fwc") || lower == QStringLiteral("ftwc")) return QStringLiteral("ft");
    if (lower == QStringLiteral("meter") || lower == QStringLiteral("metre")) return QStringLiteral("m");
    if (lower == QStringLiteral("foot") || lower == QStringLiteral("feet")) return QStringLiteral("ft");
    QString compact = lower;
    compact.remove(QLatin1Char(' '));
    if (compact == QStringLiteral("millift") || compact == QStringLiteral("millifeet")
        || compact == QStringLiteral("milli-ft") || compact == QStringLiteral("0.001ft"))
        return QStringLiteral("millift");
    if (lower == QStringLiteral("millimeter") || lower == QStringLiteral("millimetre"))
        return QStringLiteral("mm");
    if (lower == QStringLiteral("inch") || lower == QStringLiteral("inches")) return QStringLiteral("in");
    if (lower == QStringLiteral("usgal") || lower == QStringLiteral("gal")) return QStringLiteral("US gal");
    if (lower == QStringLiteral("kw")) return QStringLiteral("kW");
    if (lower == QStringLiteral("kpa")) return QStringLiteral("kPa");
    if (lower == QStringLiteral("μg/l") || lower == QStringLiteral("µg/l")
        || lower == QStringLiteral("ug/l")) return QStringLiteral("ug/L");
    if (lower == QStringLiteral("mg/l")) return QStringLiteral("mg/L");
    if (lower == QStringLiteral("g/m^3") || lower == QStringLiteral("g/m3"))
        return QStringLiteral("g/m3");
    if (lower == QStringLiteral("h") || lower == QStringLiteral("hr")
        || lower == QStringLiteral("hrs") || lower == QStringLiteral("hour")
        || lower == QStringLiteral("hours")) return QStringLiteral("h");
    if (lower == QStringLiteral("minute") || lower == QStringLiteral("minutes"))
        return QStringLiteral("min");
    if (lower == QStringLiteral("s") || lower == QStringLiteral("sec")
        || lower == QStringLiteral("second") || lower == QStringLiteral("seconds"))
        return QStringLiteral("s");
    if (lower == QStringLiteral("day") || lower == QStringLiteral("days"))
        return QStringLiteral("d");
    return unit;
}
inline void assignSelection(
    const QJsonObject &source,
    QJsonObject &result,
    QStringList *unmapped,
    const char *source_key,
    std::initializer_list<const char *> destinations)
{
    const QString key = QString::fromLatin1(source_key);
    const QString raw = source.value(key).toString().trimmed();
    if (raw.isEmpty())
        return;
    const QString unit = normalizeUnit(raw);
    bool assigned = false;
    for (const char *destination : destinations)
    {
        if (!aowis::units::isAllowedUnit(destination, unit.toStdString()))
            continue;
        const QString quantity = QString::fromLatin1(destination);
        if (!result.contains(quantity))
            result.insert(quantity, unit);
        assigned = true;
    }
    if (!assigned && unmapped)
        unmapped->append(key + QStringLiteral(" = ") + raw);
}
inline QJsonObject selections(const QJsonObject &source, QStringList *unmapped = nullptr) {
    QJsonObject result;
    assignSelection(source, result, unmapped, "flow", {"volumetric_flow_rate"});
    assignSelection(source, result, unmapped, "baseDemand", {"base_demand_flow_rate"});
    assignSelection(source, result, unmapped, "customerDemand", {"customer_demand_flow_rate"});
    assignSelection(source, result, unmapped, "length", {"length", "distance", "vertical_offset"});
    assignSelection(source, result, unmapped, "level", {"water_level"});
    assignSelection(source, result, unmapped, "initialLevel", {"tank_initial_level"});
    assignSelection(source, result, unmapped, "minLevel", {"tank_minimum_level"});
    assignSelection(source, result, unmapped, "maxLevel", {"tank_maximum_level"});
    assignSelection(source, result, unmapped, "elevation", {"elevation", "altitude"});
    assignSelection(source, result, unmapped, "head", {"hydraulic_head", "head_gain"});
    assignSelection(source, result, unmapped, "headloss", {"head_loss"});
    assignSelection(source, result, unmapped, "diameter", {"link_diameter"});
    // Roughness has length units only for Darcy-Weisbach. The imported
    // coefficient for Hazen-Williams or Chezy-Manning remains dimensionless;
    // do not interpret a dimensionless "roughness" source preference as length.
    const QString source_roughness = normalizeUnit(
        source.value(QStringLiteral("roughness")).toString());
    if (!source_roughness.isEmpty() && source_roughness != QStringLiteral("1"))
        assignSelection(source, result, unmapped, "roughness",
                        {"darcy_weisbach_roughness_height"});
    assignSelection(source, result, unmapped, "tankDiameter", {"tank_diameter"});
    assignSelection(source, result, unmapped, "velocity", {"velocity"});
    // Pressure-valued EPANET inputs are normalized to metres of hydraulic
    // head by the importer; choose their original unit for GUI presentation.
    assignSelection(source, result, unmapped, "pressure", {"pressure_head"});
    assignSelection(source, result, unmapped, "volume", {"volume"});
    assignSelection(source, result, unmapped, "minVolume", {"tank_minimum_volume"});
    assignSelection(source, result, unmapped, "power", {"power"});
    assignSelection(source, result, unmapped, "waterAge", {"water_age"});
    assignSelection(source, result, unmapped, "chemicalConcentration", {"chemical_mass_concentration"});
    return result;
}

// Match only source preferences that the GUI can actually present. An EPANET
// flow template wins over the canonical profile when both resolve identically
// (notably CMH). Hidden fields (including legacy minimum tank volume and
// other unused/legacy metadata) do not justify another GUI presentation profile.
inline QString matchingBuiltinId(const QJsonObject &selections) {
    const QString flow = selections.value(QStringLiteral("volumetric_flow_rate")).toString();
    if (flow.isEmpty()) return {};
    for (std::size_t index = 0; index < aowis::units::flow_template_names.size(); ++index) {
        if (flow.toStdString() != aowis::units::flow_template_units[index]) continue;
        const aowis::units::UnitProfile profile = aowis::units::flowTemplateProfile(index);
        bool matches = true;
        for (QJsonObject::const_iterator item = selections.constBegin();
             item != selections.constEnd(); ++item) {
            const aowis::units::UnitField *field =
                aowis::units::findUnitField(item.key().toStdString());
            if (field == nullptr || !UnitPresentedQuantities::isPresented(*field)) continue;
            if (aowis::units::resolvedUnit(profile, item.key().toStdString())
                != item.value().toString().toStdString()) {
                matches = false;
                break;
            }
        }
        if (matches) return QString::fromStdString(profile.id);
    }
    // No other built-in flow template can be equivalent to the source flow.
    return {};
}
} // namespace EpanetJsUnitProfile
