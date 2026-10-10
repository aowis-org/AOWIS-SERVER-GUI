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
        names.insert(QString::fromStdString(profile.name).toCaseFolded());
        ids.insert(QString::fromStdString(profile.id));
    }
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty() || names.contains(name.toCaseFolded())) continue;
        QString id = object.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty() || ids.contains(id)) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        aowis::units::UnitProfile profile;
        profile.id = id.toStdString();
        profile.name = name.toStdString();
        const QJsonObject units = object.value(QStringLiteral("units")).toObject();
        for (const aowis::units::UnitField &field : aowis::units::unit_fields) {
            const QString key = QString::fromLatin1(field.key.data(), static_cast<qsizetype>(field.key.size()));
            const QString unit = units.value(key).toString();
            if (aowis::units::isAllowedUnit(field.key, unit.toStdString()) &&
                unit.toStdString() != aowis::units::canonicalUnit(field.key))
                profile.overrides.emplace(key.toStdString(), unit.toStdString());
        }
        names.insert(name.toCaseFolded());
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
        // Preserve the legacy full-unit JSON representation for backward compatibility.
        for (const aowis::units::UnitField &field : aowis::units::unit_fields) {
            const std::string_view unit = aowis::units::resolvedUnit(profile, field.key);
            units.insert(QString::fromLatin1(field.key.data(), static_cast<qsizetype>(field.key.size())),
                         QString::fromLatin1(unit.data(), static_cast<qsizetype>(unit.size())));
        }
        custom.append(QJsonObject{{QStringLiteral("id"), QString::fromStdString(profile.id)},
                                  {QStringLiteral("name"), QString::fromStdString(profile.name)},
                                  {QStringLiteral("units"), units}});
    }
    QSettings settings(this->path_, QSettings::IniFormat);
    settings.setValue(QStringLiteral("units/custom_profiles"), QJsonDocument(custom).toJson(QJsonDocument::Compact));
    settings.setValue(QStringLiteral("units/selected_profile_id"), snapshot.active_id);
    settings.setValue(QStringLiteral("units/selected_profile"), snapshot.legacy_active_name);
    settings.sync();
    return settings.status() == QSettings::NoError;
}
