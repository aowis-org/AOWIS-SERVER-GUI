#include "map/rhi/map_rhi_globe_renderer.h"

#include "map/core/map_model.h"
#include "map/data/map_tile_repository.h"
#include "map/data/map_terrain_repository.h"
#include "map/data/map_terrain_tile.h"
#include "map/rhi/map_rhi_terrain_mesh_scheduler.h"
#include "config/gui_configuration.h"
#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <QDebug>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QSet>
#include <QtMath>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace
{
// Highest imagery zoom the globe will ever request. Matches MapModel::MaxZoom
// (19) exactly, since MapModel::MinViewGlobeDistanceM is itself pinned to
// zoom 19 via viewGlobeDistanceMForZoomLevel() -- the globe's maximum zoom-in
// should reach exactly as much detail as 2D/3D ever do, no more, no less.
constexpr int GlobeImageryMaxZoom = MapModel::MaxZoom;
constexpr int GlobeTerrainReliefMinimumZoom = 8;
// Keep the same terrain LOD policy as the flat RHI 3D renderer: powers of
// two from one cell up to the DEM-native density, with camera-driven rebuilds
// rate-limited so continuous orbit/zoom never resamples the retained apron at
// vsync frequency.
constexpr int GlobeTerrainMinimumLodCellCount = 1;
constexpr int GlobeAsyncTerrainMeshMinimumCellCount = 16;
constexpr qint64 GlobeMinimumTerrainLodRebuildIntervalMs = 120;
// Longitude segments used for each polar cap fan. Independent of the
// imagery tile grid -- a small seam between the imagery tiles' edge at
// +-85.05 degrees and the cap fan's ring is not visually significant at
// whole-globe viewing distance.
constexpr int GlobePolarCapSegments = 48;
// Flat fallback color for the polar caps (area above/below Web Mercator's
// +-85.05 degree limit, which basemap tiles never cover). A light,
// ice/cloud-like color reads reasonably for both poles without pretending
// to be real imagery.
const QColor GlobePolarCapColor(235, 240, 245);
// Missing imagery must never punch transparent/black holes through the planet
// while requests are still arriving. This low-contrast ocean-like fallback is
// only visible until the real tile texture is uploaded.
const QColor GlobeMissingTileColor(18, 58, 72);

constexpr int GlobeCameraUniformBytes = 16 * int(sizeof(float));

// Vertex grid subdivisions per tile edge, by zoom level. Low zoom tiles
// span a huge angular area (a zoom-0 tile is the entire planet, a zoom-1
// tile is a full hemisphere) and need heavy subdivision for the ellipsoid
// curvature to look smooth -- 8 subdivisions across an entire 360-degree
// tile is only 45 degrees per facet, which renders as a visibly faceted
// polyhedron rather than a sphere. By the time tiles are a few degrees
// across or smaller, the curvature within a single tile is negligible and
// a coarse grid is indistinguishable from a fine one while costing far
// less geometry across a whole tile window. Unlike the old single-zoom
// window, the quadtree walk (selectVisibleGlobeQuadtreeLeaves() below) can
// keep a zoom 0-3 leaf alive whenever the camera is far enough out that a
// large fraction of the planet projects to under the subdivide threshold at
// once (the same "zoomed all the way out" case the old code's
// full-coverage special case handled), so this remains an occasional rather
// than a hot-path case, but it is reached through the ordinary walk now
// instead of a special-cased branch.
int subdivisionsForZoom(int zoom)
{
    if (zoom <= 0)
        return 32;
    if (zoom <= 1)
        return 24;
    if (zoom <= 3)
        return 12;
    if (zoom <= 6)
        return 4;
    return 2;
}

// Replaces the old single-zoom "sample the projected ellipsoid boundary into
// one rectangular tile window" approach. That approach picked one imagery
// zoom for the whole screen from camera distance alone, then found the
// tile-index rectangle covering everything from the near-camera ground out
// to the visible limb at *that* zoom. The moment a tilt brought the horizon
// into view, the limb sample landed thousands of kilometres from the
// near-camera point (Earth's curvature), so covering that whole span at
// near-camera resolution meant the tile-index rectangle could balloon to
// the entire zoom level's tile grid -- millions of tiles at typical in-close
// zooms. A single zoom level simply cannot cover both a near-camera patch
// and a near-limb region economically at once.
//
// A quadtree fixes this the way real terrain/globe engines (FlightGear's
// VirtualPlanetBuilder-based scenery, Cesium, etc.) do: walk the tile
// hierarchy from the whole-planet root and let each node decide for itself,
// from its own real-world size and its own real distance to the camera,
// whether it still needs to subdivide. Near the camera that bottoms out at
// many small, fine tiles; near the horizon it bottoms out at a handful of
// large, coarse tiles automatically, because a coarse tile's on-screen size
// near the limb is small even though its real-world footprint is huge. Total
// leaf count stays bounded by the walk's own occlusion/frustum culling and a
// hard visited-node cap, regardless of pitch.
constexpr int GlobeQuadtreeRootZoom = 0;
// Subdivide a node once its own on-screen projected size exceeds this many
// pixels; merge (stop subdividing) once it falls below the lower
// GlobeQuadtreeMergeScreenPx bound instead of the same value, so a node
// sitting right at the boundary does not flip between one leaf and four
// children every frame as the camera drifts by sub-pixel amounts (the same
// role GlobeZoomHysteresis played for the old single-zoom picker, applied
// per node instead of once globally).
constexpr double GlobeQuadtreeSubdivideScreenPx = 320.0;
constexpr double GlobeQuadtreeMergeScreenPx = 160.0;
// Hard ceilings so a pathological view (camera exactly edge-on to the
// ellipsoid, or a bug in the culling above) degrades gracefully instead of
// pathologically. In normal operation, horizon/frustum culling keeps the
// walk to at most a few hundred visited nodes.
constexpr int GlobeQuadtreeMaxVisitedNodes = 20000;
constexpr int GlobeQuadtreeMaxLeaves = 3000;
// Extra half-angle of slack added to the camera's field of view when
// culling a node against the view cone. This is a coarse "is this node even
// worth walking into" cull, not exact clipping -- the per-tile draw already
// only ever draws leaves that were kept -- so generous slack is cheap
// insurance against ever dropping a node that is genuinely partly on
// screen.
//
// Deliberately generous well beyond that minimum, for a second reason: this
// is also the *only* lever that gives imagery/terrain a head start on
// loading before a tile becomes strictly visible (requestMissingTiles()
// requests every tile in window_tiles, which this cull directly gates). Too
// tight a margin here means a tile only starts its network fetch once it is
// already on screen, so panning reveals a visible gap or the flat
// GlobeMissingTileColor placeholder for as long as that fetch takes --
// exactly the popping-in this margin is sized to hide. GlobeQuadtreeMaxLeaves
// (3000) and GlobeQuadtreeMaxVisitedNodes (20000) both have comfortable
// headroom above what this widened margin adds in ordinary framing; an
// unusually wide, low-pitch view near the horizon is the case most likely
// to feel that budget pressure first.
constexpr double GlobeQuadtreeViewConeMarginRad = 0.5;

quint64 globeQuadtreeNodeKey(int zoom, int tile_x, int tile_y)
{
    return (quint64(quint32(zoom)) << 48)
        | (quint64(quint32(tile_x)) << 24)
        | quint64(quint32(tile_y));
}

// Cheap bounding sphere for a tile: ECEF positions of its four corners plus
// centre, centroid as the sphere centre, farthest sample as the radius.
//
// This corner-sampling approach breaks down for wide tiles: at zoom 0 the
// tile spans the full 360 degrees of longitude, so lon0 (-180) and lon1
// (+180) are the *same* ECEF point, collapsing 4 of the 5 samples into just
// 2 distinct locations -- both on one side of the planet. The resulting
// "bounding sphere" ends up skewed off-centre rather than actually bounding
// the tile, and whether that skewed sphere happens to still overlap the
// camera's view direction becomes a matter of which way the camera happens
// to be facing -- exactly the kind of direction-dependent, intermittent
// failure that showed up during panning. Below zoom 2 a tile still spans a
// full hemisphere or more, where the same corner-averaging approach is
// similarly unreliable even without an exact point collapse. For zoom 0-1,
// skip the sampling and use the planet's own bounding sphere instead: it is
// trivially correct (everything is inside it) and there are at most four
// such wide tiles in existence, so tightness does not matter here the way
// it does for the many small tiles deeper in the tree.
void globeQuadtreeNodeBoundingSphere(
    int zoom, int tile_x, int tile_y, QVector3D *center, double *radius_m)
{
    if (zoom <= 1)
    {
        *center = QVector3D(0.0f, 0.0f, 0.0f);
        *radius_m = GeoWgs84Ellipsoid::EquatorialRadiusM;
        return;
    }

    const double lon0 = GeoWebMercator::tileXToLon(double(tile_x), zoom);
    const double lon1 = GeoWebMercator::tileXToLon(double(tile_x + 1), zoom);
    const double lat0 = GeoWebMercator::tileYToLat(double(tile_y), zoom);
    const double lat1 = GeoWebMercator::tileYToLat(double(tile_y + 1), zoom);
    const double lon_mid = 0.5 * (lon0 + lon1);
    const double lat_mid = 0.5 * (lat0 + lat1);

    const QVector3D samples[5] = {
        GeoWgs84Ellipsoid::geodeticToEcef(lon0, lat0, 0.0),
        GeoWgs84Ellipsoid::geodeticToEcef(lon1, lat0, 0.0),
        GeoWgs84Ellipsoid::geodeticToEcef(lon0, lat1, 0.0),
        GeoWgs84Ellipsoid::geodeticToEcef(lon1, lat1, 0.0),
        GeoWgs84Ellipsoid::geodeticToEcef(lon_mid, lat_mid, 0.0),
    };

    constexpr int SampleCount = sizeof(samples) / sizeof(samples[0]);
    QVector3D centroid(0.0f, 0.0f, 0.0f);
    for (const QVector3D &sample : samples)
        centroid += sample;
    centroid /= float(SampleCount);

    double max_distance = 0.0;
    for (const QVector3D &sample : samples)
        max_distance = qMax(max_distance, double((sample - centroid).length()));

    *center = centroid;
    *radius_m = max_distance;
}

// A tile's bounding sphere is centered on the tile and sized to its
// diagonal, but the actual tile geometry is a curved quad following the
// ellipsoid surface -- for a wide, coarse (low-zoom) tile viewed nearly
// edge-on at the visible limb, the quad's own corners can reach noticeably
// farther around the curve toward the camera than a sphere of that radius
// would suggest. globeQuadtreeNodeOccludedByHorizon() only has the sphere
// to work with, so undercounting this by using node_radius_m directly (as
// an earlier version of this function did) culls tiles that are genuinely
// still partly visible right at the horizon -- worse, and asymmetrically,
// the more oblique the camera's angle to that part of the limb. This
// doesn't need to be exact, only generous: the true occlusion case (a tile
// on the planet's far side) is occluded by many tile-radii, not a
// borderline amount, so a generous multiplier here only affects tiles
// genuinely near the grazing edge.
constexpr double GlobeQuadtreeHorizonOcclusionMarginFactor = 3.0;

// True if the straight line from eye to node_center is blocked by the
// planet itself -- i.e. the node is entirely hidden behind the visible
// limb/horizon, not merely far away.
//
// This deliberately does NOT solve eye.dot(eye) - R^2 = 0 directly: eye and
// node_center are ECEF meters at ~6.4e6 magnitude stored in QVector3D,
// which is float32. eye.dot(eye) and R^2 are then both ~4e13, and float32's
// precision floor at that magnitude is on the order of a few million --
// comparable to or larger than the actual 2*R*h signal once the camera's
// height h above the surface drops to a few metres (exactly the case at
// close-in globe zoom, especially combined with a low pitch looking toward
// the horizon). The quadratic below would then misfire and could cull the
// ground directly under the camera, taking the entire quadtree subtree
// under it with it -- a real, previously-hit failure mode, not a
// theoretical one.
//
// Instead this reuses GeoWgs84Ellipsoid::rayIntersection() -- the same
// ellipsoid-intersection routine already proven at the horizon/limb by the
// existing screen-ray picking code -- against the real WGS84 ellipsoid
// rather than a hand-rolled sphere approximation: cast the ray from eye
// toward node_center and compare its ellipsoid intersection distance to the
// distance to node_center itself. If the ellipsoid blocks the ray
// meaningfully closer than the node, something nearer (the planet's own
// bulge) is in the way.
bool globeQuadtreeNodeOccludedByHorizon(
    const QVector3D &node_center, double node_radius_m, const QVector3D &eye)
{
    const QVector3D to_node = node_center - eye;
    const double distance_to_node = double(to_node.length());
    // The eye is at or inside the node's own bounding sphere -- nothing to
    // occlude (also guards the degenerate zero-length direction below).
    if (distance_to_node <= qMax(1.0, node_radius_m))
        return false;

    QVector3D direction = to_node;
    direction.normalize();
    QVector3D intersection;
    if (!GeoWgs84Ellipsoid::rayIntersection(eye, direction, &intersection))
        return false; // ray never touches the ellipsoid -- cannot be occluded by it.

    const double distance_to_surface = double((intersection - eye).length());
    // The node itself sits at (or essentially at) the ellipsoid surface, so
    // when it is the visible point in this direction the ray's own
    // intersection distance should land within about the node's own size
    // of distance_to_node. Only flag occlusion when the ellipsoid blocks the
    // ray meaningfully closer than that -- i.e. something nearer than the
    // node itself, by more than the node's own radius (times a generous
    // safety factor -- see GlobeQuadtreeHorizonOcclusionMarginFactor's
    // comment), is in the way.
    return distance_to_surface
        < distance_to_node - qMax(1.0, node_radius_m * GlobeQuadtreeHorizonOcclusionMarginFactor);
}

// Coarse "is this node even pointed at" cull: the half-angle from the
// camera's forward axis to the node, compared against the camera's own
// half field of view plus the node's own angular radius plus a fixed
// margin. A node entirely behind the eye is only kept if it is large enough
// that a child of it might still wrap into view (only matters for the
// zoom-0/1 root nodes at the very start of the walk).
bool globeQuadtreeNodeInViewCone(
    const QVector3D &node_center, double node_radius_m,
    const GeoWgs84Ellipsoid::OrbitCameraBasis &camera_basis, double half_fov_rad)
{
    const QVector3D to_node = node_center - camera_basis.eye;
    const double distance = double(to_node.length());
    if (distance <= 1e-6)
        return true;

    const double forward_component = double(
        QVector3D::dotProduct(to_node, camera_basis.forward));
    if (forward_component <= 0.0)
        return node_radius_m > distance;

    const double angular_radius_rad = std::atan2(node_radius_m, distance);
    const double view_angle_rad = std::acos(
        qBound(-1.0, forward_component / distance, 1.0));
    return view_angle_rad
        <= half_fov_rad + angular_radius_rad + GlobeQuadtreeViewConeMarginRad;
}

// Apparent on-screen size (diameter, in pixels) of a node's bounding sphere
// -- the same "would this still look coarse on screen" question
// terrainCellCountForTile() already asks per-tile for mesh density, just
// asked here of the tile/zoom choice itself.
double globeQuadtreeNodeProjectedSizePx(
    const QVector3D &node_center, double node_radius_m,
    const GeoWgs84Ellipsoid::OrbitCameraBasis &camera_basis,
    double viewport_height_px, double tan_half_fov)
{
    const double distance_m = qMax(
        1.0, double((node_center - camera_basis.eye).length()) - node_radius_m);
    const double angular_diameter = 2.0 * node_radius_m / distance_m;
    return angular_diameter * (viewport_height_px / (2.0 * tan_half_fov));
}

// Below this zoom, tile bounding spheres are at their widest and least
// precise (see globeQuadtreeNodeBoundingSphere()), which makes the view-cone
// test's own "large object, is it behind the eye" fallback unreliable right
// where it matters least: there are at most 16 nodes total at zoom 0-1, so
// visiting all of them unconditionally and relying solely on the (ellipsoid
// ray-intersection based, precision-robust) horizon-occlusion test to prune
// them is negligible extra cost for meaningfully more robust culling.
constexpr int GlobeQuadtreeViewConeCullMinZoom = 2;

void collectGlobeQuadtreeLeaves(
    int zoom, int tile_x, int tile_y,
    const GeoWgs84Ellipsoid::OrbitCameraBasis &camera_basis,
    double viewport_height_px, double tan_half_fov, double half_fov_rad,
    const QSet<quint64> &previously_subdivided_nodes,
    QSet<quint64> *currently_subdivided_nodes,
    QVector<MapRhiGlobeQuadtreeLeaf> *leaves, int *visit_budget)
{
    if (leaves == nullptr || visit_budget == nullptr
        || *visit_budget <= 0 || leaves->size() >= GlobeQuadtreeMaxLeaves)
    {
        return;
    }
    --(*visit_budget);

    QVector3D node_center;
    double node_radius_m = 0.0;
    globeQuadtreeNodeBoundingSphere(zoom, tile_x, tile_y, &node_center, &node_radius_m);

    if (globeQuadtreeNodeOccludedByHorizon(node_center, node_radius_m, camera_basis.eye))
        return;
    if (zoom >= GlobeQuadtreeViewConeCullMinZoom
        && !globeQuadtreeNodeInViewCone(node_center, node_radius_m, camera_basis, half_fov_rad))
    {
        return;
    }

    const double projected_size_px = globeQuadtreeNodeProjectedSizePx(
        node_center, node_radius_m, camera_basis, viewport_height_px, tan_half_fov);
    const bool was_subdivided = previously_subdivided_nodes.contains(
        globeQuadtreeNodeKey(zoom, tile_x, tile_y));
    const double subdivide_threshold_px =
        was_subdivided ? GlobeQuadtreeMergeScreenPx : GlobeQuadtreeSubdivideScreenPx;
    const bool can_subdivide = zoom < GlobeImageryMaxZoom;

    if (!can_subdivide || projected_size_px <= subdivide_threshold_px)
    {
        leaves->append(MapRhiGlobeQuadtreeLeaf{zoom, tile_x, tile_y});
        return;
    }

    if (currently_subdivided_nodes != nullptr)
        currently_subdivided_nodes->insert(globeQuadtreeNodeKey(zoom, tile_x, tile_y));

    // Visit the child closest to the camera first. Harmless when the visit
    // budget never comes under pressure (the normal case), but if it ever
    // does, whatever gets dropped by running out of budget should be the
    // least camera-relevant remaining branch, not whichever one happened to
    // sit first in raster (dx, dy) order.
    const int child_zoom = zoom + 1;
    const int child_tile_span = 1 << child_zoom;
    const int child_x = tile_x * 2;
    const int child_y = tile_y * 2;
    struct Child
    {
        int x = 0;
        int y = 0;
        double distance_sq = 0.0;
    };
    Child children[4];
    int child_count = 0;
    for (int dx = 0; dx < 2; ++dx)
    {
        for (int dy = 0; dy < 2; ++dy)
        {
            const int cy = child_y + dy;
            if (cy < 0 || cy >= child_tile_span)
                continue;

            QVector3D child_center;
            double child_radius_m = 0.0;
            globeQuadtreeNodeBoundingSphere(
                child_zoom, child_x + dx, cy, &child_center, &child_radius_m);
            children[child_count] = Child{
                child_x + dx, cy,
                double((child_center - camera_basis.eye).lengthSquared())};
            ++child_count;
        }
    }
    std::sort(
        children, children + child_count,
        [](const Child &first, const Child &second)
    {
        return first.distance_sq < second.distance_sq;
    });

    for (int index = 0; index < child_count; ++index)
    {
        collectGlobeQuadtreeLeaves(
            child_zoom, children[index].x, children[index].y, camera_basis,
            viewport_height_px, tan_half_fov, half_fov_rad, previously_subdivided_nodes,
            currently_subdivided_nodes, leaves, visit_budget);
    }
}

// Top-level entry point: walks the quadtree from the single whole-planet
// root tile (zoom 0 is the entire world in Web Mercator X and, above/below
// the +-85.05 degree limit, its polar caps -- see the class comment) and
// returns the resulting leaves. previously_subdivided_nodes is both read
// (for hysteresis, see collectGlobeQuadtreeLeaves()) and overwritten with
// the new set of subdivided nodes for next frame's call.
QVector<MapRhiGlobeQuadtreeLeaf> selectVisibleGlobeQuadtreeLeaves(
    const MapModel &map_model, const QSize &viewport_size,
    QSet<quint64> *previously_subdivided_nodes)
{
    QVector<MapRhiGlobeQuadtreeLeaf> leaves;
    if (!viewport_size.isValid() || previously_subdivided_nodes == nullptr)
        return leaves;

    const GeoWgs84Ellipsoid::OrbitCameraBasis camera_basis =
        GeoWgs84Ellipsoid::orbitCameraBasis(
            map_model.centerLon(), map_model.centerLat(),
            map_model.viewGlobeYawDeg(),
            qBound(MapModel::MinViewGlobePitchDeg, map_model.viewGlobePitchDeg(),
                   MapModel::MaxViewGlobePitchDeg),
            qMax(MapModel::MinViewGlobeDistanceM, map_model.viewGlobeDistanceM()));
    const double viewport_height_px = double(qMax(1, viewport_size.height()));
    const double half_fov_rad = qDegreesToRadians(MapModel::GlobeFieldOfViewDeg * 0.5);
    const double tan_half_fov = std::tan(half_fov_rad);

    QSet<quint64> currently_subdivided_nodes;
    int visit_budget = GlobeQuadtreeMaxVisitedNodes;
    collectGlobeQuadtreeLeaves(
        GlobeQuadtreeRootZoom, 0, 0, camera_basis, viewport_height_px, tan_half_fov,
        half_fov_rad, *previously_subdivided_nodes, &currently_subdivided_nodes,
        &leaves, &visit_budget);

    *previously_subdivided_nodes = std::move(currently_subdivided_nodes);

    // Defense in depth: a working horizon/frustum cull should never leave
    // this empty while the camera is anywhere near the planet (the root
    // alone, uncontested, always qualifies as at least one leaf). If a
    // future bug in the culling above ever does produce zero leaves, fall
    // back to a single tile under the current target rather than rendering
    // nothing -- "wrong LOD for one frame" is a far cheaper failure mode
    // than a fully black globe.
    if (leaves.isEmpty())
    {
        const int fallback_zoom = qBound(
            0,
            int(std::lround(MapModel::viewGlobeZoomLevelForDistanceM(
                qMax(1.0, map_model.viewGlobeDistanceM()), map_model.centerLat(),
                int(viewport_height_px)))),
            GlobeImageryMaxZoom);
        const int fallback_tile_span = 1 << fallback_zoom;
        const int fallback_x = qBound(
            0,
            int(std::floor(GeoWebMercator::lonToTileX(
                GeoWebMercator::normalizeLongitude(map_model.centerLon()), fallback_zoom))),
            fallback_tile_span - 1);
        const int fallback_y = qBound(
            0,
            int(std::floor(GeoWebMercator::latToTileY(map_model.centerLat(), fallback_zoom))),
            fallback_tile_span - 1);
        leaves.append(MapRhiGlobeQuadtreeLeaf{fallback_zoom, fallback_x, fallback_y});
    }

    return leaves;
}

// Request priority measured on the globe, not in raw XYZ x/y space. This is
// important close to the poles where many different Mercator X tiles are at
// essentially the same physical distance from the crosshair. Lower values are
// dispatched first by MapTileRepository, matching the centre-out behaviour of
// the 2D/3D renderer.
int globeTileRequestPriority(
    int tile_x, int tile_y, int zoom, double center_lon_deg, double center_lat_deg)
{
    const double tile_lon_deg = GeoWebMercator::tileXToLon(double(tile_x) + 0.5, zoom);
    const double tile_lat_deg = GeoWebMercator::tileYToLat(double(tile_y) + 0.5, zoom);

    const double center_lat_rad = qDegreesToRadians(center_lat_deg);
    const double tile_lat_rad = qDegreesToRadians(tile_lat_deg);
    const double lon_delta_rad = qDegreesToRadians(
        GeoWebMercator::normalizeLongitude(tile_lon_deg - center_lon_deg));
    const double cosine_angle = qBound(
        -1.0,
        std::sin(center_lat_rad) * std::sin(tile_lat_rad)
            + std::cos(center_lat_rad) * std::cos(tile_lat_rad) * std::cos(lon_delta_rad),
        1.0);

    // 1-cos(theta) is monotonic over [0, pi], avoids acos(), and gives enough
    // integer resolution that the repository does not fall back to insertion
    // order except for genuinely equidistant tiles.
    return int(std::lround((1.0 - cosine_angle) * 1000000.0));
}


int globeTerrainZoomForImageryZoom(int imagery_zoom)
{
    const int configured_max_detail_zoom = qMax(
        GlobeTerrainReliefMinimumZoom,
        guiConfiguration().map_performance.terrain_max_detail_zoom);
    return qBound(
        GlobeTerrainReliefMinimumZoom, imagery_zoom, configured_max_detail_zoom);
}

QString globeTerrainDatasetId()
{
    return QStringLiteral("copernicus-glo30");
}

bool globeTerrainDatumUsable(MapTerrainVerticalDatum datum)
{
    return datum == MapTerrainVerticalDatum::Wgs84Ellipsoid
        || datum == MapTerrainVerticalDatum::Egm96
        || datum == MapTerrainVerticalDatum::Egm2008;
}

bool globeTerrainDatumIsOrthometric(MapTerrainVerticalDatum datum)
{
    return datum == MapTerrainVerticalDatum::Egm96
        || datum == MapTerrainVerticalDatum::Egm2008;
}

QShader loadGlobeShader(const QString &resource_path)
{
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly))
        return QShader();
    return QShader::fromSerialized(file.readAll());
}
}

