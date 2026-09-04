#ifndef GEO_WGS84_ELLIPSOID_H
#define GEO_WGS84_ELLIPSOID_H

#include <QPointF>
#include <QSize>
#include <QVector3D>

// Converts geodetic coordinates (longitude/latitude/height above the WGS84
// ellipsoid) into earth-centered, earth-fixed (ECEF) coordinates, for the
// "Globe" map view mode, which renders planet Earth as its actual WGS84
// ellipsoid shape rather than the flat Web Mercator plane used by the 2D/3D
// view modes. Also provides the shared orbit-camera basis, screen-ray/
// ellipsoid intersection, and ECEF -> geodetic conversion used by both the
// GPU camera (MapRhiCamera) and the CPU-side picking/panning math (MapModel)
// -- mirroring how the 2D/3D view already keeps its own screen<->geo
// conversions in MapModel independent of the GPU camera (see
// MapModel::wgs84FromScreen()/screenFromWgs84()).
//
// The geodetic <-> ECEF conversions themselves are delegated to
// GeographicLib::Geocentric, which is already vendored and linked into this
// project (see geo_metric_projection.h for the existing UTM use of
// GeographicLib). Hand-rolling those formulas here would just be a
// worse-tested reimplementation of what GeographicLib already provides,
// including the local east/north/up frame used by the globe orbit camera
// below.
class GeoWgs84Ellipsoid
{
public:
    GeoWgs84Ellipsoid() = delete;

    // WGS84 semi-major axis (equatorial radius) and semi-minor axis (polar
    // radius), in meters. EquatorialRadiusM is used for camera framing
    // (near/far clip planes, default orbit distance); both are used
    // together by rayIntersection() below, since that is a genuine
    // ellipsoid (not sphere) intersection. The actual vertex geometry
    // always goes through GeographicLib, which carries the full ellipsoid
    // itself.
    static constexpr double EquatorialRadiusM = 6378137.0;
    static constexpr double FlatteningInverse = 298.257223563;
    static constexpr double PolarRadiusM =
        EquatorialRadiusM * (1.0 - 1.0 / FlatteningInverse);

    // A point on (or above) the ellipsoid together with the local
    // east/north/up unit basis at that point, all expressed in ECEF meters.
    // "position" is the ECEF coordinate itself; "east"/"north"/"up" are unit
    // directions, not positions.
    struct LocalFrame
    {
        QVector3D position;
        QVector3D east;
        QVector3D north;
        QVector3D up;
    };

    // Eye/target/basis of an orbit camera: "distance_m" away from the
    // geodetic (target_lon_deg, target_lat_deg) point, offset by yaw_deg
    // (heading around that point's local up axis) and pitch_deg (tilt up
    // from the local horizon), matching the same yaw/pitch/distance orbit
    // shape as the existing ThreeD camera, just anchored to the ellipsoid's
    // local frame instead of the flat Web Mercator plane. Shared by
    // MapRhiCamera (GPU view/projection matrix) and MapModel (CPU-side
    // screen-ray picking for click-drag panning), so the two stay in sync
    // by construction instead of by keeping two hand-written copies of this
    // math in agreement.
    struct OrbitCameraBasis
    {
        QVector3D eye;
        QVector3D target;
        QVector3D forward;
        QVector3D right;
        QVector3D up;
    };

    // Converts geodetic coordinates to ECEF meters. height_m is height above
    // the WGS84 ellipsoid (0 for a point on the surface).
    static QVector3D geodeticToEcef(double lon_deg, double lat_deg, double height_m);

    // Same conversion, but also returns the local east/north/up frame at
    // that point.
    static LocalFrame localFrameAtGeodetic(double lon_deg, double lat_deg, double height_m);

    // Inverse of geodeticToEcef(): finds the geodetic longitude/latitude of
    // the ellipsoid point nearest to an arbitrary ECEF position. height_m is
    // optional -- most callers only ever want "which lon/lat is this point
    // over" and pass nullptr, but terrain-aware globe picking (bisecting a
    // ray against actual DEM relief rather than the bare ellipsoid) needs
    // the point's true height above the ellipsoid too, so it is available
    // as a byproduct of the same GeographicLib reverse conversion rather
    // than a second, separate computation. Always succeeds for any non-zero
    // input; only returns false for the degenerate (0,0,0) case.
    static bool ecefToGeodetic(
        const QVector3D &ecef, double *lon_deg, double *lat_deg, double *height_m = nullptr);

    // Builds the orbit camera basis described above. pitch_deg is expected
    // to already be clamped by the caller (this function does not know
    // MapModel's Min/MaxViewGlobePitchDeg bounds, to keep this header free
    // of a dependency on map/core). target_height_m lifts the target (and
    // therefore the whole eye/target rig) that far above the ellipsoid
    // along the target's local "up" -- 0 for the bare sea-level ellipsoid
    // surface, or the real DEM elevation at (target_lon_deg, target_lat_deg)
    // so the orbit pivot sits on the actual visible terrain instead of
    // floating above/clipping through it near mountains.
    static OrbitCameraBasis orbitCameraBasis(
        double target_lon_deg, double target_lat_deg,
        double yaw_deg, double pitch_deg, double distance_m,
        double target_height_m = 0.0);

    // Nearest intersection of the ray (origin + t*direction, t >= 0) with
    // the WGS84 ellipsoid. direction need not be normalized. Returns false
    // if the ray misses the ellipsoid entirely or points away from it --
    // callers (screen-ray picking near the globe's horizon/limb) must
    // handle that as "no pick" rather than assuming a hit.
    static bool rayIntersection(
        const QVector3D &origin, const QVector3D &direction, QVector3D *intersection);

    // Maps a screen position to a world-space ray through the given orbit
    // camera basis (same vertical_fov_deg/viewport convention as the GPU
    // perspective projection built from that same basis). This is the one
    // place the NDC -> ray formula lives; MapRhiCamera's GPU-side globe ray
    // and MapModel's CPU-side globe picking/panning each build their own
    // OrbitCameraBasis (they know different things: synced camera state vs.
    // model state) but both route the actual ray math through here instead
    // of keeping their own copies of the same formula. MapRhiGlobeRenderer's
    // visible-tile-window sampling still has its own, pre-existing copy of
    // this same math (kept as-is here to avoid touching that tuned, hot
    // path); routing it through this function too is a reasonable follow-up.
    static bool screenRay(
        const OrbitCameraBasis &basis, const QPointF &screen_position,
        const QSize &viewport, double vertical_fov_deg,
        QVector3D *eye, QVector3D *direction);
};

#endif // GEO_WGS84_ELLIPSOID_H
