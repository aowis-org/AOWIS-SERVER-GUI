#include "geo/geo_wgs84_ellipsoid.h"

#include <GeographicLib/Geocentric.hpp>

#include <QtMath>

#include <cmath>
#include <vector>

QVector3D GeoWgs84Ellipsoid::geodeticToEcef(double lon_deg, double lat_deg, double height_m)
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    GeographicLib::Geocentric::WGS84().Forward(lat_deg, lon_deg, height_m, x, y, z);
    return QVector3D(float(x), float(y), float(z));
}

GeoWgs84Ellipsoid::LocalFrame GeoWgs84Ellipsoid::localFrameAtGeodetic(
    double lon_deg, double lat_deg, double height_m)
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::vector<double> rotation(9, 0.0);
    GeographicLib::Geocentric::WGS84().Forward(
        lat_deg, lon_deg, height_m, x, y, z, rotation);

    LocalFrame frame;
    frame.position = QVector3D(float(x), float(y), float(z));

    // GeographicLib::Geocentric::Forward documents the rotation matrix M as
    // row-major with ecef = M * [east, north, up]^T, i.e. column j of M is
    // the ECEF direction of local unit axis j (0 = east, 1 = north, 2 = up).
    frame.east = QVector3D(float(rotation[0]), float(rotation[3]), float(rotation[6]));
    frame.north = QVector3D(float(rotation[1]), float(rotation[4]), float(rotation[7]));
    frame.up = QVector3D(float(rotation[2]), float(rotation[5]), float(rotation[8]));
    return frame;
}

bool GeoWgs84Ellipsoid::ecefToGeodetic(const QVector3D &ecef, double *lon_deg, double *lat_deg)
{
    if (lon_deg == nullptr || lat_deg == nullptr)
        return false;
    if (ecef.lengthSquared() <= 1e-6f)
        return false;

    double lat = 0.0;
    double lon = 0.0;
    double height = 0.0;
    GeographicLib::Geocentric::WGS84().Reverse(
        double(ecef.x()), double(ecef.y()), double(ecef.z()), lat, lon, height);

    *lon_deg = lon;
    *lat_deg = lat;
    return true;
}

GeoWgs84Ellipsoid::OrbitCameraBasis GeoWgs84Ellipsoid::orbitCameraBasis(
    double target_lon_deg, double target_lat_deg,
    double yaw_deg, double pitch_deg, double distance_m)
{
    const LocalFrame frame = localFrameAtGeodetic(target_lon_deg, target_lat_deg, 0.0);
    const double pitch_rad = qDegreesToRadians(pitch_deg);
    const double yaw_rad = qDegreesToRadians(yaw_deg);
    const double distance = qMax(0.0, distance_m);
    const double horizontal_distance = distance * std::cos(pitch_rad);

    // Match the existing RHI ThreeD orbit convention exactly in the local
    // ENU frame. Web Mercator's +Y points south, so ThreeD yaw 0 places the
    // eye south of the focus while screen-up points north. Expressed in ENU
    // that is:
    //   eye horizontal = +east*sin(yaw) - north*cos(yaw)
    //   screen right   = +east*cos(yaw) + north*sin(yaw)
    //
    // The old globe basis used +north at yaw 0. Numerically its pitch range
    // looked like ThreeD, but physically the camera orbited from the opposite
    // side of the focus. Tilting away from nadir therefore made the globe rise
    // toward / stand on the upper edge of the screen instead of behaving like
    // the RHI ThreeD terrain camera.
    const QVector3D horizontal_direction =
        frame.east * float(std::sin(yaw_rad)) - frame.north * float(std::cos(yaw_rad));
    const QVector3D right =
        frame.east * float(std::cos(yaw_rad)) + frame.north * float(std::sin(yaw_rad));

    OrbitCameraBasis basis;
    basis.target = frame.position;
    basis.eye = frame.position
        + frame.up * float(distance * std::sin(pitch_rad))
        + horizontal_direction * float(horizontal_distance);
    basis.forward = (basis.target - basis.eye).normalized();
    basis.right = right;
    // "right" is constructed purely from yaw, in the target's tangent
    // plane, so it is always perpendicular to "forward" (which only ever
    // combines "up" and "horizontal_direction", both of which "right" is
    // perpendicular to by construction) -- including at pitch 90 degrees,
    // where a naive up-vector-based lookAt() would degenerate. Using
    // cross(right, forward) here (rather than cross(forward, right)) is
    // deliberate: lookAt() derives its screen-right axis as cross(forward,
    // up_hint), and cross(forward, cross(right, forward)) reduces to
    // +right (by the vector triple product identity, since right and
    // forward are perpendicular and forward is unit length), which keeps
    // geographic east on screen-right. The other cross order would
    // silently mirror the globe left/right.
    basis.up = QVector3D::crossProduct(right, basis.forward).normalized();
    return basis;
}

bool GeoWgs84Ellipsoid::rayIntersection(
    const QVector3D &origin, const QVector3D &direction, QVector3D *intersection)
{
    if (intersection == nullptr)
        return false;

    // Scale ECEF space by (1/a, 1/a, 1/b) so the WGS84 ellipsoid becomes a
    // unit sphere, solve the standard ray/sphere quadratic there, then use
    // the resulting ray parameter "t" against the *original* (unscaled) ray
    // -- t is the same parametrization in both spaces since the scaling is
    // linear and applied identically to both origin and direction.
    const float inverse_a = float(1.0 / EquatorialRadiusM);
    const float inverse_b = float(1.0 / PolarRadiusM);
    const QVector3D scale(inverse_a, inverse_a, inverse_b);

    const QVector3D scaled_origin(
        origin.x() * scale.x(), origin.y() * scale.y(), origin.z() * scale.z());
    const QVector3D scaled_direction(
        direction.x() * scale.x(), direction.y() * scale.y(), direction.z() * scale.z());

    const double a_coefficient = double(QVector3D::dotProduct(scaled_direction, scaled_direction));
    if (a_coefficient <= 1e-20)
        return false;

    const double b_coefficient =
        2.0 * double(QVector3D::dotProduct(scaled_origin, scaled_direction));
    const double c_coefficient =
        double(QVector3D::dotProduct(scaled_origin, scaled_origin)) - 1.0;
    const double discriminant = b_coefficient * b_coefficient - 4.0 * a_coefficient * c_coefficient;
    if (discriminant < 0.0)
        return false;

    const double sqrt_discriminant = std::sqrt(discriminant);
    const double t_near = (-b_coefficient - sqrt_discriminant) / (2.0 * a_coefficient);
    const double t_far = (-b_coefficient + sqrt_discriminant) / (2.0 * a_coefficient);
    // Nearest intersection in front of the ray origin -- t_near first (the
    // ray entering the ellipsoid), falling back to t_far for the
    // origin-inside-the-ellipsoid case. That should not happen for an
    // orbit camera, which always sits outside the globe, but is handled
    // defensively rather than assumed away.
    const double t = t_near >= 0.0 ? t_near : t_far;
    if (t < 0.0)
        return false;

    *intersection = origin + direction * float(t);
    return true;
}