MapRhiGlobeRenderer::MapRhiGlobeRenderer(MapModel *map_model, MapTileRepository *tile_repository)
    : map_model(map_model),
      tile_repository(tile_repository),
      terrain_mesh_scheduler(std::make_unique<MapRhiTerrainMeshScheduler>())
{
}

MapRhiGlobeRenderer::~MapRhiGlobeRenderer() = default;

void MapRhiGlobeRenderer::setTileRepository(MapTileRepository *new_tile_repository)
{
    if (this->tile_repository == new_tile_repository)
        return;

    this->tile_repository = new_tile_repository;
    invalidateImagery();
}

void MapRhiGlobeRenderer::setTerrainRepository(MapTerrainRepository *new_terrain_repository)
{
    if (this->terrain_repository == new_terrain_repository)
        return;

    this->terrain_repository = new_terrain_repository;
    this->reported_orthometric_datum_warning = false;
    this->reported_unusable_datum_warning = false;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.invalidate();

    // Terrain availability changes the fallback mesh density as well as the
    // data source, so rebuild the window from the ellipsoid. Old background
    // results are harmless: their request ids no longer match new tiles.
    this->window_dirty = true;
}

void MapRhiGlobeRenderer::notifyTerrainTileAvailable(const QString &key)
{
    if (key.isEmpty())
        return;

    for (GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_key != key)
            continue;
        tile.terrain_mesh_request_id = 0;
        tile.terrain_mesh_applied = false;
    }
}

