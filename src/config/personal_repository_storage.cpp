#include "config/personal_repository_storage.h"

#include "config/project_server_profile_storage.h"
#include "projects/dvmdb_identity.h"

#include <QDir>
#include <QUrl>

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
        if (error) *error = QStringLiteral("Personal DVMDB identity has not been verified. Select it in Projects → Server Inspector.");
        return false;
    }
    return true;
}

PersonalRepositoryTarget resolvePersonalRepositoryTarget()
{
    PersonalRepositoryTarget target;
    const QList<ProjectServerProfile> servers = loadProjectServerProfiles();
    for (const ProjectServerProfile &server : servers)
    {
        if (!server.personal) continue;
        if (server.connection_type == QStringLiteral("local"))
        {
#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
            if (QDir::isAbsolutePath(server.local_path))
            {
                target.kind = PersonalRepositoryTarget::Kind::Embedded;
                target.storage_path = QDir::cleanPath(server.local_path);
            }
#endif
            return target;
        }
        const PersonalRepositoryProfile profile{server.endpoint, server.personal_repository_id};
        if (!validatePersonalRepositoryProfile(profile)) return target;
        target.kind = PersonalRepositoryTarget::Kind::Http;
        target.endpoint = profile.endpoint;
        target.repository_id = profile.repository_id;
        return target;
    }
    return target;
}
