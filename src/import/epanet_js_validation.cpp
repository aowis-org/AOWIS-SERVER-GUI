#include "import/epanet_js_validation.h"
#include "import/epanet_js_conversion_common.h"
#include "import/epanet_js_project_identity.h"
#include "import/epanet_js_schema.h"

#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVariant>

namespace
{
using namespace EpanetJsConversionCommon;
using namespace EpanetJsProjectIdentity;
using namespace EpanetJsSchema;

bool localGridProjectionToken(QString token)
{
    token = token.trimmed().toLower();
    token.remove(QLatin1Char(' '));
    token.remove(QLatin1Char('_'));
    token.remove(QLatin1Char('-'));

    return token == QStringLiteral("xy")
        || token == QStringLiteral("xygrid")
        || token == QStringLiteral("grid")
        || token == QStringLiteral("localgrid")
        || token == QStringLiteral("local")
        || token == QStringLiteral("cartesian")
        || token == QStringLiteral("none");
}

} // namespace

bool EpanetJsValidation::validateProjectCoordinateStorage(
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const QJsonValue projection_value = project_settings.value(QStringLiteral("projection"));
    if (projection_value.isUndefined() || projection_value.isNull())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("missing-project-projection"),
            QStringLiteral(
                "The epanet-js project has no projection metadata; AOWIS assumes its stored coordinates are WGS84 longitude/latitude for compatibility with older save files."));
        return true;
    }

    QString projection_type;
    QString projection_id;
    if (projection_value.isObject())
    {
        const QJsonObject projection = projection_value.toObject();
        projection_type = projection.value(QStringLiteral("type")).toString();
        projection_id = projection.value(QStringLiteral("id")).toString();
        if (projection.isEmpty())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Warning,
                QStringLiteral("empty-project-projection"),
                QStringLiteral(
                    "The epanet-js project has empty projection metadata; AOWIS assumes its stored coordinates are WGS84 longitude/latitude."));
            return true;
        }
    }
    else if (projection_value.isString())
    {
        projection_id = projection_value.toString();
    }
    else
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-project-projection"),
            QStringLiteral("The epanet-js project projection metadata has an unsupported data type."));
        return false;
    }

    if (localGridProjectionToken(projection_type) || localGridProjectionToken(projection_id))
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("unsupported-local-grid-projection"),
            QStringLiteral(
                "The epanet-js project uses a non-georeferenced X-Y grid. AOWIS does not yet import local-grid .ejsdb geometry because mapping those coordinates into WGS84 would require inventing a geographic location."));
        return false;
    }

    return true;
}

void EpanetJsValidation::registerEntityIds(
    const EpanetJsProjectSnapshot &project,
    EpanetJsProjectConversionResult &result)
{
    for (const QString &table_name : entityTables())
    {
        const EpanetJsTableSnapshot *table = tableByName(project, table_name);
        if (table == nullptr)
            continue;

        if (!tableHasColumn(table, QStringLiteral("id")))
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("missing-id-column"),
                QStringLiteral("epanet-js table '%1' has no id column.").arg(table_name),
                table_name);
            continue;
        }

        QSet<qint64> seen_ids;
        for (const QVariantMap &row : table->rows)
        {
            const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
            if (!source_id.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-source-id"),
                    QStringLiteral("epanet-js table '%1' contains a row with a non-integer id.")
                        .arg(table_name),
                    table_name);
                continue;
            }

            if (seen_ids.contains(*source_id))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("duplicate-source-id"),
                    QStringLiteral("epanet-js table '%1' contains duplicate id %2.")
                        .arg(table_name)
                        .arg(*source_id),
                    table_name,
                    *source_id);
                continue;
            }
            seen_ids.insert(*source_id);

            QString registration_error;
            if (!result.id_map.registerId(table_name, *source_id, &registration_error))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("id-map-registration-failed"),
                    registration_error,
                    table_name,
                    *source_id);
            }
        }
    }
}

