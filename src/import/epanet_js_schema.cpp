#include "import/epanet_js_schema.h"

namespace EpanetJsSchema
{
const EpanetJsTableSnapshot *tableByName(
    const EpanetJsProjectSnapshot &project,
    const QString &table_name)
{
    const QMap<QString, EpanetJsTableSnapshot>::const_iterator iterator =
        project.tables.constFind(table_name);
    if (iterator == project.tables.cend())
        return nullptr;
    return &iterator.value();
}

bool tableHasColumn(const EpanetJsTableSnapshot *table, const QString &column_name)
{
    return table != nullptr && table->columns.contains(column_name);
}

}
