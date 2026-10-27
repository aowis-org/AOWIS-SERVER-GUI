#pragma once

#include <QString>

// DVMDB transport identities. Domain-object UUIDv7s identify logical
// revisions/branches; legacy SHA-256 values remain readable until the backend
// v2 graph and REST contracts are switched. Never derive a UUID from a hash.
namespace DvmdbIdentity
{
enum class RevisionFormat { Invalid, LegacySha256, UuidV7 };

inline bool isUuidV7(const QString &value)
{
    if (value.size() != 36) return false;
    for (int i = 0; i < value.size(); ++i)
    {
        const QChar c = value.at(i);
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (c != QLatin1Char('-')) return false;
        }
        else if (i == 14)
        {
            if (c != QLatin1Char('7')) return false;
        }
        else if (i == 19)
        {
            if (c != QLatin1Char('8') && c != QLatin1Char('9')
                && c != QLatin1Char('a') && c != QLatin1Char('b')) return false;
        }
        else if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                   || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))))
            return false;
    }
    return true;
}

inline bool isLegacySha256(const QString &value)
{
    if (value.size() != 64) return false;
    for (const QChar c : value)
        if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
              || (c >= QLatin1Char('a') && c <= QLatin1Char('f')))) return false;
    return true;
}

inline RevisionFormat revisionFormat(const QString &value)
{
    if (isUuidV7(value)) return RevisionFormat::UuidV7;
    if (isLegacySha256(value)) return RevisionFormat::LegacySha256;
    return RevisionFormat::Invalid;
}

// UUIDv7's leading twelve characters encode a millisecond timestamp and
// can be identical for distinct revisions created close together. Display
// its random trailing portion; keep the FULL ID as the selection key.
inline QString shortLabel(const QString &value)
{
    return isUuidV7(value) ? value.right(12) : value.left(12);
}

inline QString revisionFormatName(const QString &value)
{
    switch (revisionFormat(value))
    {
        case RevisionFormat::UuidV7: return QStringLiteral("UUIDv7");
        case RevisionFormat::LegacySha256: return QStringLiteral("Legacy SHA-256");
        case RevisionFormat::Invalid: return QStringLiteral("Unknown identity");
    }
    return QStringLiteral("Unknown identity");
}
}
