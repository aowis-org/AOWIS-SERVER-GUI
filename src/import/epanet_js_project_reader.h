#ifndef EPANET_JS_PROJECT_READER_H
#define EPANET_JS_PROJECT_READER_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

struct EpanetJsTableSnapshot
{
    QString name;
    QStringList columns;
    QList<QVariantMap> rows;

    bool hasColumn(const QString &column_name) const;
};

struct EpanetJsProjectSnapshot
{
    int user_version = 0;
    QMap<QString, EpanetJsTableSnapshot> tables;

    bool hasTable(const QString &table_name) const;
    const EpanetJsTableSnapshot *table(const QString &table_name) const;
    qsizetype totalRowCount() const;
};

struct EpanetJsProjectReadResult
{
    bool success = false;
    QString error;
    EpanetJsProjectSnapshot project;
};

class EpanetJsProjectReader
{
public:
    static bool hasSqliteHeader(const QByteArray &file_content);
    static EpanetJsProjectReadResult readFile(const QString &file_path);
    static EpanetJsProjectReadResult readBytes(const QByteArray &file_content);
};

#endif // EPANET_JS_PROJECT_READER_H
