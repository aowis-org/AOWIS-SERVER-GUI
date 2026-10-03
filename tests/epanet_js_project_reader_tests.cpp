#include "import/epanet_js_project_reader.h"
#include "test_harness.h"

#include <QCoreApplication>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

namespace
{
AowisTestHarness test_harness;

void expectTrue(bool condition, const char *message)
{
    test_harness.expectTrue(condition, message);
}

QByteArray createDatabase(
    int user_version,
    bool include_project_signature,
    bool include_network_signature,
    bool include_future_schema)
{
    QTemporaryDir directory;
    if (!directory.isValid())
        return {};

    const QString database_path = directory.filePath(QStringLiteral("project.ejsdb"));
    const QString connection_name = QStringLiteral("aowis_epanet_js_test_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    bool created = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connection_name);
        database.setDatabaseName(database_path);
        if (database.open())
        {
            QSqlQuery query(database);
            created = query.exec(QStringLiteral("PRAGMA user_version = %1").arg(user_version));

            if (created && include_project_signature)
            {
                created = query.exec(QStringLiteral(
                    "CREATE TABLE project ("
                    "id INTEGER PRIMARY KEY, settings TEXT, pipe_library TEXT)"));
                if (created)
                {
                    created = query.exec(QStringLiteral(
                        "INSERT INTO project (id, settings, pipe_library) VALUES "
                        "(1, '{\"name\":\"test\"}', '[{\"label\":\"AOWIS Synthetic Polymer\",\"entries\":[]}]')"));
                }
            }

            if (created && include_network_signature)
            {
                created = query.exec(QStringLiteral(
                    "CREATE TABLE junctions ("
                    "id INTEGER PRIMARY KEY, label TEXT, coord_x REAL, coord_y REAL, elevation REAL)"));
                if (created)
                {
                    created = query.exec(QStringLiteral(
                        "INSERT INTO junctions (id, label, coord_x, coord_y, elevation) "
                        "VALUES (1, 'J1', 18.0, 11.0, 42.0)"));
                }
                if (created)
                {
                    const QString extra_column = include_future_schema
                        ? QStringLiteral(", future_note TEXT")
                        : QString();
                    created = query.exec(QStringLiteral(
                        "CREATE TABLE pipes ("
                        "id INTEGER PRIMARY KEY, label TEXT, start_node_id INTEGER, end_node_id INTEGER, "
                        "coords TEXT, length REAL%1)").arg(extra_column));
                }
                if (created)
                {
                    const QString insert_statement = include_future_schema
                        ? QStringLiteral(
                            "INSERT INTO pipes "
                            "(id, label, start_node_id, end_node_id, coords, length, future_note) "
                            "VALUES (2, 'P1', 1, 1, '[[18,11],[18.1,11.1]]', 10.0, 'preserved')")
                        : QStringLiteral(
                            "INSERT INTO pipes "
                            "(id, label, start_node_id, end_node_id, coords, length) "
                            "VALUES (2, 'P1', 1, 1, '[[18,11],[18.1,11.1]]', 10.0)");
                    created = query.exec(insert_statement);
                }
            }

            if (created && include_future_schema)
            {
                created = query.exec(QStringLiteral(
                    "CREATE TABLE future_epanet_js_table (id INTEGER PRIMARY KEY, payload TEXT)"));
                if (created)
                    created = query.exec(QStringLiteral(
                        "INSERT INTO future_epanet_js_table (id, payload) VALUES (1, 'future')"));
            }

            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connection_name);

    if (!created)
        return {};

    QFile file(database_path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray createUnrelatedDatabase()
{
    QTemporaryDir directory;
    if (!directory.isValid())
        return {};

    const QString database_path = directory.filePath(QStringLiteral("other.sqlite"));
    const QString connection_name = QStringLiteral("aowis_unrelated_sqlite_test_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    bool created = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connection_name);
        database.setDatabaseName(database_path);
        if (database.open())
        {
            QSqlQuery query(database);
            created = query.exec(QStringLiteral(
                "CREATE TABLE unrelated (id INTEGER PRIMARY KEY, value TEXT)"));
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connection_name);

    if (!created)
        return {};

    QFile file(database_path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void testReadsCurrentStyleProject()
{
    const QByteArray database = createDatabase(24, true, true, false);
    expectTrue(!database.isEmpty(), "test epanet-js SQLite fixture is created");

    const EpanetJsProjectReadResult result = EpanetJsProjectReader::readBytes(database);
    expectTrue(result.success, "reader accepts a current-style epanet-js project");
    expectTrue(result.project.user_version == 24, "reader records SQLite user_version without gating on it");
    expectTrue(result.project.hasTable(QStringLiteral("project")), "reader snapshots project table");
    expectTrue(result.project.hasTable(QStringLiteral("junctions")), "reader snapshots junction table");
    expectTrue(result.project.hasTable(QStringLiteral("pipes")), "reader snapshots pipe table");
    expectTrue(result.project.totalRowCount() == 3, "reader counts rows across snapshotted tables");

    const EpanetJsTableSnapshot *pipe_table = result.project.table(QStringLiteral("pipes"));
    expectTrue(pipe_table != nullptr, "pipe table can be retrieved by name");
    if (pipe_table != nullptr)
    {
        expectTrue(pipe_table->hasColumn(QStringLiteral("start_node_id")),
                   "reader retains pipe column names");
        expectTrue(pipe_table->rows.size() == 1, "reader retains pipe rows");
        if (!pipe_table->rows.isEmpty())
        {
            expectTrue(pipe_table->rows.first().value(QStringLiteral("label")).toString()
                           == QStringLiteral("P1"),
                       "reader retains pipe values by column name");
        }
    }
}

void testAcceptsFutureSchemaAndPreservesUnknownData()
{
    const QByteArray database = createDatabase(999, true, true, true);
    const EpanetJsProjectReadResult result = EpanetJsProjectReader::readBytes(database);

    expectTrue(result.success, "reader accepts a future epanet-js user_version");
    expectTrue(result.project.user_version == 999, "future user_version is recorded verbatim");
    expectTrue(result.project.hasTable(QStringLiteral("future_epanet_js_table")),
               "reader preserves unknown future tables");

    const EpanetJsTableSnapshot *pipe_table = result.project.table(QStringLiteral("pipes"));
    expectTrue(pipe_table != nullptr && pipe_table->hasColumn(QStringLiteral("future_note")),
               "reader preserves unknown future columns");
    if (pipe_table != nullptr && !pipe_table->rows.isEmpty())
    {
        expectTrue(pipe_table->rows.first().value(QStringLiteral("future_note")).toString()
                       == QStringLiteral("preserved"),
                   "reader preserves unknown future column values");
    }
}

void testAcceptsMinimalNetworkSignatureWithoutProjectTable()
{
    const QByteArray database = createDatabase(7, false, true, false);
    const EpanetJsProjectReadResult result = EpanetJsProjectReader::readBytes(database);

    expectTrue(result.success,
               "reader accepts an older/minimal project when junction and pipe schemas match");
    expectTrue(!result.project.hasTable(QStringLiteral("project")),
               "project table is not required when the hydraulic schema is recognizable");
}

void testRejectsUnrelatedSqliteDatabase()
{
    const QByteArray database = createUnrelatedDatabase();
    const EpanetJsProjectReadResult result = EpanetJsProjectReader::readBytes(database);

    expectTrue(!result.success, "reader rejects an unrelated SQLite database");
    expectTrue(!result.error.isEmpty(), "unrelated SQLite rejection includes a diagnostic");
}

void testRejectsNonSqliteContent()
{
    const EpanetJsProjectReadResult result = EpanetJsProjectReader::readBytes(
        QByteArrayLiteral("not a sqlite database"));
    expectTrue(!result.success, "reader rejects non-SQLite content");
    expectTrue(result.error.contains(QStringLiteral("not a SQLite")),
               "non-SQLite rejection explains the format mismatch");
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    test_harness.runCase("current epanet-js schema", testReadsCurrentStyleProject);
    test_harness.runCase("future epanet-js schema", testAcceptsFutureSchemaAndPreservesUnknownData);
    test_harness.runCase("minimal epanet-js schema", testAcceptsMinimalNetworkSignatureWithoutProjectTable);
    test_harness.runCase("unrelated SQLite", testRejectsUnrelatedSqliteDatabase);
    test_harness.runCase("non-SQLite input", testRejectsNonSqliteContent);
    return test_harness.finish();
}