void MapRhiGlobeRenderer::invalidateTerrain()
{
    // Do not flatten the currently displayed relief while a replacement is
    // built (for example after vertical exaggeration changed). Mark it stale
    // and let the async worker replace it in-place when ready.
    for (GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;
        tile.terrain_mesh_request_id = 0;
        tile.terrain_mesh_applied = false;
    }
}

void MapRhiGlobeRenderer::setWireframeVisible(bool visible)
{
    if (this->wireframe_visible == visible)
        return;

    this->wireframe_visible = visible;
    if (visible)
        rebuildWireframeVertices();
}

void MapRhiGlobeRenderer::setMapVisible(bool visible)
{
    if (this->map_visible == visible)
        return;

    this->map_visible = visible;
    if (visible)
        this->window_tiles_requested = false;
}

bool MapRhiGlobeRenderer::hasPendingTerrainMeshes() const
{
    if (this->terrain_lod_rebuild_pending)
        return true;

    for (const GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_mesh_request_id != 0)
            return true;
    }
    return false;
}

bool MapRhiGlobeRenderer::isVisibleTerrainReady() const
{
    if (this->terrain_repository == nullptr)
        return true;

    if (this->window_tiles.isEmpty())
        return false;

    for (const GlobeTile &tile : this->window_tiles)
    {
        if (tile.is_cap || tile.terrain_key.isEmpty())
            continue;
        if (!tile.terrain_mesh_applied)
            return false;
    }
    return true;
}

