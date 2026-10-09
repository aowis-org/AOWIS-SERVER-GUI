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

void testReadsDatabaseFileDirectly()
{
    const QByteArray database = createDatabase(24, true, true, false);
    QTemporaryDir directory;
    expectTrue(directory.isValid(), "direct file test temporary directory is created");
    if (!directory.isValid() || database.isEmpty())
        return;

    const QString file_path = directory.filePath(QStringLiteral("direct.ejsdb"));
    QFile file(file_path);
    expectTrue(file.open(QIODevice::WriteOnly), "direct file test project file is created");
    if (!file.isOpen())
        return;
    const bool written = file.write(database) == database.size();
    file.close();
    expectTrue(written, "direct file test writes the complete SQLite database");
    if (!written)
        return;

    const EpanetJsProjectReadResult result = EpanetJsProjectReader::readFile(file_path);
    expectTrue(result.success, "reader imports SQLite project directly from file path");
    expectTrue(result.project.user_version == 24, "direct file import preserves user_version");
    expectTrue(result.project.hasTable(QStringLiteral("pipes")), "direct file import preserves tables");
}

void testRejectsNonSqliteFile()
{
    QTemporaryDir directory;
    expectTrue(directory.isValid(), "invalid file test temporary directory is created");
    if (!directory.isValid())
        return;

    const QString file_path = directory.filePath(QStringLiteral("invalid.ejsdb"));
    QFile file(file_path);
    if (!file.open(QIODevice::WriteOnly))
    {
        expectTrue(false, "invalid file test project file is created");
        return;
    }
    file.write("not sqlite");
    file.close();

    const EpanetJsProjectReadResult result = EpanetJsProjectReader::readFile(file_path);
    expectTrue(!result.success, "reader rejects a non-SQLite file path");
    expectTrue(result.error.contains(QStringLiteral("not a SQLite")),
               "non-SQLite file path reports the format mismatch");
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

void testRejectsCorruptedSqliteContentWithValidHeader()
{
    const QByteArray complete_database = createDatabase(24, true, true, false);
    expectTrue(!complete_database.isEmpty(),
               "complete SQLite fixture exists before corruption");

    const qsizetype truncated_size = qMin<qsizetype>(complete_database.size(), 128);
    const QByteArray corrupted_database = complete_database.left(truncated_size);
    expectTrue(corrupted_database.startsWith(QByteArray("SQLite format 3\0", 16)),
               "corrupted fixture retains a valid SQLite header");
    expectTrue(corrupted_database.size() < complete_database.size(),
               "corrupted fixture is actually truncated");

    const EpanetJsProjectReadResult result = EpanetJsProjectReader::readBytes(corrupted_database);
    expectTrue(!result.success,
               "reader rejects a corrupted SQLite database even when its header is valid");
    expectTrue(!result.error.isEmpty(),
               "corrupted SQLite rejection includes a diagnostic");
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
    test_harness.runCase("direct SQLite file reading", testReadsDatabaseFileDirectly);
    test_harness.runCase("non-SQLite file reading", testRejectsNonSqliteFile);
    test_harness.runCase("future epanet-js schema", testAcceptsFutureSchemaAndPreservesUnknownData);
    test_harness.runCase("minimal epanet-js schema", testAcceptsMinimalNetworkSignatureWithoutProjectTable);
    test_harness.runCase("unrelated SQLite", testRejectsUnrelatedSqliteDatabase);
    test_harness.runCase("corrupted SQLite", testRejectsCorruptedSqliteContentWithValidHeader);
    test_harness.runCase("non-SQLite input", testRejectsNonSqliteContent);
    return test_harness.finish();
}
