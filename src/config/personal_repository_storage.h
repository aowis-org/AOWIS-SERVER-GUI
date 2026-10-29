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

// Resolve the intended PRIVATE repository independently of selected source
// profiles. Resolution does not verify identity/role, establish a session,
// open the store, or grant permission to edit (VC-04).
struct PersonalRepositoryTarget
{
    enum class Kind { Unavailable, Embedded, Http };
    Kind kind = Kind::Unavailable;
    QString storage_path;    // Embedded only, never a shared-source path.
    QString endpoint;        // HTTP only.
    QString repository_id;   // Expected HTTP UUIDv7; verified on connection.
};

// The Standalone private store is a stable, separate root from the built-in
// local source DVMDB. Neither this getter nor resolve opens the database.
QString defaultPersonalDvmdbPath();
PersonalRepositoryTarget resolvePersonalRepositoryTarget();