MapRhiGlobeRenderer::TileVertex MapRhiGlobeRenderer::makeTileVertex(
    double lon_deg, double lat_deg, float u, float v)
{
    const QVector3D position = GeoWgs84Ellipsoid::geodeticToEcef(lon_deg, lat_deg, 0.0);
    TileVertex vertex;
    vertex.x = position.x();
    vertex.y = position.y();
    vertex.z = position.z();
    vertex.u = u;
    vertex.v = v;
    return vertex;
}

void MapRhiGlobeRenderer::buildPolarCap(bool north)
{
    const double ring_lat = north
        ? GeoWebMercator::MaximumLatitude
        : -GeoWebMercator::MaximumLatitude;
    const double pole_lat = north ? 90.0 : -90.0;

    GlobeTile cap;
    cap.is_cap = true;
    cap.first_vertex = this->cap_vertices.size();

    const TileVertex pole_vertex = makeTileVertex(0.0, pole_lat, 0.5f, 0.5f);
    for (int segment = 0; segment < GlobePolarCapSegments; ++segment)
    {
        const double lon0 = -180.0 + 360.0 * double(segment) / double(GlobePolarCapSegments);
        const double lon1 = -180.0 + 360.0 * double(segment + 1) / double(GlobePolarCapSegments);
        const TileVertex ring0 = makeTileVertex(lon0, ring_lat, 0.5f, 0.5f);
        const TileVertex ring1 = makeTileVertex(lon1, ring_lat, 0.5f, 0.5f);

        this->cap_vertices.append(pole_vertex);
        if (north)
        {
            this->cap_vertices.append(ring0);
            this->cap_vertices.append(ring1);
        }
        else
        {
            this->cap_vertices.append(ring1);
            this->cap_vertices.append(ring0);
        }
    }

    cap.vertex_count = this->cap_vertices.size() - cap.first_vertex;
    this->cap_tiles.append(cap);
}

void MapRhiGlobeRenderer::buildCaps()
{
    if (this->caps_built)
        return;

    this->cap_vertices.clear();
    this->cap_tiles.clear();
    buildPolarCap(true);
    buildPolarCap(false);
    this->caps_built = true;
    this->cap_vertex_upload_pending = true;
    rebuildWireframeVertices();
}

int MapRhiGlobeRenderer::terrainCellCountForTile(
    const GlobeTile &tile, const QSize &viewport_size) const
{
    if (this->map_model == nullptr
        || tile.terrain_zoom < GlobeTerrainReliefMinimumZoom
        || tile.zoom < tile.terrain_zoom
        || !viewport_size.isValid())
    {
        return 1;
    }

    const int zoom_delta = tile.zoom - tile.terrain_zoom;
    const int cell_divisor = 1 << qMin(zoom_delta, 6);
    const int native_cell_count = qMax(
        1, MapTerrainTileCellCount / cell_divisor);
    const int maximum_cell_count = native_cell_count;
    const int minimum_cell_count = qMin(
        maximum_cell_count, GlobeTerrainMinimumLodCellCount);
    if (maximum_cell_count <= minimum_cell_count)
        return maximum_cell_count;

    const double tile_center_lon_deg = GeoWebMercator::tileXToLon(
        double(tile.virtual_x) + 0.5, tile.zoom);
    const double tile_center_lat_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y) + 0.5, tile.zoom);
    const QVector3D tile_center = GeoWgs84Ellipsoid::geodeticToEcef(
        tile_center_lon_deg, tile_center_lat_deg, 0.0);

    const double tile_lat_top_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y), tile.zoom);
    const double tile_lat_bottom_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y) + 1.0, tile.zoom);
    const double tile_width_m =
        (2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM
         * qMax(0.0, std::cos(qDegreesToRadians(tile_center_lat_deg))))
        / double(1 << tile.zoom);
    const double tile_height_m = GeoWgs84Ellipsoid::EquatorialRadiusM
        * std::abs(qDegreesToRadians(tile_lat_top_deg - tile_lat_bottom_deg));
    const double tile_reference_size_m = qMax(tile_width_m, tile_height_m);

    const GeoWgs84Ellipsoid::OrbitCameraBasis camera_basis =
        GeoWgs84Ellipsoid::orbitCameraBasis(
            this->map_model->centerLon(), this->map_model->centerLat(),
            this->map_model->viewGlobeYawDeg(),
            qBound(
                MapModel::MinViewGlobePitchDeg,
                this->map_model->viewGlobePitchDeg(),
                MapModel::MaxViewGlobePitchDeg),
            qMax(MapModel::MinViewGlobeDistanceM,
                 this->map_model->viewGlobeDistanceM()));

    const double ground_distance_from_focus_m =
        double((tile_center - camera_basis.target).length());

    // Exactly the same "full detail down to zoom" rule as flat RHI 3D:
    // only the focus tile is forced to the DEM-native density. The rest of
    // the retained globe still follows screen-space falloff.
    if (tile.zoom >= guiConfiguration().map_performance.terrain_full_detail_zoom
        && ground_distance_from_focus_m < tile_reference_size_m * 0.75)
    {
        return maximum_cell_count;
    }

    const double native_camera_distance_m = MapModel::viewGlobeDistanceMForZoomLevel(
        double(tile.zoom), this->map_model->centerLat(),
        qMax(1, viewport_size.height()));
    const double camera_to_tile_distance_m =
        double((tile_center - camera_basis.eye).length());
    const double camera_to_focus_distance_m =
        double((camera_basis.target - camera_basis.eye).length());
    const double focus_falloff_distance_m = std::hypot(
        camera_to_focus_distance_m, ground_distance_from_focus_m);

    // Matching the RHI 3D policy, a tile beneath an oblique camera must not
    // become more detailed than the crosshair/focus merely because the eye is
    // physically closer to it. The focus remains the highest-detail location.
    const double lod_distance_m = qMax(
        camera_to_tile_distance_m, focus_falloff_distance_m);
    const double projected_tile_scale = qBound(
        0.0,
        native_camera_distance_m / qMax(1e-9, lod_distance_m),
        4.0);
    const double target_cell_size_px = qMax(
        1.0, guiConfiguration().map_performance.terrain_lod_target_cell_size_px);
    const double desired_cell_count =
        (double(MapModel::TileSize) / target_cell_size_px)
        * projected_tile_scale;

    int cell_count = minimum_cell_count;
    while (cell_count < maximum_cell_count)
    {
        const int next_cell_count = qMin(
            maximum_cell_count, cell_count * 2);
        const double threshold = std::sqrt(
            double(cell_count) * double(next_cell_count));
        if (desired_cell_count < threshold)
            break;
        cell_count = next_cell_count;
    }

    return cell_count;
}

void MapRhiGlobeRenderer::updateTerrainStitchCellCounts(
    QVector<GlobeTile> *tiles) const
{
    if (tiles == nullptr)
        return;

    QHash<quint64, qsizetype> tiles_by_position;
    tiles_by_position.reserve(tiles->size());
    for (qsizetype index = 0; index < tiles->size(); ++index)
    {
        GlobeTile &tile = (*tiles)[index];
        tile.terrain_stitch_top_cell_count = 0;
        tile.terrain_stitch_right_cell_count = 0;
        tile.terrain_stitch_bottom_cell_count = 0;
        tile.terrain_stitch_left_cell_count = 0;

        if (tile.terrain_key.isEmpty() || tile.terrain_cell_count <= 0)
            continue;
        tiles_by_position.insert(
            globeQuadtreeNodeKey(tile.zoom, tile.tile_x, tile.tile_y), index);
    }

    for (qsizetype index = 0; index < tiles->size(); ++index)
    {
        GlobeTile &tile = (*tiles)[index];
        if (tile.terrain_key.isEmpty() || tile.terrain_cell_count <= 1)
            continue;

        const int left_x = GeoWebMercator::wrapTileX(tile.tile_x - 1, tile.zoom);
        const int right_x = GeoWebMercator::wrapTileX(tile.tile_x + 1, tile.zoom);

        const QHash<quint64, qsizetype>::const_iterator top_iterator =
            tiles_by_position.constFind(
                globeQuadtreeNodeKey(tile.zoom, tile.tile_x, tile.tile_y - 1));
        if (top_iterator != tiles_by_position.cend())
        {
            const GlobeTile &neighbor = tiles->at(top_iterator.value());
            if (neighbor.terrain_cell_count > 0
                && neighbor.terrain_cell_count < tile.terrain_cell_count
                && tile.terrain_cell_count % neighbor.terrain_cell_count == 0)
            {
                tile.terrain_stitch_top_cell_count =
                    neighbor.terrain_cell_count;
            }
        }

        const QHash<quint64, qsizetype>::const_iterator right_iterator =
            tiles_by_position.constFind(
                globeQuadtreeNodeKey(tile.zoom, right_x, tile.tile_y));
        if (right_iterator != tiles_by_position.cend())
        {
            const GlobeTile &neighbor = tiles->at(right_iterator.value());
            if (neighbor.terrain_cell_count > 0
                && neighbor.terrain_cell_count < tile.terrain_cell_count
                && tile.terrain_cell_count % neighbor.terrain_cell_count == 0)
            {
                tile.terrain_stitch_right_cell_count =
                    neighbor.terrain_cell_count;
            }
        }

        const QHash<quint64, qsizetype>::const_iterator bottom_iterator =
            tiles_by_position.constFind(
                globeQuadtreeNodeKey(tile.zoom, tile.tile_x, tile.tile_y + 1));
        if (bottom_iterator != tiles_by_position.cend())
        {
            const GlobeTile &neighbor = tiles->at(bottom_iterator.value());
            if (neighbor.terrain_cell_count > 0
                && neighbor.terrain_cell_count < tile.terrain_cell_count
                && tile.terrain_cell_count % neighbor.terrain_cell_count == 0)
            {
                tile.terrain_stitch_bottom_cell_count =
                    neighbor.terrain_cell_count;
            }
        }

        const QHash<quint64, qsizetype>::const_iterator left_iterator =
            tiles_by_position.constFind(
                globeQuadtreeNodeKey(tile.zoom, left_x, tile.tile_y));
        if (left_iterator != tiles_by_position.cend())
        {
            const GlobeTile &neighbor = tiles->at(left_iterator.value());
            if (neighbor.terrain_cell_count > 0
                && neighbor.terrain_cell_count < tile.terrain_cell_count
                && tile.terrain_cell_count % neighbor.terrain_cell_count == 0)
            {
                tile.terrain_stitch_left_cell_count =
                    neighbor.terrain_cell_count;
            }
        }

    }
}

