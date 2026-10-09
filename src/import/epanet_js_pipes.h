#ifndef EPANET_JS_PIPES_H
#define EPANET_JS_PIPES_H
#include "import/epanet_js_project_converter.h"
namespace EpanetJsPipes {
void importPipes(const EpanetJsProjectSnapshot &project, const QJsonObject &project_settings, EpanetJsProjectConversionResult &result);
}
#endif
