#include "import/epanet_js_geometry.h"
#include "import/epanet_js_conversion_common.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <limits>
namespace EpanetJsGeometry {
using namespace EpanetJsConversionCommon;
double approximateDistanceMeters(
    const CoordinateWGS84 &from,
    const CoordinateWGS84 &to);

std::optional<double> normalizedPipePosition(
    const QHash<QUuid, CoordinateWGS84> &node_coordinates,
    const HydraulicLinkPipe &pipe,
    const CoordinateWGS84 &snap_coordinate)
{
    const auto from_coordinate = node_coordinates.constFind(pipe.node_uuid_from);
    const auto to_coordinate = node_coordinates.constFind(pipe.node_uuid_to);
    if (from_coordinate == node_coordinates.cend() || to_coordinate == node_coordinates.cend())
        return std::nullopt;

    QList<CoordinateWGS84> coordinates;
    coordinates.reserve(pipe.vertices.size() + 2);
    coordinates.append(from_coordinate.value());
    for (const HydraulicLinkVertex &vertex : pipe.vertices)
        coordinates.append(vertex.coordinate_wgs84);
    coordinates.append(to_coordinate.value());
    if (coordinates.size() < 2)
        return std::nullopt;

    double total_length_m = 0.0;
    QList<double> segment_lengths_m;
    segment_lengths_m.reserve(coordinates.size() - 1);
    for (qsizetype index = 1; index < coordinates.size(); ++index)
    {
        const double segment_length_m = approximateDistanceMeters(
            coordinates.at(index - 1), coordinates.at(index));
        segment_lengths_m.append(segment_length_m);
        total_length_m += segment_length_m;
    }
    if (!std::isfinite(total_length_m) || total_length_m <= 0.0)
        return std::nullopt;

    constexpr double degrees_to_radians = 0.017453292519943295769236907684886;
    const double longitude_scale = std::max(
        1.0e-12,
        std::abs(std::cos(snap_coordinate.latitude_deg * degrees_to_radians)));

    double best_distance_squared = std::numeric_limits<double>::infinity();
    double best_distance_along_m = 0.0;
    double cumulative_length_m = 0.0;
    for (qsizetype index = 1; index < coordinates.size(); ++index)
    {
        const CoordinateWGS84 &start = coordinates.at(index - 1);
        const CoordinateWGS84 &end = coordinates.at(index);

        const double start_x =
            (start.longitude_deg - snap_coordinate.longitude_deg) * longitude_scale;
        const double start_y = start.latitude_deg - snap_coordinate.latitude_deg;
        const double end_x =
            (end.longitude_deg - snap_coordinate.longitude_deg) * longitude_scale;
        const double end_y = end.latitude_deg - snap_coordinate.latitude_deg;
        const double delta_x = end_x - start_x;
        const double delta_y = end_y - start_y;
        const double segment_length_squared = delta_x * delta_x + delta_y * delta_y;

        double segment_fraction = 0.0;
        if (segment_length_squared > 0.0)
        {
            segment_fraction = std::clamp(
                -(start_x * delta_x + start_y * delta_y) / segment_length_squared,
                0.0,
                1.0);
        }

        const double projected_x = start_x + segment_fraction * delta_x;
        const double projected_y = start_y + segment_fraction * delta_y;
        const double distance_squared = projected_x * projected_x + projected_y * projected_y;
        if (distance_squared < best_distance_squared)
        {
            best_distance_squared = distance_squared;
            best_distance_along_m = cumulative_length_m
                + segment_fraction * segment_lengths_m.at(index - 1);
        }
        cumulative_length_m += segment_lengths_m.at(index - 1);
    }

    return std::clamp(best_distance_along_m / total_length_m, 0.0, 1.0);
}

double approximateDistanceMeters(
    const CoordinateWGS84 &from,
    const CoordinateWGS84 &to)
{
    constexpr double earth_radius_m = 6371008.8;
    constexpr double degrees_to_radians = 0.017453292519943295769236907684886;
    const double latitude_from = from.latitude_deg * degrees_to_radians;
    const double latitude_to = to.latitude_deg * degrees_to_radians;
    const double delta_latitude = (to.latitude_deg - from.latitude_deg) * degrees_to_radians;
    const double delta_longitude = (to.longitude_deg - from.longitude_deg) * degrees_to_radians;
    const double sine_latitude = std::sin(delta_latitude * 0.5);
    const double sine_longitude = std::sin(delta_longitude * 0.5);
    const double haversine = sine_latitude * sine_latitude
        + std::cos(latitude_from) * std::cos(latitude_to)
            * sine_longitude * sine_longitude;
    const double bounded = std::max(0.0, std::min(1.0, haversine));
    return 2.0 * earth_radius_m * std::asin(std::sqrt(bounded));
}

std::optional<QList<CoordinateWGS84>> linkCoordinates(
    const QVariantMap &row,
    qint64 source_id,
    const QString &table_name,
    const QString &entity_name,
    EpanetJsProjectConversionResult &result)
{
    if (!row.contains(QStringLiteral("coords")) || row.value(QStringLiteral("coords")).isNull())
        return QList<CoordinateWGS84>();

    const QString text = row.value(QStringLiteral("coords")).toString().trimmed();
    if (text.isEmpty())
        return QList<CoordinateWGS84>();

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isArray())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-%1-coordinates").arg(entity_name),
            QStringLiteral("epanet-js %1 id %2 has invalid coords JSON.").arg(entity_name).arg(source_id),
            table_name,
            source_id);
        return std::nullopt;
    }

    const QJsonArray points = document.array();
    if (points.size() < 2)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-%1-coordinates").arg(entity_name),
            QStringLiteral("epanet-js %1 id %2 has fewer than two coordinates.").arg(entity_name).arg(source_id),
            table_name,
            source_id);
        return std::nullopt;
    }

    QList<CoordinateWGS84> coordinates;
    for (const QJsonValue &point_value : points)
    {
        double longitude = 0.0;
        double latitude = 0.0;
        bool valid = false;

        if (point_value.isArray())
        {
            const QJsonArray point = point_value.toArray();
            const std::optional<double> x = point.size() >= 2 ? finiteDouble(point.at(0)) : std::nullopt;
            const std::optional<double> y = point.size() >= 2 ? finiteDouble(point.at(1)) : std::nullopt;
            if (x.has_value() && y.has_value())
            {
                longitude = *x;
                latitude = *y;
                valid = true;
            }
        }
        else if (point_value.isObject())
        {
            const QJsonObject point = point_value.toObject();
            const QJsonValue longitude_value = point.contains(QStringLiteral("longitude"))
                ? point.value(QStringLiteral("longitude"))
                : point.contains(QStringLiteral("lon"))
                    ? point.value(QStringLiteral("lon"))
                    : point.value(QStringLiteral("x"));
            const QJsonValue latitude_value = point.contains(QStringLiteral("latitude"))
                ? point.value(QStringLiteral("latitude"))
                : point.contains(QStringLiteral("lat"))
                    ? point.value(QStringLiteral("lat"))
                    : point.value(QStringLiteral("y"));
            const std::optional<double> x = finiteDouble(longitude_value);
            const std::optional<double> y = finiteDouble(latitude_value);
            if (x.has_value() && y.has_value())
            {
                longitude = *x;
                latitude = *y;
                valid = true;
            }
        }

        if (!valid || longitude < -180.0 || longitude > 180.0
            || latitude < -90.0 || latitude > 90.0)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-%1-coordinates").arg(entity_name),
                QStringLiteral("epanet-js %1 id %2 contains an invalid WGS84 coordinate.").arg(entity_name).arg(source_id),
                table_name,
                source_id);
            return std::nullopt;
        }

        CoordinateWGS84 coordinate;
        coordinate.longitude_deg = longitude;
        coordinate.latitude_deg = latitude;
        coordinates.append(coordinate);
    }

    return coordinates;
}


}
