#include "config/personal_repository_storage.h"

#include "config/gui_configuration.h"
#include "projects/dvmdb_identity.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#include <cstdlib>

EM_JS(char *, aowisLoadPersonalRepository, (),
{
    try
    {
        const value = globalThis.localStorage
            ? globalThis.localStorage.getItem("aowis.projects.personalRepository") : null;
        return stringToNewUTF8(value === null ? "" : value);
    }
    catch (error) { return stringToNewUTF8(""); }
});

EM_JS(int, aowisSavePersonalRepository, (const char *json),
{
    try
    {
        if (!globalThis.localStorage) return 0;
        const value = UTF8ToString(json);
        if (value.length === 0)
            globalThis.localStorage.removeItem("aowis.projects.personalRepository");
        else
            globalThis.localStorage.setItem("aowis.projects.personalRepository", value);
        return 1;
    }
    catch (error) { return 0; }
});
#else
#include <QSettings>
#endif

namespace
{
const QString config_key = QStringLiteral("projects/personal_repository");

bool writeProfile(const QByteArray &json, bool clear)
{
#ifdef __EMSCRIPTEN__
    Q_UNUSED(clear);
    return aowisSavePersonalRepository(json.constData()) != 0;
#else
    (void)guiConfiguration();
    QSettings settings(guiConfigurationFilePath(), QSettings::IniFormat);
    if (clear)
        settings.remove(config_key);
    else
        settings.setValue(config_key, QString::fromUtf8(json));
    settings.sync();
    return settings.status() == QSettings::NoError;
#endif
}
}

bool validatePersonalRepositoryProfile(const PersonalRepositoryProfile &profile, QString *error)
{
    const QUrl url(profile.endpoint);
    const QString path = url.path();
    if (!url.isValid() || (url.scheme() != QStringLiteral("http")
                           && url.scheme() != QStringLiteral("https"))
        || url.host().isEmpty() || !url.userInfo().isEmpty()
        || url.hasQuery() || url.hasFragment()
        || (!path.isEmpty() && path != QStringLiteral("/")
            && path != QStringLiteral("/dvmdb/v1")
            && path != QStringLiteral("/dvmdb/v1/")))
    {
        if (error) *error = QStringLiteral("Use an HTTP(S) DVMDB root without credentials, query or fragment.");
        return false;
    }
    if (!DvmdbIdentity::isUuidV7(profile.repository_id))
    {
        if (error) *error = QStringLiteral("Enter the exact lowercase UUIDv7 from GET /dvmdb/v1/repository.");
        return false;
    }
    return true;
}

std::optional<PersonalRepositoryProfile> loadPersonalRepositoryProfile()
{
    QByteArray json;
#ifdef __EMSCRIPTEN__
    char *stored = aowisLoadPersonalRepository();
    if (stored != nullptr)
    {
        json = QByteArray(stored);
        std::free(stored);
    }
#else
    (void)guiConfiguration();
    QSettings settings(guiConfigurationFilePath(), QSettings::IniFormat);
    json = settings.value(config_key).toString().toUtf8();
    if (settings.status() != QSettings::NoError) return std::nullopt;
#endif
    if (json.isEmpty()) return std::nullopt;
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QJsonObject record = document.object();
    PersonalRepositoryProfile profile{
        record.value(QStringLiteral("endpoint")).toString(),
        record.value(QStringLiteral("repositoryId")).toString()};
    return validatePersonalRepositoryProfile(profile) ? std::optional(profile) : std::nullopt;
}

bool savePersonalRepositoryProfile(const PersonalRepositoryProfile &profile)
{
    if (!validatePersonalRepositoryProfile(profile)) return false;
    const QByteArray json = QJsonDocument(QJsonObject{
        {QStringLiteral("endpoint"), profile.endpoint},
        {QStringLiteral("repositoryId"), profile.repository_id}
    }).toJson(QJsonDocument::Compact);
    return writeProfile(json, false);
}

bool clearPersonalRepositoryProfile()
{
    return writeProfile(QByteArray(), true);
}
