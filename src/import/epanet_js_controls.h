#ifndef EPANET_JS_CONTROLS_H
#define EPANET_JS_CONTROLS_H

#include "import/epanet_js_project_converter.h"

namespace EpanetJsControls
{
void importControls(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result);
}

#endif
