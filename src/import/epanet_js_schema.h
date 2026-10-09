#ifndef EPANET_JS_SCHEMA_H
#define EPANET_JS_SCHEMA_H

#include "import/epanet_js_project_reader.h"

namespace EpanetJsSchema
{
const EpanetJsTableSnapshot *tableByName(const EpanetJsProjectSnapshot &project, const QString &table_name);
bool tableHasColumn(const EpanetJsTableSnapshot *table, const QString &column_name);
}

#endif // EPANET_JS_SCHEMA_H
