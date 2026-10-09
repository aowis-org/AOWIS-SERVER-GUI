#ifndef EPANET_JS_PROJECT_IMPORTER_H
#define EPANET_JS_PROJECT_IMPORTER_H

#include "import/epanet_js_project_converter.h"

#include <QByteArray>
#include <QString>

struct EpanetJsProjectImportResult
{
    bool recognized = false;
    QString read_error;
    EpanetJsProjectConversionResult conversion;

    bool success() const { return read_error.isEmpty() && conversion.success; }
};

class EpanetJsProjectImporter
{
public:
    static bool accepts(const QString &file_name, const QByteArray &file_content);
    static EpanetJsProjectImportResult importBytes(const QByteArray &file_content);
    static EpanetJsProjectImportResult importFile(const QString &file_path);
};

#endif // EPANET_JS_PROJECT_IMPORTER_H
