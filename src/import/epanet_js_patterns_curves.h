#ifndef EPANET_JS_PATTERNS_CURVES_H
#define EPANET_JS_PATTERNS_CURVES_H
#include "import/epanet_js_project_converter.h"
#include <QJsonObject>
namespace EpanetJsPatternsCurves
{
std::optional<QList<QPair<double, double>>> curvePoints(const QString &text);
void importPatterns(const EpanetJsProjectSnapshot &project, EpanetJsProjectConversionResult &result);
void importCurves(const EpanetJsProjectSnapshot &project, const QJsonObject &project_settings, EpanetJsProjectConversionResult &result);
}
#endif // EPANET_JS_PATTERNS_CURVES_H
