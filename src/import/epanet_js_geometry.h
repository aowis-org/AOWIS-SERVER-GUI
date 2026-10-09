#ifndef EPANET_JS_GEOMETRY_H
#define EPANET_JS_GEOMETRY_H
#include "import/epanet_js_project_converter.h"
#include <optional>
#include <QHash>
namespace EpanetJsGeometry {
double approximateDistanceMeters(const CoordinateWGS84 &from, const CoordinateWGS84 &to);
std::optional<double> normalizedPipePosition(const QHash<QUuid, CoordinateWGS84> &node_coordinates, const HydraulicLinkPipe &pipe, const CoordinateWGS84 &snap_coordinate);
std::optional<QList<CoordinateWGS84>> linkCoordinates(const QVariantMap &row, qint64 source_id, const QString &table_name, const QString &entity_name, EpanetJsProjectConversionResult &result);
}
#endif
