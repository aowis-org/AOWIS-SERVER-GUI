#ifndef EPANET_JS_DEMAND_POINTS_H
#define EPANET_JS_DEMAND_POINTS_H

#include "import/epanet_js_project_converter.h"

namespace EpanetJsDemandPoints
{
void importCustomerPoints(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result);
}

#endif