void EpanetJsValidation::validateReferenceDomains(
    const EpanetJsProjectSnapshot &project,
    EpanetJsProjectConversionResult &result)
{
    const QList<QStringList> domains = {nodeTables(), linkTables()};
    const QStringList domain_names = {
        QStringLiteral("node"),
        QStringLiteral("link")
    };

    for (int domain_index = 0; domain_index < domains.size(); ++domain_index)
    {
        QMap<qint64, QStringList> tables_by_id;
        for (const QString &table_name : domains.at(domain_index))
        {
            const EpanetJsTableSnapshot *table = tableByName(project, table_name);
            if (table == nullptr)
                continue;

            for (const QVariantMap &row : table->rows)
            {
                const std::optional<qint64> source_id = integerValue(
                    row.value(QStringLiteral("id")));
                if (!source_id.has_value())
                    continue;
                tables_by_id[*source_id].append(table_name);
            }
        }

        for (QMap<qint64, QStringList>::const_iterator iterator = tables_by_id.cbegin();
             iterator != tables_by_id.cend(); ++iterator)
        {
            QStringList tables = iterator.value();
            tables.removeDuplicates();
            if (tables.size() <= 1)
                continue;

            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("ambiguous-%1-id").arg(domain_names.at(domain_index)),
                QStringLiteral("epanet-js %1 id %2 occurs in multiple tables: %3.")
                    .arg(domain_names.at(domain_index))
                    .arg(iterator.key())
                    .arg(tables.join(QStringLiteral(", "))),
                tables.first(),
                iterator.key());
        }
    }
}

void EpanetJsValidation::validateLinkEndpointReferences(
    const EpanetJsProjectSnapshot &project,
    EpanetJsProjectConversionResult &result)
{
    for (const QString &table_name : linkTables())
    {
        const EpanetJsTableSnapshot *table = tableByName(project, table_name);
        if (table == nullptr)
            continue;

        const bool has_start = tableHasColumn(table, QStringLiteral("start_node_id"));
        const bool has_end = tableHasColumn(table, QStringLiteral("end_node_id"));
        if (!has_start || !has_end)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("missing-endpoint-column"),
                QStringLiteral("epanet-js table '%1' is missing start_node_id or end_node_id.")
                    .arg(table_name),
                table_name);
            continue;
        }

        for (const QVariantMap &row : table->rows)
        {
            const std::optional<qint64> link_id = integerValue(row.value(QStringLiteral("id")));
            for (const QString &column_name : {QStringLiteral("start_node_id"),
                                               QStringLiteral("end_node_id")})
            {
                const std::optional<qint64> node_id = integerValue(row.value(column_name));
                if (!node_id.has_value())
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("invalid-node-reference"),
                        QStringLiteral("epanet-js %1 id %2 has a non-integer %3.")
                            .arg(table_name)
                            .arg(link_id.has_value() ? QString::number(*link_id) : QStringLiteral("?"))
                            .arg(column_name),
                        table_name,
                        link_id);
                    continue;
                }

                const QList<QString> matching_tables = result.id_map.nodeTablesForId(*node_id);
                if (matching_tables.isEmpty())
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("missing-node-reference"),
                        QStringLiteral("epanet-js %1 id %2 references missing node id %3 through %4.")
                            .arg(table_name)
                            .arg(link_id.has_value() ? QString::number(*link_id) : QStringLiteral("?"))
                            .arg(*node_id)
                            .arg(column_name),
                        table_name,
                        link_id);
                }
                else if (matching_tables.size() > 1)
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("ambiguous-node-reference"),
                        QStringLiteral("epanet-js node id %1 occurs in multiple node tables: %2.")
                            .arg(*node_id)
                            .arg(matching_tables.join(QStringLiteral(", "))),
                        table_name,
                        link_id);
                }
            }
        }
    }
}
