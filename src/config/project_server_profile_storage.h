#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// A locally configured endpoint. Authentication modes are descriptive only;
// credentials and connection state are not part of the stored profile.
struct ProjectServerProfile
{
    QString id;
    QString name;
    QString endpoint;
    QString description;
    QStringList tags;
    QString auth_mode;
};

QList<ProjectServerProfile> loadProjectServerProfiles();
bool saveProjectServerProfiles(const QList<ProjectServerProfile> &profiles);