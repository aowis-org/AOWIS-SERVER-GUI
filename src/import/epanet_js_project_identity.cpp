#include "import/epanet_js_project_identity.h"
#include "import/epanet_js_project_converter.h"
#include "import/epanet_js_schema.h"
#include "import/epanet_js_conversion_common.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QVariant>
#include <algorithm>

namespace EpanetJsProjectIdentity
{
using namespace EpanetJsSchema;
QStringList entityTables()
{
    return {
        QStringLiteral("junctions"),
        QStringLiteral("reservoirs"),
        QStringLiteral("tanks"),
        QStringLiteral("pipes"),
        QStringLiteral("pumps"),
        QStringLiteral("valves"),
        QStringLiteral("patterns"),
        QStringLiteral("curves"),
        QStringLiteral("customer_points"),
        QStringLiteral("zones")
    };
}

QStringList nodeTables()
{
    return {
        QStringLiteral("junctions"),
        QStringLiteral("reservoirs"),
        QStringLiteral("tanks")
    };
}

QStringList linkTables()
{
    return {
        QStringLiteral("pipes"),
        QStringLiteral("pumps"),
        QStringLiteral("valves")
    };
}

QUuid aowisEpanetJsNamespace()
{
    return QUuid(QStringLiteral("{d150f58c-c6c4-5d39-94d5-24f709935868}"));
}

static QString projectSettingsText(const EpanetJsProjectSnapshot &project)
{
    const EpanetJsTableSnapshot *project_table = tableByName(project, QStringLiteral("project"));
    if (project_table == nullptr || project_table->rows.isEmpty())
        return {};
    return project_table->rows.first().value(QStringLiteral("settings")).toString();
}

QJsonObject projectSettingsObject(const EpanetJsProjectSnapshot &project)
{
    const QByteArray settings = projectSettingsText(project).toUtf8();
    if (settings.isEmpty())
        return {};

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(settings, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    return document.object();
}

QByteArray fallbackProjectIdentitySeed(const EpanetJsProjectSnapshot &project)
{
    QByteArray seed("aowis/epanet-js/project\n");
    const QString settings = projectSettingsText(project);
    if (!settings.isEmpty())
    {
        seed.append("settings=");
        seed.append(settings.toUtf8());
        seed.append('\n');
    }

    for (const QString &table_name : entityTables())
    {
        const EpanetJsTableSnapshot *table = tableByName(project, table_name);
        if (table == nullptr)
            continue;

        QList<QPair<QString, QString>> identities;
        for (const QVariantMap &row : table->rows)
        {
            identities.append(qMakePair(
                row.value(QStringLiteral("id")).toString(),
                row.value(QStringLiteral("label")).toString()));
        }
        std::sort(identities.begin(), identities.end(),
                  [](const QPair<QString, QString> &left,
                     const QPair<QString, QString> &right)
                  {
                      if (left.first != right.first)
                          return left.first < right.first;
                      return left.second < right.second;
                  });

        seed.append(table_name.toUtf8());
        seed.append('=');
        for (const QPair<QString, QString> &identity : identities)
        {
            seed.append(identity.first.toUtf8());
            seed.append(':');
            seed.append(identity.second.toUtf8());
            seed.append(';');
        }
        seed.append('\n');
    }

    return seed;
}

}

// Source-ID map definitions live with project identity handling.
using namespace EpanetJsProjectIdentity;
using EpanetJsConversionCommon::uuidV5;

EpanetJsProjectIdMap::EpanetJsProjectIdMap(const QUuid &project_uuid)
{
    this->project_uuid = project_uuid;
}

QUuid EpanetJsProjectIdMap::projectUuid() const
{
    return this->project_uuid;
}

bool EpanetJsProjectIdMap::registerId(
    const QString &table_name,
    qint64 source_id,
    QString *error)
{
    if (this->project_uuid.isNull())
    {
        if (error != nullptr)
            *error = QStringLiteral("Cannot register epanet-js ids without a project UUID.");
        return false;
    }
    if (table_name.isEmpty())
    {
        if (error != nullptr)
            *error = QStringLiteral("Cannot register an epanet-js id without a table name.");
        return false;
    }

    QMap<qint64, QUuid> &table_map = this->uuids_by_table[table_name];
    if (table_map.contains(source_id))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("epanet-js table '%1' already contains id %2.")
                .arg(table_name)
                .arg(source_id);
        }
        return false;
    }

    const QByteArray stable_name = table_name.toUtf8()
        + QByteArrayLiteral("/")
        + QByteArray::number(source_id);
    table_map.insert(source_id, uuidV5(this->project_uuid, stable_name));
    return true;
}

bool EpanetJsProjectIdMap::contains(const QString &table_name, qint64 source_id) const
{
    const QMap<QString, QMap<qint64, QUuid>>::const_iterator table_iterator =
        this->uuids_by_table.constFind(table_name);
    if (table_iterator == this->uuids_by_table.cend())
        return false;
    return table_iterator.value().contains(source_id);
}

QUuid EpanetJsProjectIdMap::uuidFor(const QString &table_name, qint64 source_id) const
{
    const QMap<QString, QMap<qint64, QUuid>>::const_iterator table_iterator =
        this->uuids_by_table.constFind(table_name);
    if (table_iterator == this->uuids_by_table.cend())
        return {};
    return table_iterator.value().value(source_id);
}

qsizetype EpanetJsProjectIdMap::size() const
{
    qsizetype count{0};
    for (const QMap<qint64, QUuid> &table_map : this->uuids_by_table)
        count += table_map.size();
    return count;
}

QList<QString> EpanetJsProjectIdMap::nodeTablesForId(qint64 source_id) const
{
    QList<QString> matches;
    for (const QString &table_name : nodeTables())
    {
        if (contains(table_name, source_id))
            matches.append(table_name);
    }
    return matches;
}

QList<QString> EpanetJsProjectIdMap::linkTablesForId(qint64 source_id) const
{
    QList<QString> matches;
    for (const QString &table_name : linkTables())
    {
        if (contains(table_name, source_id))
            matches.append(table_name);
    }
    return matches;
}

QUuid EpanetJsProjectIdMap::nodeUuid(qint64 source_id) const
{
    const QList<QString> matches = nodeTablesForId(source_id);
    if (matches.size() != 1)
        return {};
    return uuidFor(matches.first(), source_id);
}

QUuid EpanetJsProjectIdMap::linkUuid(qint64 source_id) const
{
    const QList<QString> matches = linkTablesForId(source_id);
    if (matches.size() != 1)
        return {};
    return uuidFor(matches.first(), source_id);
}


