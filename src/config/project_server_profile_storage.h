#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// A locally configured data source. Old endpoint-only records are REST sources.
// Authentication modes are descriptive only; credentials and connection state
// are not part of the stored profile.
struct ProjectServerProfile
{
    QString id;
    QString name;
    QString endpoint;
    QString description;
    QStringList tags;
    QString auth_mode;
    QString connection_type = QStringLiteral("remote"); // "remote" or "local"
    QString local_path;
};

QList<ProjectServerProfile> loadProjectServerProfiles();
bool saveProjectServerProfiles(const QList<ProjectServerProfile> &profiles);
QString defaultLocalDvmdbPath();
// The embedded source exists only in builds with the native DVMDB backend.
// Does not open/create the database; that belongs to the repository adapter.
bool ensureDefaultLocalDvmdbProfile(QList<ProjectServerProfile> &profiles);