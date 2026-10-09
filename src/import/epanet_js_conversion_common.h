#ifndef EPANET_JS_CONVERSION_COMMON_H
#define EPANET_JS_CONVERSION_COMMON_H

#include "import/epanet_js_project_converter.h"
#include <QCryptographicHash>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariant>
#include <cmath>
#include <optional>

// Internal helpers shared by the conversion stages; preserve diagnostic semantics.
namespace EpanetJsConversionCommon
{
// Build a stable, first-match UUID index without retaining pointers into QList.
// Duplicate UUIDs intentionally resolve to the first entity, matching the
// original linear lookup semantics.
template <typename EntityList>
QHash<QUuid, qsizetype> uuidPositionIndex(const EntityList &entities)
{
    QHash<QUuid, qsizetype> positions;
    positions.reserve(entities.size());
    for (qsizetype index = 0; index < entities.size(); ++index)
    {
        const QUuid &uuid = entities.at(index).uuid;
        if (!positions.contains(uuid))
            positions.insert(uuid, index);
    }
    return positions;
}

// Build a UUID membership set for completed entity collections.
// Duplicate UUIDs are intentionally collapsed, as with the previous QSet loops.
template <typename EntityList>
QSet<QUuid> uuidMembershipIndex(const EntityList &entities)
{
    QSet<QUuid> uuids;
    uuids.reserve(entities.size());
    for (const auto &entity : entities)
        uuids.insert(entity.uuid);
    return uuids;
}

inline QUuid uuidV5(const QUuid &namespace_uuid, const QByteArray &name)
{
    QByteArray input = namespace_uuid.toRfc4122();
    input.append(name);
    QByteArray bytes = QCryptographicHash::hash(input, QCryptographicHash::Sha1).left(16);
    if (bytes.size() != 16)
        return {};

    bytes[6] = static_cast<char>((static_cast<unsigned char>(bytes.at(6)) & 0x0fU) | 0x50U);
    bytes[8] = static_cast<char>((static_cast<unsigned char>(bytes.at(8)) & 0x3fU) | 0x80U);
    return QUuid::fromRfc4122(bytes);
}


inline void appendDiagnostic(
    EpanetJsProjectConversionResult &result,
    EpanetJsConversionDiagnosticSeverity severity,
    const QString &code,
    const QString &message,
    const QString &table_name = {},
    std::optional<qint64> source_id = std::nullopt)
{
    EpanetJsConversionDiagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = code;
    diagnostic.message = message;
    diagnostic.table_name = table_name;
    diagnostic.source_id = source_id;
    result.diagnostics.append(diagnostic);
}

inline std::optional<qint64> integerValue(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return std::nullopt;

    bool ok = false;
    const qint64 integer = value.toLongLong(&ok);
    if (!ok)
        return std::nullopt;
    return integer;
}


inline std::optional<double> finiteDouble(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number))
        return std::nullopt;
    return number;
}

inline QJsonObject projectUnitsObject(const QJsonObject &project_settings)
{
    const QJsonValue units = project_settings.value(QStringLiteral("units"));
    return units.isObject() ? units.toObject() : QJsonObject();
}

inline std::optional<double> finiteVariantDouble(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return std::nullopt;

    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number))
        return std::nullopt;
    return number;
}

inline QString firstUnit(const QJsonObject &units, const QStringList &keys)
{
    for (const QString &key : keys)
    {
        const QString unit = units.value(key).toString().trimmed();
        if (!unit.isEmpty())
            return unit;
    }
    return {};
}

inline QString importedEntityId(
    const QVariantMap &row,
    const QString &table_name,
    qint64 source_id,
    EpanetJsProjectConversionResult &result)
{
    const QString label = row.value(QStringLiteral("label")).toString().trimmed();
    if (!label.isEmpty())
        return label;

    const QString generated = QStringLiteral("%1-%2").arg(table_name).arg(source_id);
    appendDiagnostic(
        result,
        EpanetJsConversionDiagnosticSeverity::Warning,
        QStringLiteral("generated-entity-id"),
        QStringLiteral("epanet-js %1 id %2 has no label; AOWIS generated id '%3'.")
            .arg(table_name)
            .arg(source_id)
            .arg(generated),
        table_name,
        source_id);
    return generated;
}

inline bool importStoredWgs84Coordinate(
    const QVariantMap &row,
    const QString &table_name,
    qint64 source_id,
    CoordinateWGS84 &coordinate,
    EpanetJsProjectConversionResult &result)
{
    const std::optional<double> longitude = finiteVariantDouble(row.value(QStringLiteral("coord_x")));
    const std::optional<double> latitude = finiteVariantDouble(row.value(QStringLiteral("coord_y")));
    if (!longitude.has_value() || !latitude.has_value()
        || *longitude < -180.0 || *longitude > 180.0
        || *latitude < -90.0 || *latitude > 90.0)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-node-coordinate"),
            QStringLiteral(
                "epanet-js %1 id %2 has an invalid stored longitude/latitude coordinate.")
                .arg(table_name)
                .arg(source_id),
            table_name,
            source_id);
        return false;
    }

    coordinate.longitude_deg = *longitude;
    coordinate.latitude_deg = *latitude;
    return true;
}


}

#endif // EPANET_JS_CONVERSION_COMMON_H