bool MapRhiGlobeRenderer::currentTerrainLodMatches(
    const QSize &viewport_size) const
{
    for (const GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;
        if (tile.terrain_cell_count
            != terrainCellCountForTile(tile, viewport_size))
        {
            return false;
        }
    }
    return true;
}

void MapRhiGlobeRenderer::rebuildWindow(
    const QVector<MapRhiGlobeQuadtreeLeaf> &leaves, const QSize &viewport_size)
{
    const bool geometry_reuse_allowed = !this->window_dirty;
    QVector<TileVertex> previous_vertices = std::move(this->window_vertices);
    QVector<GlobeTile> previous_tiles = std::move(this->window_tiles);
    QHash<quint64, qsizetype> previous_tiles_by_position;
    if (geometry_reuse_allowed)
    {
        previous_tiles_by_position.reserve(previous_tiles.size());
        for (qsizetype index = 0; index < previous_tiles.size(); ++index)
        {
            const GlobeTile &tile = previous_tiles.at(index);
            previous_tiles_by_position.insert(
                globeQuadtreeNodeKey(tile.zoom, tile.tile_x, tile.tile_y), index);
        }
    }

    // Leaves come straight out of a quadtree partition, so distinct leaves
    // can never legitimately share a (zoom, x, y) identity -- the dedup set
    // here is just defensive bookkeeping against a future bug in the walk,
    // not something normal operation should ever hit.
    QVector<GlobeTile> next_tiles;
    next_tiles.reserve(leaves.size());
    QSet<quint64> seen_positions;
    seen_positions.reserve(leaves.size());
    for (const MapRhiGlobeQuadtreeLeaf &leaf : leaves)
    {
        const quint64 position_key =
            globeQuadtreeNodeKey(leaf.zoom, leaf.tile_x, leaf.tile_y);
        if (seen_positions.contains(position_key))
            continue;
        seen_positions.insert(position_key);

        const bool terrain_enabled =
            this->terrain_repository != nullptr
            && leaf.zoom >= GlobeTerrainReliefMinimumZoom;
        const int terrain_zoom = terrain_enabled
            ? globeTerrainZoomForImageryZoom(leaf.zoom)
            : -1;

        GlobeTile tile;
        tile.virtual_x = leaf.tile_x;
        tile.tile_x = leaf.tile_x;
        tile.tile_y = leaf.tile_y;
        tile.zoom = leaf.zoom;
        tile.imagery_key =
            this->map_model->tileCacheKeyAtZoom(leaf.tile_x, leaf.tile_y, leaf.zoom);

        if (terrain_enabled)
        {
            const int zoom_delta = leaf.zoom - terrain_zoom;
            MapTerrainTileAddress terrain_address;
            terrain_address.zoom = terrain_zoom;
            terrain_address.x = quint32(leaf.tile_x) >> zoom_delta;
            terrain_address.y = quint32(leaf.tile_y) >> zoom_delta;
            tile.terrain_zoom = terrain_zoom;
            tile.terrain_key =
                mapTerrainTileKey(globeTerrainDatasetId(), terrain_address);
            tile.terrain_cell_count =
                terrainCellCountForTile(tile, viewport_size);
        }

        next_tiles.append(tile);
    }

    // Same-zoom-neighbour terrain mesh density stitching only -- see
    // updateTerrainStitchCellCounts()'s own scope. A leaf whose neighbour is
    // at a different quadtree zoom (an actual LOD boundary) is not stitched
    // by this pass; that seam is a known, purely cosmetic follow-up (see the
    // class comment) and does not affect correctness or performance here.
    updateTerrainStitchCellCounts(&next_tiles);

    qsizetype estimated_vertex_count = 0;
    for (const GlobeTile &tile : next_tiles)
    {
        const int subdivisions = !tile.terrain_key.isEmpty()
            ? qMax(1, tile.terrain_cell_count)
            : subdivisionsForZoom(tile.zoom);
        estimated_vertex_count +=
            qsizetype(subdivisions) * qsizetype(subdivisions) * 6;
    }

    this->window_vertices.clear();
    this->window_vertices.reserve(estimated_vertex_count);
    this->window_tiles.clear();
    this->window_tiles.reserve(next_tiles.size());

    for (GlobeTile &tile : next_tiles)
    {
        const bool terrain_enabled = !tile.terrain_key.isEmpty();
        tile.first_vertex = this->window_vertices.size();
        const int subdivisions = terrain_enabled
            ? qMax(1, tile.terrain_cell_count)
            : subdivisionsForZoom(tile.zoom);
        const int expected_vertex_count = subdivisions * subdivisions * 6;

        bool reused = false;
        const QHash<quint64, qsizetype>::const_iterator previous_iterator =
            previous_tiles_by_position.constFind(
                globeQuadtreeNodeKey(tile.zoom, tile.tile_x, tile.tile_y));
        if (geometry_reuse_allowed
            && previous_iterator != previous_tiles_by_position.cend())
        {
            const GlobeTile &previous_tile =
                previous_tiles.at(previous_iterator.value());
            const bool same_geometry =
                previous_tile.zoom == tile.zoom
                && previous_tile.imagery_key == tile.imagery_key
                && previous_tile.terrain_key == tile.terrain_key
                && previous_tile.terrain_cell_count == tile.terrain_cell_count
                && previous_tile.terrain_stitch_top_cell_count
                    == tile.terrain_stitch_top_cell_count
                && previous_tile.terrain_stitch_right_cell_count
                    == tile.terrain_stitch_right_cell_count
                && previous_tile.terrain_stitch_bottom_cell_count
                    == tile.terrain_stitch_bottom_cell_count
                && previous_tile.terrain_stitch_left_cell_count
                    == tile.terrain_stitch_left_cell_count
                && previous_tile.vertex_count == expected_vertex_count
                && previous_tile.first_vertex >= 0
                && previous_tile.first_vertex + previous_tile.vertex_count
                    <= previous_vertices.size();
            if (same_geometry)
            {
                const TileVertex *source = previous_vertices.constData()
                    + previous_tile.first_vertex;
                for (int index = 0; index < previous_tile.vertex_count; ++index)
                    this->window_vertices.append(source[index]);
                tile.vertex_count = previous_tile.vertex_count;
                tile.terrain_mesh_request_id =
                    previous_tile.terrain_mesh_request_id;
                tile.terrain_mesh_applied =
                    previous_tile.terrain_mesh_applied;
                reused = true;
            }
        }

        if (!reused)
        {
            bool terrain_built = false;
            if (terrain_enabled
                && this->terrain_repository != nullptr
                && !tile.terrain_key.isEmpty()
                && tile.terrain_cell_count < GlobeAsyncTerrainMeshMinimumCellCount)
            {
                const MapTerrainTile *terrain_tile =
                    this->terrain_repository->tile(tile.terrain_key);
                if (terrain_tile != nullptr
                    && terrain_tile->elevations_m.size() == MapTerrainTileSampleCount
                    && globeTerrainDatumUsable(terrain_tile->vertical_datum))
                {
                    if (globeTerrainDatumIsOrthometric(terrain_tile->vertical_datum)
                        && !this->reported_orthometric_datum_warning)
                    {
                        qWarning().noquote()
                            << QStringLiteral(
                                   "Globe terrain tiles use an orthometric EGM vertical datum; "
                                   "using it directly as local ellipsoid-normal displacement until "
                                   "the terrain service exposes WGS84-ellipsoid tile heights.");
                        this->reported_orthometric_datum_warning = true;
                    }

                    MapRhiTerrainMeshRequest request;
                    request.terrain_key = tile.terrain_key;
                    request.terrain_tile = *terrain_tile;
                    request.terrain_available = true;
                    request.virtual_x = tile.virtual_x;
                    request.tile_x = tile.tile_x;
                    request.y = tile.tile_y;
                    request.imagery_zoom = tile.zoom;
                    request.terrain_zoom = tile.terrain_zoom;
                    request.requested_cell_count = tile.terrain_cell_count;
                    request.stitch_top_cell_count =
                        tile.terrain_stitch_top_cell_count;
                    request.stitch_right_cell_count =
                        tile.terrain_stitch_right_cell_count;
                    request.stitch_bottom_cell_count =
                        tile.terrain_stitch_bottom_cell_count;
                    request.stitch_left_cell_count =
                        tile.terrain_stitch_left_cell_count;
                    request.geometry = MapRhiTerrainMeshGeometry::GlobeEcef;
                    request.globe_vertical_exaggeration =
                        this->map_model->view3dVerticalExaggeration();

                    const MapRhiTerrainMeshResult result =
                        buildTerrainMeshResult(request);
                    if (result.vertices.size() == expected_vertex_count)
                    {
                        for (const MapRhiTerrainMeshVertex &vertex : result.vertices)
                        {
                            this->window_vertices.append(TileVertex{
                                vertex.x, vertex.y, vertex.z, vertex.u, vertex.v});
                        }
                        tile.vertex_count = expected_vertex_count;
                        tile.terrain_mesh_applied = true;
                        terrain_built = true;
                    }
                }
            }

            if (!terrain_built)
            {
                for (int row = 0; row < subdivisions; ++row)
                {
                    const double v0 = double(row) / double(subdivisions);
                    const double v1 = double(row + 1) / double(subdivisions);
                    const double lat0 = GeoWebMercator::tileYToLat(
                        double(tile.tile_y) + v0, tile.zoom);
                    const double lat1 = GeoWebMercator::tileYToLat(
                        double(tile.tile_y) + v1, tile.zoom);

                    for (int column = 0; column < subdivisions; ++column)
                    {
                        const double u0 = double(column) / double(subdivisions);
                        const double u1 = double(column + 1) / double(subdivisions);
                        const double lon0 = GeoWebMercator::tileXToLon(
                            double(tile.virtual_x) + u0, tile.zoom);
                        const double lon1 = GeoWebMercator::tileXToLon(
                            double(tile.virtual_x) + u1, tile.zoom);

                        const TileVertex p00 = makeTileVertex(
                            lon0, lat0, float(u0), float(v0));
                        const TileVertex p10 = makeTileVertex(
                            lon1, lat0, float(u1), float(v0));
                        const TileVertex p01 = makeTileVertex(
                            lon0, lat1, float(u0), float(v1));
                        const TileVertex p11 = makeTileVertex(
                            lon1, lat1, float(u1), float(v1));

                        this->window_vertices.append(p00);
                        this->window_vertices.append(p01);
                        this->window_vertices.append(p10);
                        this->window_vertices.append(p10);
                        this->window_vertices.append(p01);
                        this->window_vertices.append(p11);
                    }
                }
                tile.vertex_count = expected_vertex_count;
            }
        }

        this->window_tiles.append(tile);
    }

    this->window_dirty = false;
    this->window_tiles_requested = false;
    this->window_vertex_upload_pending = true;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.restart();

    rebuildWireframeVertices();
    pruneUnusedTileResources();
}

