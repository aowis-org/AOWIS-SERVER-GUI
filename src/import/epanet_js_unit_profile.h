#pragma once

#include <aowis/model/units/unit_profile.h>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// EPANET-JS spellings and quantity mapping belong at the import boundary.
// The Model registry remains authoritative for permitted presentation units.
namespace EpanetJsUnitProfile {
inline QString normalizeUnit(const QString &source) {
    const QString unit = source.trimmed();
    const QString lower = unit.toLower();
    if (lower == QStringLiteral("gal/min") || lower == QStringLiteral("gpm")) return QStringLiteral("US gal/min");
    if (lower == QStringLiteral("ft^3/s") || lower == QStringLiteral("cfs")) return QStringLiteral("ft3/s");
    if (lower == QStringLiteral("m^3") || lower == QStringLiteral("m3")) return QStringLiteral("m3");
    if (lower == QStringLiteral("ft^3")) return QStringLiteral("ft3");
    if (lower == QStringLiteral("mwc") || lower == QStringLiteral("ftwc")) return lower == QStringLiteral("mwc") ? QStringLiteral("m") : QStringLiteral("ft");
    if (lower == QStringLiteral("mgd")) return QStringLiteral("MUSgal/d");
    if (lower == QStringLiteral("imgd")) return QStringLiteral("MImpgal/d");
    if (lower == QStringLiteral("lps")) return QStringLiteral("L/s");
    if (lower == QStringLiteral("lpm")) return QStringLiteral("L/min");
    if (lower == QStringLiteral("ml/d") || lower == QStringLiteral("mld")) return QStringLiteral("ML/d");
    if (lower == QStringLiteral("m^3/h")) return QStringLiteral("m3/h");
    if (lower == QStringLiteral("m^3/d")) return QStringLiteral("m3/d");
    if (lower == QStringLiteral("m^3/s")) return QStringLiteral("m3/s");
    if (lower == QStringLiteral("kw")) return QStringLiteral("kW");
    if (lower == QStringLiteral("μg/l") || lower == QStringLiteral("µg/l") || lower == QStringLiteral("ug/l")) return QStringLiteral("ug/L");
    if (lower == QStringLiteral("mg/l")) return QStringLiteral("mg/L");
    if (lower == QStringLiteral("g/m^3") || lower == QStringLiteral("g/m3")) return QStringLiteral("g/m3");
    if (lower == QStringLiteral("fwc")) return QStringLiteral("ft");
    if (lower == QStringLiteral("acft/d")) return QStringLiteral("acre.ft/d");
    if (lower == QStringLiteral("gal")) return QStringLiteral("US gal");
    if (lower == QStringLiteral("min")) return QStringLiteral("min");
    if (lower == QStringLiteral("h") || lower == QStringLiteral("hr")) return QStringLiteral("h");
    if (lower == QStringLiteral("ft")) return QStringLiteral("ft");
    if (lower == QStringLiteral("hp")) return QStringLiteral("hp");
    return unit;
}
inline QJsonObject selections(const QJsonObject &source, QStringList *unmapped = nullptr) {
    QJsonObject result;
    const auto assign = [&source, &result, unmapped](const char *sourceKey, std::initializer_list<const char *> destinations) {
        const QString key = QString::fromLatin1(sourceKey);
        const QString raw = source.value(key).toString().trimmed();
        if (raw.isEmpty()) return;
        const QString unit = normalizeUnit(raw);
        bool assigned = false;
        for (const char *destination : destinations) {
            if (!aowis::units::isAllowedUnit(destination, unit.toStdString())) continue;
            const QString quantity = QString::fromLatin1(destination);
            if (!result.contains(quantity)) result.insert(quantity, unit);
            assigned = true;
        }
        if (!assigned && unmapped) unmapped->append(key + QStringLiteral(" = ") + raw);
    };
    assign("flow", {"volumetric_flow_rate"});
    assign("baseDemand", {"volumetric_flow_rate"});
    assign("customerDemand", {"volumetric_flow_rate"});
    assign("length", {"length", "distance", "vertical_offset"});
    assign("level", {"water_level"});
    assign("initialLevel", {"water_level"});
    assign("minLevel", {"water_level"});
    assign("maxLevel", {"water_level"});
    assign("elevation", {"elevation", "altitude"});
    assign("head", {"hydraulic_head", "head_gain"});
    assign("headloss", {"head_loss"});
    assign("diameter", {"link_diameter"});
    assign("tankDiameter", {"tank_diameter"});
    assign("velocity", {"velocity"});
    if (normalizeUnit(source.value(QStringLiteral("pressure")).toString()) == QStringLiteral("psi"))
        assign("pressure", {"pressure"});
    else
        assign("pressure", {"pressure_head"});
    assign("volume", {"volume"});
    assign("minVolume", {"volume"});
    assign("power", {"power"});
    assign("waterAge", {"water_age"});
    assign("chemicalConcentration", {"chemical_mass_concentration"});
    return result;
}
} // namespace EpanetJsUnitProfile
