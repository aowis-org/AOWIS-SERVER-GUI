#ifndef EPANET_JS_PUMPS_VALVES_H
#define EPANET_JS_PUMPS_VALVES_H

#include "import/epanet_js_project_converter.h"

namespace EpanetJsPumpsValves
{
void importPumpsAndValves(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result);
}

#endif
