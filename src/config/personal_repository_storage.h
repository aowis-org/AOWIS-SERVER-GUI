#pragma once

#include <QString>

// Internal identity pin used by HTTP requests. The user never enters UUIDs.
// The pinned identity is discovered when a Projects server is designated personal.
struct PersonalRepositoryProfile
{
    QString endpoint;
    QString repository_id;
};

bool validatePersonalRepositoryProfile(const PersonalRepositoryProfile &profile,
                                       QString *error = nullptr);

struct PersonalRepositoryTarget
{
    enum class Kind { Unavailable, Embedded, Http };
    Kind kind = Kind::Unavailable;
    QString storage_path;    // Selected embedded source profile path.
    QString endpoint;        // Selected HTTP source profile endpoint.
    QString repository_id;   // Pinned automatically discovered HTTP UUIDv7.
};

PersonalRepositoryTarget resolvePersonalRepositoryTarget();
