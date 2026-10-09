#include "import/epanet_js_project_importer.h"

#include <QFileInfo>

bool EpanetJsProjectImporter::accepts(const QString &file_name, const QByteArray &file_content)
{
    return EpanetJsProjectReader::hasSqliteHeader(file_content)
        || QFileInfo(file_name).suffix().compare(QStringLiteral("ejsdb"), Qt::CaseInsensitive) == 0;
}

namespace
{
EpanetJsProjectImportResult convertReadResult(const EpanetJsProjectReadResult &read_result)
{
    EpanetJsProjectImportResult result;
    result.recognized = read_result.success;
    if (!read_result.success)
    {
        result.read_error = read_result.error;
        return result;
    }

    result.conversion = EpanetJsProjectConverter::convert(read_result.project);
    return result;
}
}

EpanetJsProjectImportResult EpanetJsProjectImporter::importBytes(const QByteArray &file_content)
{
    return convertReadResult(EpanetJsProjectReader::readBytes(file_content));
}

EpanetJsProjectImportResult EpanetJsProjectImporter::importFile(const QString &file_path)
{
    return convertReadResult(EpanetJsProjectReader::readFile(file_path));
}
