#ifndef EPANET_JS_WATER_QUALITY_H
#define EPANET_JS_WATER_QUALITY_H

#include "import/epanet_js_project_converter.h"

namespace EpanetJsWaterQuality
{
void importWaterQualityEntityData(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    const QJsonObject &simulation_settings,
    EpanetJsProjectConversionResult &result);
}

#endif // EPANET_JS_WATER_QUALITY_H
