#include "import/epanet_js_project_reader.h"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryFile>
#include <QUuid>

namespace
{
QString quotedIdentifier(QString identifier)
{
    identifier.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"") + identifier + QStringLiteral("\"");
}

bool hasColumns(const EpanetJsTableSnapshot *table, const QStringList &columns)
{
    if (table == nullptr)
        return false;

    for (const QString &column : columns)
    {
        if (!table->hasColumn(column))
            return false;
    }
    return true;
}

bool looksLikeEpanetJs(const EpanetJsProjectSnapshot &project)
{
    const EpanetJsTableSnapshot *project_table = project.table(QStringLiteral("project"));
    if (project_table != nullptr
        && (project_table->hasColumn(QStringLiteral("settings"))
            || project_table->hasColumn(QStringLiteral("pipe_library"))))
    {
        return true;
    }

    const bool has_junction_shape = hasColumns(
        project.table(QStringLiteral("junctions")),
        {QStringLiteral("id"), QStringLiteral("label"),
         QStringLiteral("coord_x"), QStringLiteral("coord_y")});
    const bool has_pipe_shape = hasColumns(
        project.table(QStringLiteral("pipes")),
        {QStringLiteral("id"), QStringLiteral("label"),
         QStringLiteral("start_node_id"), QStringLiteral("end_node_id")});
    if (has_junction_shape && has_pipe_shape)
        return true;

    static const QStringList known_tables = {
        QStringLiteral("controls"),
        QStringLiteral("curves"),
        QStringLiteral("customer_point_demands"),
        QStringLiteral("customer_points"),
        QStringLiteral("junction_demands"),
        QStringLiteral("junctions"),
        QStringLiteral("patterns"),
        QStringLiteral("pipes"),
        QStringLiteral("project"),
        QStringLiteral("pumps"),
        QStringLiteral("raw_controls"),
        QStringLiteral("reservoirs"),
        QStringLiteral("scenarios"),
        QStringLiteral("selection_sets"),
        QStringLiteral("simulation_settings"),
        QStringLiteral("tanks"),
        QStringLiteral("valves"),
        QStringLiteral("zones")
    };

    int known_table_count = 0;
    for (const QString &table_name : known_tables)
    {
        if (project.hasTable(table_name))
            ++known_table_count;
    }

    return known_table_count >= 4
        && (project.hasTable(QStringLiteral("junctions"))
            || project.hasTable(QStringLiteral("pipes"))
            || project.hasTable(QStringLiteral("customer_points")));
}

EpanetJsProjectReadResult readDatabasePath(const QString &file_path)
{
    EpanetJsProjectReadResult result;

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")))
    {
        result.error = QStringLiteral("Qt SQLite driver QSQLITE is not available.");
        return result;
    }

    const QString connection_name = QStringLiteral("aowis_epanet_js_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connection_name);
        database.setDatabaseName(file_path);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));

        if (!database.open())
        {
            result.error = QStringLiteral("Could not open SQLite project: %1")
                .arg(database.lastError().text());
        }
        else
        {
            QSqlQuery version_query(database);
            if (version_query.exec(QStringLiteral("PRAGMA user_version"))
                && version_query.next())
            {
                result.project.user_version = version_query.value(0).toInt();
            }

            QStringList table_names;
            QSqlQuery table_query(database);
            if (!table_query.exec(QStringLiteral(
                    "SELECT name FROM sqlite_master "
                    "WHERE type = 'table' AND name NOT LIKE 'sqlite_%' "
                    "ORDER BY name")))
            {
                result.error = QStringLiteral("Could not enumerate SQLite tables: %1")
                    .arg(table_query.lastError().text());
            }
            else
            {
                while (table_query.next())
                    table_names.append(table_query.value(0).toString());

                bool table_read_failed = false;
                for (const QString &table_name : table_names)
                {
                    QSqlQuery row_query(database);
                    const QString statement = QStringLiteral("SELECT * FROM %1")
                        .arg(quotedIdentifier(table_name));
                    if (!row_query.exec(statement))
                    {
                        result.error = QStringLiteral("Could not read table '%1': %2")
                            .arg(table_name, row_query.lastError().text());
                        table_read_failed = true;
                        break;
                    }

                    EpanetJsTableSnapshot table;
                    table.name = table_name;
                    const QSqlRecord record = row_query.record();
                    for (int column_index = 0; column_index < record.count(); ++column_index)
                        table.columns.append(record.fieldName(column_index));

                    while (row_query.next())
                    {
                        QVariantMap row;
                        for (int column_index = 0; column_index < record.count(); ++column_index)
                        {
                            row.insert(
                                record.fieldName(column_index),
                                row_query.value(column_index));
                        }
                        table.rows.append(row);
                    }

                    result.project.tables.insert(table_name, table);
                }

                if (!table_read_failed && !looksLikeEpanetJs(result.project))
                {
                    result.error = QStringLiteral(
                        "The SQLite database does not contain a recognizable epanet-js project schema.");
                }
                else if (!table_read_failed)
                {
                    result.success = true;
                }
            }

            database.close();
        }
    }

    QSqlDatabase::removeDatabase(connection_name);
    return result;
}
}

bool EpanetJsTableSnapshot::hasColumn(const QString &column_name) const
{
    return this->columns.contains(column_name);
}

bool EpanetJsProjectSnapshot::hasTable(const QString &table_name) const
{
    return this->tables.contains(table_name);
}

const EpanetJsTableSnapshot *EpanetJsProjectSnapshot::table(const QString &table_name) const
{
    const QMap<QString, EpanetJsTableSnapshot>::const_iterator iterator =
        this->tables.constFind(table_name);
    if (iterator == this->tables.cend())
        return nullptr;
    return &iterator.value();
}

qsizetype EpanetJsProjectSnapshot::totalRowCount() const
{
    qsizetype count = 0;
    for (const EpanetJsTableSnapshot &table : this->tables)
        count += table.rows.size();
    return count;
}

EpanetJsProjectReadResult EpanetJsProjectReader::readFile(const QString &file_path)
{
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly))
    {
        EpanetJsProjectReadResult result;
        result.error = QStringLiteral("Could not open project file '%1'.").arg(file_path);
        return result;
    }

    const QByteArray file_content = file.readAll();
    file.close();
    return readBytes(file_content);
}

EpanetJsProjectReadResult EpanetJsProjectReader::readBytes(const QByteArray &file_content)
{
    EpanetJsProjectReadResult result;
    static const QByteArray sqlite_header("SQLite format 3\0", 16);
    if (file_content.size() < sqlite_header.size()
        || file_content.left(sqlite_header.size()) != sqlite_header)
    {
        result.error = QStringLiteral("The selected file is not a SQLite database.");
        return result;
    }

    QTemporaryFile temporary_file(
        QDir::tempPath() + QStringLiteral("/aowis-epanet-js-XXXXXX.ejsdb"));
    temporary_file.setAutoRemove(true);
    if (!temporary_file.open())
    {
        result.error = QStringLiteral("Could not create temporary storage for the epanet-js project.");
        return result;
    }

    if (temporary_file.write(file_content) != file_content.size()
        || !temporary_file.flush())
    {
        result.error = QStringLiteral("Could not prepare the epanet-js project for SQLite import.");
        return result;
    }

    const QString temporary_file_path = temporary_file.fileName();
    temporary_file.close();
    return readDatabasePath(temporary_file_path);
}
