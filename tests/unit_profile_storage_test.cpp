#include "config/unit_profile_storage.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cassert>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("unit-profiles.ini"));
    QSettingsUnitProfileStorage storage(path);
    UnitProfileSnapshot first;
    first.active_id = QStringLiteral("profile-one");
    first.legacy_active_name = QStringLiteral("My units");
    first.custom_profiles.push_back({"profile-one", "My units", {{"pressure", "psi"}, {"link_diameter", "in"}}, false});
    assert(storage.save(first));
    const UnitProfileSnapshot restored = storage.load();
    assert(restored.active_id == first.active_id);
    assert(restored.legacy_active_name == first.legacy_active_name);
    assert(restored.custom_profiles.size() == 1);
    assert(restored.custom_profiles[0].id == "profile-one");
    assert(restored.custom_profiles[0].overrides == first.custom_profiles[0].overrides);
    // Legacy JSON profiles have no IDs. The adapter assigns one during import.
    {
        QSettings settings(path, QSettings::IniFormat);
        QJsonArray array;
        array.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("Legacy")},
                                 {QStringLiteral("units"), QJsonObject{{QStringLiteral("pressure"), QStringLiteral("bar")}}}});
        settings.setValue(QStringLiteral("units/custom_profiles"), QJsonDocument(array).toJson(QJsonDocument::Compact));
        settings.remove(QStringLiteral("units/selected_profile_id"));
        settings.setValue(QStringLiteral("units/selected_profile"), QStringLiteral("Legacy"));
        settings.sync();
    }
    const UnitProfileSnapshot legacy = storage.load();
    assert(legacy.custom_profiles.size() == 1);
    assert(!legacy.custom_profiles[0].id.empty());
    assert(legacy.custom_profiles[0].overrides.at("pressure") == "bar");
    assert(legacy.legacy_active_name == QStringLiteral("Legacy"));
    assert(legacy.active_id.isEmpty());
    return 0;
}
