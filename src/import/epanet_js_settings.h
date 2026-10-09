#ifndef EPANET_JS_SETTINGS_H
#define EPANET_JS_SETTINGS_H
#include "import/epanet_js_project_converter.h"
#include <QJsonObject>
namespace EpanetJsSettings
{
QJsonObject simulationSettingsObject(const EpanetJsProjectSnapshot &project, EpanetJsProjectConversionResult &result);
void mapHeadlossFormula(const QJsonObject &project_settings, EpanetJsProjectConversionResult &result);
void importPipeMaterials(const EpanetJsProjectSnapshot &project, const QJsonObject &project_settings, EpanetJsProjectConversionResult &result);
void mapSimulationSettings(const QJsonObject &settings, const QJsonObject &project_settings, EpanetJsProjectConversionResult &result);
void mapQualitySimulationSettings(const QJsonObject &settings, const QJsonObject &project_settings, EpanetJsProjectConversionResult &result);
}
#endif // EPANET_JS_SETTINGS_H
