#ifndef EPANET_JS_VALIDATION_H
#define EPANET_JS_VALIDATION_H

#include "import/epanet_js_project_converter.h"
#include <QJsonObject>

namespace EpanetJsValidation
{
bool validateProjectCoordinateStorage(const QJsonObject &project_settings, EpanetJsProjectConversionResult &result);
void registerEntityIds(const EpanetJsProjectSnapshot &project, EpanetJsProjectConversionResult &result);
void validateReferenceDomains(const EpanetJsProjectSnapshot &project, EpanetJsProjectConversionResult &result);
void validateLinkEndpointReferences(const EpanetJsProjectSnapshot &project, EpanetJsProjectConversionResult &result);
}
#endif // EPANET_JS_VALIDATION_H