QVector<MapRhiGlobeQuadtreeLeaf> MapRhiGlobeRenderer::currentWindowLeaves() const
{
    QVector<MapRhiGlobeQuadtreeLeaf> leaves;
    leaves.reserve(this->window_tiles.size());
    for (const GlobeTile &tile : this->window_tiles)
        leaves.append(MapRhiGlobeQuadtreeLeaf{tile.zoom, tile.tile_x, tile.tile_y});
    return leaves;
}

void MapRhiGlobeRenderer::appendWireframeEdges(const QVector<TileVertex> &vertices)
{
    for (qsizetype vertex_index = 0; vertex_index + 2 < vertices.size(); vertex_index += 3)
    {
        const TileVertex &a = vertices.at(vertex_index);
        const TileVertex &b = vertices.at(vertex_index + 1);
        const TileVertex &c = vertices.at(vertex_index + 2);
        const WireframeVertex wa = {a.x, a.y, a.z};
        const WireframeVertex wb = {b.x, b.y, b.z};
        const WireframeVertex wc = {c.x, c.y, c.z};

        this->wireframe_vertices.append(wa);
        this->wireframe_vertices.append(wb);
        this->wireframe_vertices.append(wb);
        this->wireframe_vertices.append(wc);
        this->wireframe_vertices.append(wc);
        this->wireframe_vertices.append(wa);
    }
}

void MapRhiGlobeRenderer::rebuildWireframeVertices()
{
    this->wireframe_vertices.clear();
    this->wireframe_vertices.reserve(
        (this->window_vertices.size() + this->cap_vertices.size()) * 2);
    appendWireframeEdges(this->window_vertices);
    appendWireframeEdges(this->cap_vertices);
    this->wireframe_vertex_upload_pending = true;
}

bool MapRhiGlobeRenderer::uploadWireframeVertices(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (!this->wireframe_visible || !this->wireframe_vertex_upload_pending)
        return true;

    if (this->wireframe_vertices.isEmpty())
    {
        this->wireframe_vertex_buffer.reset();
        this->wireframe_vertex_buffer_size = 0;
        this->wireframe_vertex_upload_pending = false;
        return true;
    }

    const int required_bytes = int(
        this->wireframe_vertices.size() * qsizetype(sizeof(WireframeVertex)));
    if (!this->wireframe_vertex_buffer
        || this->wireframe_vertex_buffer_size != required_bytes)
    {
        this->wireframe_vertex_buffer.reset(this->rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_bytes));
        if (!this->wireframe_vertex_buffer || !this->wireframe_vertex_buffer->create())
            return false;
        this->wireframe_vertex_buffer_size = required_bytes;
    }

    resource_updates->updateDynamicBuffer(
        this->wireframe_vertex_buffer.get(), 0, required_bytes,
        this->wireframe_vertices.constData());
    this->wireframe_vertex_upload_pending = false;
    return true;
}

void MapRhiGlobeRenderer::pruneUnusedTileResources()
{
    QSet<QString> keys_in_use;
    keys_in_use.reserve(this->window_tiles.size());
    for (const GlobeTile &tile : this->window_tiles)
    {
        if (!tile.imagery_key.isEmpty())
            keys_in_use.insert(tile.imagery_key);
    }

    for (auto it = this->tile_resources.begin(); it != this->tile_resources.end();)
    {
        if (keys_in_use.contains(it->first))
            ++it;
        else
            it = this->tile_resources.erase(it);
    }
}

bool MapRhiGlobeRenderer::rebuildTileBindings(TileResource *resource)
{
    resource->bindings.reset(this->rhi->newShaderResourceBindings());
    if (!resource->bindings)
        return false;

    resource->bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage
                | QRhiShaderResourceBinding::FragmentStage,
            this->camera_uniform_buffer.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage,
            resource->texture.get(), this->sampler.get())
    });
    return resource->bindings->create();
}

bool MapRhiGlobeRenderer::ensureTileResource(
    GlobeTile &tile, QRhiResourceUpdateBatch *resource_updates)
{
    if (tile.is_cap)
    {
        if (!this->cap_resource.texture)
        {
            QImage image(1, 1, QImage::Format_RGBA8888);
            image.fill(GlobePolarCapColor);
            this->cap_resource.texture.reset(
                this->rhi->newTexture(QRhiTexture::RGBA8, image.size()));
            if (!this->cap_resource.texture || !this->cap_resource.texture->create())
                return false;
            resource_updates->uploadTexture(this->cap_resource.texture.get(), image);
        }
        if (!this->cap_resource.bindings && !rebuildTileBindings(&this->cap_resource))
            return false;

        tile.resource = &this->cap_resource;
        return true;
    }

    if (this->tile_repository == nullptr || tile.imagery_key.isEmpty())
        return true;

    const QPixmap *pixmap = this->tile_repository->tile(tile.imagery_key);
    if (pixmap == nullptr)
        return true;

    std::unique_ptr<TileResource> &slot = this->tile_resources[tile.imagery_key];
    if (!slot)
        slot = std::make_unique<TileResource>();

    TileResource *resource = slot.get();
    const qint64 cache_key = pixmap->cacheKey();
    if (!resource->texture || resource->pixmap_cache_key != cache_key)
    {
        QImage image = pixmap->toImage().convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull())
            return true;

        resource->bindings.reset();
        resource->texture.reset(this->rhi->newTexture(QRhiTexture::RGBA8, image.size()));
        if (!resource->texture || !resource->texture->create())
            return false;
        resource_updates->uploadTexture(resource->texture.get(), image);
        resource->pixmap_cache_key = cache_key;
    }

    if (!resource->bindings && !rebuildTileBindings(resource))
        return false;

    tile.resource = resource;
    return true;
}

void MapRhiGlobeRenderer::requestMissingTiles(QRhiResourceUpdateBatch *resource_updates)
{
    if (this->dummy_texture_upload_pending && this->dummy_texture)
    {
        QImage image(1, 1, QImage::Format_RGBA8888);
        image.fill(GlobeMissingTileColor);
        resource_updates->uploadTexture(this->dummy_texture.get(), image);
        this->dummy_texture_upload_pending = false;
    }

    if (this->tile_repository != nullptr && !this->window_tiles_requested)
    {
        const quint64 batch = this->tile_repository->beginTileRequestBatch(
            this, QStringLiteral("globe"));
        for (const GlobeTile &tile : this->window_tiles)
        {
            if (this->tile_repository->tile(tile.imagery_key) != nullptr)
                continue;

            const int priority = globeTileRequestPriority(
                tile.tile_x, tile.tile_y, tile.zoom,
                this->map_model->centerLon(), this->map_model->centerLat());
            this->tile_repository->requestTile(
                this->map_model->tileEndpointAtZoom(tile.tile_x, tile.tile_y, tile.zoom),
                tile.imagery_key, tile.tile_x, tile.tile_y, priority, batch, true);
        }
        this->window_tiles_requested = true;
    }

    for (GlobeTile &tile : this->window_tiles)
        ensureTileResource(tile, resource_updates);
    for (GlobeTile &tile : this->cap_tiles)
        ensureTileResource(tile, resource_updates);
}


