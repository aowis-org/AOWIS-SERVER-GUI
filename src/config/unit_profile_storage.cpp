#include <aowis/model/units/quantity.h>
#include "config/unit_profile_storage.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>
#include <QSet>

UnitProfileSnapshot QSettingsUnitProfileStorage::load() const
{
    QSettings settings(this->path_, QSettings::IniFormat);
    UnitProfileSnapshot result;
    result.active_id = settings.value(QStringLiteral("units/selected_profile_id")).toString();
    result.legacy_active_name = settings.value(QStringLiteral("units/selected_profile"), QStringLiteral("AOWIS Canonical")).toString();
    const QJsonDocument document = QJsonDocument::fromJson(settings.value(QStringLiteral("units/custom_profiles")).toByteArray());
    if (!document.isArray()) return result;
    QSet<QString> names;
    QSet<QString> ids;
    for (const aowis::units::UnitProfile &profile : aowis::units::predefinedProfiles()) {
        names.insert(QString::fromStdString(profile.name));
        ids.insert(QString::fromStdString(profile.id));
    }
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty() || names.contains(name)) continue;
        QString id = object.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty() || ids.contains(id)) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        aowis::units::UnitProfile profile;
        profile.id = id.toStdString();
        profile.name = name.toStdString();
        const QJsonObject units = object.value(QStringLiteral("units")).toObject();
        // Only explicit canonical pressure-head selections are marked: an old
        // full-unit INI snapshot may contain a default 'm' without an override.
        const bool explicit_pressure_head =
            object.value(QStringLiteral("explicit_pressure_head")).toBool();
        for (const aowis::units::UnitField &field : aowis::units::unit_fields) {
            const QString key = QString::fromLatin1(field.key.data(), static_cast<qsizetype>(field.key.size()));
            const QString unit = units.value(key).toString();
            if (aowis::units::isAllowedUnit(field.key, unit.toStdString()) &&
                (unit.toStdString() != aowis::units::canonicalUnit(field.key) ||
                 !aowis::units::inheritedUnitQuantity(field.key).empty() ||
                 (field.key == AOWIS_QUANTITY("pressure_head") && explicit_pressure_head)))
                profile.overrides.emplace(key.toStdString(), unit.toStdString());
        }
        names.insert(name);
        ids.insert(id);
        result.custom_profiles.push_back(std::move(profile));
    }
    return result;
}

bool QSettingsUnitProfileStorage::save(const UnitProfileSnapshot &snapshot) const
{
    QJsonArray custom;
    for (const aowis::units::UnitProfile &profile : snapshot.custom_profiles) {
        if (profile.builtin) continue;
        QJsonObject units;
        const bool explicit_pressure_head =
            profile.overrides.find(AOWIS_QUANTITY("pressure_head")) != profile.overrides.end();
        // Preserve the legacy full-unit JSON representation for backward compatibility.
        for (const aowis::units::UnitField &field : aowis::units::unit_fields) {
            // Omit inherited choices until explicitly selected, so older profiles
            // continue following their general flow/level/volume preferences.
            if (!aowis::units::inheritedUnitQuantity(field.key).empty()
                && profile.overrides.find(field.key) == profile.overrides.end())
                continue;
            // The legacy physical-pressure preference is a fallback, not an
            // explicit pressure-head override. Do not freeze it during saving.
            if (field.key == AOWIS_QUANTITY("pressure_head") && !explicit_pressure_head
                && profile.overrides.find(AOWIS_QUANTITY("pressure")) != profile.overrides.end())
                continue;
            // Keep historical minimum-volume selections in storage for a
            // lossless legacy round trip; presentation always follows volume.
            const aowis::units::UnitSelections::const_iterator explicit_choice =
                profile.overrides.find(field.key);
            const std::string_view unit = field.key == AOWIS_QUANTITY("tank_minimum_volume")
                && explicit_choice != profile.overrides.end()
                ? std::string_view(explicit_choice->second)
                : aowis::units::resolvedUnit(profile, field.key);
            units.insert(QString::fromLatin1(field.key.data(), static_cast<qsizetype>(field.key.size())),
                         QString::fromLatin1(unit.data(), static_cast<qsizetype>(unit.size())));
        }
        QJsonObject stored_profile{{QStringLiteral("id"), QString::fromStdString(profile.id)},
                                   {QStringLiteral("name"), QString::fromStdString(profile.name)},
                                   {QStringLiteral("units"), units}};
        // A deliberately selected 'm' must survive even when an old physical-
        // pressure override exists. Do not reinterpret unmarked legacy files.
        if (explicit_pressure_head)
            stored_profile.insert(QStringLiteral("explicit_pressure_head"), true);
        custom.append(stored_profile);
    }
    QSettings settings(this->path_, QSettings::IniFormat);
    settings.setValue(QStringLiteral("units/custom_profiles"), QJsonDocument(custom).toJson(QJsonDocument::Compact));
    settings.setValue(QStringLiteral("units/selected_profile_id"), snapshot.active_id);
    settings.setValue(QStringLiteral("units/selected_profile"), snapshot.legacy_active_name);
    settings.sync();
    return settings.status() == QSettings::NoError;
}
