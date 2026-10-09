#ifndef EPANET_JS_NODES_H
#define EPANET_JS_NODES_H

#include "import/epanet_js_project_converter.h"

namespace EpanetJsNodes
{
void importNodes(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result);

void importJunctionDemands(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result);
void applyEmitterSettings(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    const QJsonObject &simulation_settings,
    EpanetJsProjectConversionResult &result);

}

#endif // EPANET_JS_NODES_H