void MapRhiGlobeRenderer::requestMissingTerrainTiles()
{
    if (this->terrain_repository == nullptr || this->window_tiles.isEmpty())
        return;

    QVector<const GlobeTile *> candidates;
    candidates.reserve(this->window_tiles.size());
    for (const GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_zoom < GlobeTerrainReliefMinimumZoom
            || tile.terrain_key.isEmpty())
        {
            continue;
        }
        candidates.append(&tile);
    }

    std::sort(
        candidates.begin(), candidates.end(),
        [this](const GlobeTile *first, const GlobeTile *second)
    {
        const int first_priority = globeTileRequestPriority(
            first->tile_x, first->tile_y, first->zoom,
            this->map_model->centerLon(), this->map_model->centerLat());
        const int second_priority = globeTileRequestPriority(
            second->tile_x, second->tile_y, second->zoom,
            this->map_model->centerLon(), this->map_model->centerLat());
        if (first_priority != second_priority)
            return first_priority < second_priority;
        if (first->terrain_zoom != second->terrain_zoom)
            return first->terrain_zoom > second->terrain_zoom;
        if (first->tile_y != second->tile_y)
            return first->tile_y < second->tile_y;
        return first->tile_x < second->tile_x;
    });

    QSet<QString> requested_keys;
    requested_keys.reserve(candidates.size());
    for (const GlobeTile *tile : candidates)
    {
        if (tile == nullptr
            || requested_keys.contains(tile->terrain_key)
            || this->terrain_repository->tile(tile->terrain_key) != nullptr)
        {
            continue;
        }
        requested_keys.insert(tile->terrain_key);

        const int zoom_delta = tile->zoom - tile->terrain_zoom;
        const quint32 terrain_x = quint32(tile->tile_x) >> zoom_delta;
        const quint32 terrain_y = quint32(tile->tile_y) >> zoom_delta;
        this->terrain_repository->requestTile(
            globeTerrainDatasetId(), tile->terrain_zoom, terrain_x, terrain_y);
    }
}

void MapRhiGlobeRenderer::scheduleReadyTerrainMeshes()
{
    if (this->terrain_repository == nullptr
        || this->terrain_mesh_scheduler == nullptr
        || this->map_model == nullptr)
    {
        return;
    }

    QVector<GlobeTile *> candidates;
    candidates.reserve(this->window_tiles.size());
    for (GlobeTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty()
            || tile.terrain_mesh_applied
            || tile.terrain_mesh_request_id != 0)
        {
            continue;
        }

        if (this->terrain_repository->tile(tile.terrain_key) == nullptr)
            continue;
        candidates.append(&tile);
    }

    std::sort(
        candidates.begin(), candidates.end(),
        [this](const GlobeTile *first, const GlobeTile *second)
    {
        const int first_priority = globeTileRequestPriority(
            first->tile_x, first->tile_y, first->zoom,
            this->map_model->centerLon(), this->map_model->centerLat());
        const int second_priority = globeTileRequestPriority(
            second->tile_x, second->tile_y, second->zoom,
            this->map_model->centerLon(), this->map_model->centerLat());
        return first_priority < second_priority;
    });

    for (GlobeTile *tile : candidates)
    {
        if (tile == nullptr)
            continue;

        const MapTerrainTile *terrain_tile =
            this->terrain_repository->tile(tile->terrain_key);
        if (terrain_tile == nullptr)
            continue;

        if (!globeTerrainDatumUsable(terrain_tile->vertical_datum))
        {
            if (!this->reported_unusable_datum_warning)
            {
                qWarning().noquote()
                    << QStringLiteral(
                           "Globe terrain is ignoring terrain tiles with an unknown/local "
                           "vertical datum because they cannot be interpreted as global height.");
                this->reported_unusable_datum_warning = true;
            }
            tile->terrain_mesh_applied = true;
            continue;
        }

        if (globeTerrainDatumIsOrthometric(terrain_tile->vertical_datum)
            && !this->reported_orthometric_datum_warning)
        {
            // The normalized terrain tile API currently has no per-tile
            // WGS84-ellipsoid conversion selector. Preserve the real relief
            // shape by using EGM orthometric height as the local displacement
            // for now, but make the datum approximation explicit rather than
            // silently pretending it is ellipsoidal height. A later datum
            // conversion boundary can replace this without changing the ECEF
            // mesh architecture introduced here.
            qWarning().noquote()
                << QStringLiteral(
                       "Globe terrain tiles use an orthometric EGM vertical datum; "
                       "using it directly as local ellipsoid-normal displacement until "
                       "the terrain service exposes WGS84-ellipsoid tile heights.");
            this->reported_orthometric_datum_warning = true;
        }

        MapRhiTerrainMeshRequest request;
        request.request_id = this->next_terrain_mesh_request_id++;
        request.terrain_key = tile->terrain_key;
        request.terrain_tile = *terrain_tile;
        request.terrain_available =
            terrain_tile->elevations_m.size() == MapTerrainTileSampleCount;
        request.virtual_x = tile->virtual_x;
        request.tile_x = tile->tile_x;
        request.y = tile->tile_y;
        request.imagery_zoom = tile->zoom;
        request.terrain_zoom = tile->terrain_zoom;
        request.requested_cell_count = tile->terrain_cell_count;
        request.stitch_top_cell_count =
            tile->terrain_stitch_top_cell_count;
        request.stitch_right_cell_count =
            tile->terrain_stitch_right_cell_count;
        request.stitch_bottom_cell_count =
            tile->terrain_stitch_bottom_cell_count;
        request.stitch_left_cell_count =
            tile->terrain_stitch_left_cell_count;
        request.geometry = MapRhiTerrainMeshGeometry::GlobeEcef;
        request.globe_vertical_exaggeration =
            this->map_model->view3dVerticalExaggeration();

        tile->terrain_mesh_request_id = request.request_id;
        this->terrain_mesh_scheduler->submit(request);
    }
}

bool MapRhiGlobeRenderer::applyReadyTerrainMeshes(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (this->terrain_mesh_scheduler == nullptr || resource_updates == nullptr)
        return true;

    QVector<MapRhiTerrainMeshResult> results;
    this->terrain_mesh_scheduler->collectReady(&results);
    if (results.isEmpty())
        return true;

    bool wireframe_changed = false;
    for (const MapRhiTerrainMeshResult &result : results)
    {
        for (GlobeTile &tile : this->window_tiles)
        {
            if (tile.terrain_mesh_request_id != result.request_id)
                continue;

            tile.terrain_mesh_request_id = 0;
            if (!result.terrain_available
                || result.cell_count != tile.terrain_cell_count
                || result.stitch_top_cell_count
                    != tile.terrain_stitch_top_cell_count
                || result.stitch_right_cell_count
                    != tile.terrain_stitch_right_cell_count
                || result.stitch_bottom_cell_count
                    != tile.terrain_stitch_bottom_cell_count
                || result.stitch_left_cell_count
                    != tile.terrain_stitch_left_cell_count
                || result.vertices.size() != tile.vertex_count)
            {
                break;
            }

            const qsizetype first_vertex = tile.first_vertex;
            for (qsizetype index = 0; index < result.vertices.size(); ++index)
            {
                const MapRhiTerrainMeshVertex &vertex = result.vertices.at(index);
                this->window_vertices[first_vertex + index] =
                    TileVertex{vertex.x, vertex.y, vertex.z, vertex.u, vertex.v};
            }

            tile.terrain_mesh_applied = true;
            wireframe_changed = true;

            if (this->window_vertex_buffer)
            {
                const int byte_offset =
                    int(first_vertex * qsizetype(sizeof(TileVertex)));
                const int byte_count =
                    int(result.vertices.size() * qsizetype(sizeof(TileVertex)));
                resource_updates->updateDynamicBuffer(
                    this->window_vertex_buffer.get(), byte_offset, byte_count,
                    this->window_vertices.constData() + first_vertex);
            }
            else
            {
                this->window_vertex_upload_pending = true;
            }

            break;
        }
    }

    if (wireframe_changed && this->wireframe_visible)
        rebuildWireframeVertices();
    return true;
}

bool MapRhiGlobeRenderer::ensureSharedResources()
{
    if (this->rhi == nullptr || this->render_pass_descriptor == nullptr)
        return false;

    if (!this->camera_uniform_buffer)
    {
        this->camera_uniform_buffer.reset(this->rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, GlobeCameraUniformBytes));
        if (!this->camera_uniform_buffer || !this->camera_uniform_buffer->create())
            return false;
    }

    if (!this->sampler)
    {
        this->sampler.reset(this->rhi->newSampler(
            QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
            QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!this->sampler || !this->sampler->create())
            return false;
    }

    if (!this->dummy_texture)
    {
        this->dummy_texture.reset(this->rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!this->dummy_texture || !this->dummy_texture->create())
            return false;
        this->dummy_texture_upload_pending = true;
    }

    if (!this->template_bindings)
    {
        this->template_bindings.reset(this->rhi->newShaderResourceBindings());
        if (!this->template_bindings)
            return false;
        this->template_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                this->camera_uniform_buffer.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                this->dummy_texture.get(), this->sampler.get())
        });
        if (!this->template_bindings->create())
            return false;
    }

    if (!this->wireframe_bindings)
    {
        this->wireframe_bindings.reset(this->rhi->newShaderResourceBindings());
        if (!this->wireframe_bindings)
            return false;
        this->wireframe_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage,
                this->camera_uniform_buffer.get())
        });
        if (!this->wireframe_bindings->create())
            return false;
    }

    if (!this->pipeline)
    {
        const QShader vertex_shader = loadGlobeShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe.vert.qsb"));
        const QShader fragment_shader = loadGlobeShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
            return false;

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(TileVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(TileVertex, x))},
            {0, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(TileVertex, u))}
        });

        this->pipeline.reset(this->rhi->newGraphicsPipeline());
        if (!this->pipeline)
            return false;
        this->pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->pipeline->setVertexInputLayout(input_layout);
        this->pipeline->setShaderResourceBindings(this->template_bindings.get());
        this->pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->pipeline->setSampleCount(this->sample_count);
        this->pipeline->setDepthTest(true);
        this->pipeline->setDepthWrite(true);
        this->pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if (!this->pipeline->create())
            return false;
    }

    if (!this->wireframe_pipeline)
    {
        const QShader vertex_shader = loadGlobeShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe_wireframe.vert.qsb"));
        const QShader fragment_shader = loadGlobeShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe_wireframe.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
            return false;

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(WireframeVertex))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(WireframeVertex, x))}
        });

        this->wireframe_pipeline.reset(this->rhi->newGraphicsPipeline());
        if (!this->wireframe_pipeline)
            return false;
        this->wireframe_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->wireframe_pipeline->setVertexInputLayout(input_layout);
        this->wireframe_pipeline->setShaderResourceBindings(
            this->wireframe_bindings.get());
        this->wireframe_pipeline->setRenderPassDescriptor(
            this->render_pass_descriptor);
        this->wireframe_pipeline->setTopology(QRhiGraphicsPipeline::Lines);
        this->wireframe_pipeline->setSampleCount(this->sample_count);
        this->wireframe_pipeline->setDepthTest(true);
        this->wireframe_pipeline->setDepthWrite(false);
        this->wireframe_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if (!this->wireframe_pipeline->create())
            return false;
    }

    return true;
}

