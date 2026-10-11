#pragma once

#include <QFileInfo>
#include <QString>

// File-format validation at the export boundary. An EPANET INP document is
// plain text, not an epanet-js SQLite (.ejsdb) database.
namespace EpanetNetworkFileFormat {
inline bool acceptsInpExportPath(const QString &path)
{
    const QFileInfo info(path);
    return !info.fileName().isEmpty()
        && info.suffix().compare(QStringLiteral("inp"), Qt::CaseInsensitive) == 0;
}
} // namespace EpanetNetworkFileFormat
