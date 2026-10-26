#include "config/project_server_profile_storage.h"

#include "config/gui_configuration.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#include <cstdlib>

EM_JS(char *, aowisLoadProjectServers, (),
{
    try
    {
        const value = globalThis.localStorage
            ? globalThis.localStorage.getItem("aowis.projects.servers") : null;
        return stringToNewUTF8(value === null ? "[]" : value);
    }
    catch (error)
    {
        return stringToNewUTF8("[]");
    }
});

EM_JS(int, aowisSaveProjectServers, (const char *json),
{
    try
    {
        if (!globalThis.localStorage)
            return 0;
        globalThis.localStorage.setItem("aowis.projects.servers", UTF8ToString(json));
        return 1;
    }
    catch (error)
    {
        return 0;
    }
});
#else
#include <QSettings>
#endif

namespace
{
const QString servers_key = QStringLiteral("projects/servers");

QJsonObject serializeProfile(const ProjectServerProfile &profile)
{
    QJsonArray tags;
    for (const QString &tag : profile.tags)
        tags.append(tag);

    return QJsonObject{
        {QStringLiteral("id"), profile.id},
        {QStringLiteral("name"), profile.name},
        {QStringLiteral("endpoint"), profile.endpoint},
        {QStringLiteral("description"), profile.description},
        {QStringLiteral("tags"), tags},
        {QStringLiteral("auth_mode"), profile.auth_mode}
    };
}
}

QList<ProjectServerProfile> loadProjectServerProfiles()
{
    QByteArray json;
#ifdef __EMSCRIPTEN__
    char *stored = aowisLoadProjectServers();
    if (stored != nullptr)
    {
        json = QByteArray(stored);
        std::free(stored);
    }
#else
    // Also creates the per-user configuration on first launch, before the
    // first Projects widget tries to read or write its server list.
    (void)guiConfiguration();
    QSettings settings(guiConfigurationFilePath(), QSettings::IniFormat);
    json = settings.value(servers_key, QStringLiteral("[]")).toString().toUtf8();
    if (settings.status() != QSettings::NoError)
        qWarning() << "Unable to load project server profiles from" << settings.fileName();
#endif

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
    {
        qWarning() << "Invalid project server profiles configuration:"
                   << (error.error == QJsonParseError::NoError
                        ? QStringLiteral("expected a JSON array") : error.errorString());
        return {};
    }

    QList<ProjectServerProfile> profiles;
    QSet<QString> seen_ids;
    QSet<QString> seen_names;
    for (const QJsonValue &value : document.array())
    {
        if (!value.isObject()) continue;
        const QJsonObject record = value.toObject();
        ProjectServerProfile profile;
        profile.id = record.value(QStringLiteral("id")).toString().trimmed();
        profile.name = record.value(QStringLiteral("name")).toString().trimmed();
        if (profile.id.isEmpty() || profile.name.isEmpty()
            || seen_ids.contains(profile.id)
            || seen_names.contains(profile.name.toCaseFolded()))
            continue;
        profile.endpoint = record.value(QStringLiteral("endpoint")).toString().trimmed();
        profile.description = record.value(QStringLiteral("description")).toString();
        for (const QJsonValue &tag : record.value(QStringLiteral("tags")).toArray())
            if (tag.isString()) profile.tags.append(tag.toString());
        profile.auth_mode = record.value(QStringLiteral("auth_mode")).toString();
        if (profile.auth_mode != QStringLiteral("API token")
            && profile.auth_mode != QStringLiteral("User login"))
            profile.auth_mode = QStringLiteral("None");
        seen_ids.insert(profile.id);
        seen_names.insert(profile.name.toCaseFolded());
        profiles.append(profile);
    }
    return profiles;
}

bool saveProjectServerProfiles(const QList<ProjectServerProfile> &profiles)
{
    QJsonArray records;
    for (const ProjectServerProfile &profile : profiles)
        records.append(serializeProfile(profile));
    const QByteArray json = QJsonDocument(records).toJson(QJsonDocument::Compact);

#ifdef __EMSCRIPTEN__
    // Browsers cannot modify the webroot INI file. Store per-browser profiles
    // in localStorage instead; the native build writes the shared GUI INI.
    const bool saved = aowisSaveProjectServers(json.constData()) != 0;
#else
    (void)guiConfiguration();
    QSettings settings(guiConfigurationFilePath(), QSettings::IniFormat);
    settings.setValue(servers_key, QString::fromUtf8(json));
    settings.sync();
    const bool saved = settings.status() == QSettings::NoError;
#endif
    if (!saved)
        qWarning() << "Unable to save project server profiles to GUI configuration";
    return saved;
}