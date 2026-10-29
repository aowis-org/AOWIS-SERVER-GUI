#pragma once

#include <QString>

#include <optional>

// A separately configured *personal* DVMDB. This does not grant permissions
// or allow writes: VC-04 adds the actual private working-copy operations.
// Never derive this setting from the selected source database profile.
struct PersonalRepositoryProfile
{
    QString endpoint;
    QString repository_id;
};

// Accept only credential-free DVMDB service roots (no query/fragment).
// The expected UUIDv7 pins an identity, not an authenticated user.
bool validatePersonalRepositoryProfile(const PersonalRepositoryProfile &profile,
                                       QString *error = nullptr);
std::optional<PersonalRepositoryProfile> loadPersonalRepositoryProfile();
bool savePersonalRepositoryProfile(const PersonalRepositoryProfile &profile);
bool clearPersonalRepositoryProfile();