bool MapRhiGlobeRenderer::initialize(
    QRhi *rhi_instance, QRhiRenderPassDescriptor *render_pass_descriptor_instance,
    int sample_count_value)
{
    if (rhi_instance == nullptr || render_pass_descriptor_instance == nullptr)
        return false;

    this->rhi = rhi_instance;
    this->render_pass_descriptor = render_pass_descriptor_instance;
    this->sample_count = sample_count_value;

    buildCaps();
    return ensureSharedResources();
}

bool MapRhiGlobeRenderer::prepare(
    QRhiResourceUpdateBatch *resource_updates, const QMatrix4x4 &view_projection,
    const QSize &viewport_size)
{
    if (this->rhi == nullptr || resource_updates == nullptr || this->map_model == nullptr)
        return false;
    if (!ensureSharedResources())
        return false;

    // Walk the quadtree fresh every frame -- see the class comment for why
    // this replaced the old single-zoom rectangular window. The walk itself
    // is cheap (horizon/frustum culling plus the hard visit cap keep it to
    // at most a few hundred visited nodes in normal operation); only the
    // actual geometry rebuild below is comparatively expensive, and that is
    // gated on the resulting leaf set actually differing from what is
    // already built.
    const QVector<MapRhiGlobeQuadtreeLeaf> desired_leaves = selectVisibleGlobeQuadtreeLeaves(
        *this->map_model, viewport_size, &this->previously_subdivided_quadtree_nodes);

    bool leaves_match_window = !this->window_dirty
        && desired_leaves.size() == this->window_tiles.size();
    if (leaves_match_window)
    {
        QSet<quint64> window_keys;
        window_keys.reserve(this->window_tiles.size());
        for (const GlobeTile &tile : this->window_tiles)
            window_keys.insert(globeQuadtreeNodeKey(tile.zoom, tile.tile_x, tile.tile_y));
        for (const MapRhiGlobeQuadtreeLeaf &leaf : desired_leaves)
        {
            if (!window_keys.contains(
                    globeQuadtreeNodeKey(leaf.zoom, leaf.tile_x, leaf.tile_y)))
            {
                leaves_match_window = false;
                break;
            }
        }
    }

    if (!leaves_match_window)
    {
        rebuildWindow(desired_leaves, viewport_size);
    }
    else
    {
        const bool terrain_enabled = this->terrain_repository != nullptr;
        const bool terrain_lod_matches =
            !terrain_enabled || currentTerrainLodMatches(viewport_size);
        if (!terrain_lod_matches)
        {
            if (this->terrain_lod_rebuild_clock.isValid()
                && this->terrain_lod_rebuild_clock.elapsed()
                    < GlobeMinimumTerrainLodRebuildIntervalMs)
            {
                // Keep the current terrain mesh on screen until the same
                // 120 ms LOD debounce used by RHI 3D expires. renderGlobe()
                // keeps requesting frames while this flag is set, so a drag
                // that stops inside the debounce window still settles to the
                // correct LOD without another input event.
                this->terrain_lod_rebuild_pending = true;
            }
            else
            {
                rebuildWindow(currentWindowLeaves(), viewport_size);
            }
        }
        else
        {
            this->terrain_lod_rebuild_pending = false;
        }
    }

    if (!applyReadyTerrainMeshes(resource_updates))
        return false;
    requestMissingTerrainTiles();
    scheduleReadyTerrainMeshes();

    if (this->window_vertex_upload_pending && !this->window_vertices.isEmpty())
    {
        const int required_bytes =
            int(this->window_vertices.size() * qsizetype(sizeof(TileVertex)));
        if (!this->window_vertex_buffer || this->window_vertex_buffer_size != required_bytes)
        {
            this->window_vertex_buffer.reset(this->rhi->newBuffer(
                QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_bytes));
            if (!this->window_vertex_buffer || !this->window_vertex_buffer->create())
                return false;
            this->window_vertex_buffer_size = required_bytes;
        }
        resource_updates->updateDynamicBuffer(
            this->window_vertex_buffer.get(), 0, required_bytes,
            this->window_vertices.constData());
        this->window_vertex_upload_pending = false;
    }

    if (this->cap_vertex_upload_pending && !this->cap_vertices.isEmpty())
    {
        const int required_bytes =
            int(this->cap_vertices.size() * qsizetype(sizeof(TileVertex)));
        if (!this->cap_vertex_buffer)
        {
            this->cap_vertex_buffer.reset(this->rhi->newBuffer(
                QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, required_bytes));
            if (!this->cap_vertex_buffer || !this->cap_vertex_buffer->create())
                return false;
        }
        resource_updates->updateDynamicBuffer(
            this->cap_vertex_buffer.get(), 0, required_bytes, this->cap_vertices.constData());
        this->cap_vertex_upload_pending = false;
    }

    if (!uploadWireframeVertices(resource_updates))
        return false;

    if (this->map_visible)
        requestMissingTiles(resource_updates);

    float matrix_data[16];
    std::copy(view_projection.constData(), view_projection.constData() + 16, matrix_data);
    resource_updates->updateDynamicBuffer(
        this->camera_uniform_buffer.get(), 0, GlobeCameraUniformBytes, matrix_data);

    return true;
}

void MapRhiGlobeRenderer::draw(QRhiCommandBuffer *command_buffer)
{
    if (command_buffer == nullptr)
        return;

    if (this->map_visible && this->pipeline)
    {
        command_buffer->setGraphicsPipeline(this->pipeline.get());

        if (this->window_vertex_buffer)
        {
            for (const GlobeTile &tile : this->window_tiles)
            {
                if (tile.vertex_count <= 0)
                    continue;

                QRhiShaderResourceBindings *bindings = this->template_bindings.get();
                if (tile.resource != nullptr && tile.resource->bindings)
                    bindings = tile.resource->bindings.get();
                if (bindings == nullptr)
                    continue;

                command_buffer->setShaderResources(bindings);
                const quint32 byte_offset = quint32(
                    tile.first_vertex * int(sizeof(TileVertex)));
                const QRhiCommandBuffer::VertexInput binding(
                    this->window_vertex_buffer.get(), byte_offset);
                command_buffer->setVertexInput(0, 1, &binding);
                command_buffer->draw(quint32(tile.vertex_count));
            }
        }

        if (this->cap_vertex_buffer)
        {
            for (const GlobeTile &tile : this->cap_tiles)
            {
                if (tile.resource == nullptr || !tile.resource->bindings
                    || tile.vertex_count <= 0)
                {
                    continue;
                }

                command_buffer->setShaderResources(tile.resource->bindings.get());
                const quint32 byte_offset = quint32(
                    tile.first_vertex * int(sizeof(TileVertex)));
                const QRhiCommandBuffer::VertexInput binding(
                    this->cap_vertex_buffer.get(), byte_offset);
                command_buffer->setVertexInput(0, 1, &binding);
                command_buffer->draw(quint32(tile.vertex_count));
            }
        }
    }

    if (this->wireframe_visible
        && this->wireframe_pipeline && this->wireframe_bindings
        && this->wireframe_vertex_buffer && !this->wireframe_vertices.isEmpty())
    {
        command_buffer->setGraphicsPipeline(this->wireframe_pipeline.get());
        command_buffer->setShaderResources(this->wireframe_bindings.get());
        const QRhiCommandBuffer::VertexInput binding(
            this->wireframe_vertex_buffer.get(), 0);
        command_buffer->setVertexInput(0, 1, &binding);
        command_buffer->draw(quint32(this->wireframe_vertices.size()));
    }
}

void MapRhiGlobeRenderer::invalidateImagery()
{
    this->tile_resources.clear();
    this->cap_resource = TileResource();
    for (GlobeTile &tile : this->window_tiles)
        tile.resource = nullptr;
    for (GlobeTile &tile : this->cap_tiles)
        tile.resource = nullptr;
    this->window_tiles_requested = false;
}

void MapRhiGlobeRenderer::releaseResources()
{
    this->pipeline.reset();
    this->wireframe_pipeline.reset();
    this->template_bindings.reset();
    this->wireframe_bindings.reset();
    this->dummy_texture.reset();
    this->dummy_texture_upload_pending = true;
    this->sampler.reset();
    this->camera_uniform_buffer.reset();
    this->window_vertex_buffer.reset();
    this->window_vertex_buffer_size = 0;
    this->window_vertex_upload_pending = true;
    this->wireframe_vertex_buffer.reset();
    this->wireframe_vertex_buffer_size = 0;
    this->wireframe_vertex_upload_pending = true;
    this->window_dirty = true;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.invalidate();
    this->previously_subdivided_quadtree_nodes.clear();
    this->cap_vertex_buffer.reset();
    this->cap_vertex_upload_pending = true;
    invalidateImagery();
}
