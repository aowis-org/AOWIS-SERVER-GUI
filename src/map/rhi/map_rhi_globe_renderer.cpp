#include "map/rhi/map_rhi_globe_renderer.h"

#include "map/core/map_model.h"
#include "map/data/map_tile_repository.h"
#include "map/data/map_terrain_repository.h"
#include "map/data/map_terrain_tile.h"
#include "map/rhi/map_rhi_terrain_mesh_scheduler.h"
#include "config/gui_configuration.h"
#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QLoggingCategory>
#include <QPainter>
#include <QPixmap>
#include <QRadialGradient>
#include <QRect>
#include <QSet>
#include <QtMath>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

Q_LOGGING_CATEGORY(
    globeHeatmapPerformanceLog,
    "aowis.map.rhi.globe.heatmap.performance",
    QtInfoMsg)

namespace
{
class ScopedHeatmapProfileTimer
{
public:
    ScopedHeatmapProfileTimer(qint64 *target, bool enabled)
        : target(enabled ? target : nullptr)
    {
        if (this->target != nullptr)
            this->timer.start();
    }

    ~ScopedHeatmapProfileTimer()
    {
        if (this->target != nullptr)
            *this->target += this->timer.nsecsElapsed();
    }

private:
    qint64 *target = nullptr;
    QElapsedTimer timer;
};

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

constexpr int GlobeCameraUniformBytes = 24 * int(sizeof(float));

// Matches MapRhiBasemapRenderer's HeatmapTextureSize exactly (one texel per
// Web Mercator pixel at whatever zoom a given tile is fetched at) -- see
// MapRhiGlobeRenderer::renderHeatmapTile().
constexpr int GlobeHeatmapTextureSize = 256;
// A marker is retained once at every Web Mercator index level. Candidate
// queries choose a level whose expanded tile footprint is only a few cells
// wide, avoiding both the old zoom-18 empty-cell walks and full-network scans.
constexpr int GlobeHeatmapMarkerMaximumBucketZoom = GlobeImageryMaxZoom;
constexpr double GlobeHeatmapMarkerTargetBucketSpan = 4.0;
constexpr int GlobeHeatmapValidationTolerance = 8;

struct GlobeHeatmapValidationMetrics
{
    quint64 absolute_error_sum = 0;
    quint64 alpha_error_sum = 0;
    int maximum_channel_error = 0;
    int pixels_over_tolerance = 0;
    int premultiplied_violations = 0;
    int active_pixels = 0;
    int cpu_covered_pixels = 0;
    int gpu_covered_pixels = 0;
};

QString globeHeatmapTextureFormatName(QRhiTexture::Format format)
{
    switch (format)
    {
    case QRhiTexture::RGBA8:
        return QStringLiteral("RGBA8");
    case QRhiTexture::BGRA8:
        return QStringLiteral("BGRA8");
    default:
        return QStringLiteral("unsupported_%1").arg(int(format));
    }
}

GlobeHeatmapValidationMetrics compareGlobeHeatmapPixels(
    const QImage &cpu_image, const QByteArray &gpu_data,
    QRhiTexture::Format gpu_format, bool flip_y)
{
    GlobeHeatmapValidationMetrics metrics;
    const int width = cpu_image.width();
    const int height = cpu_image.height();
    const int gpu_red = gpu_format == QRhiTexture::BGRA8 ? 2 : 0;
    const int gpu_blue = gpu_format == QRhiTexture::BGRA8 ? 0 : 2;
    const uchar *gpu_bytes = reinterpret_cast<const uchar *>(
        gpu_data.constData());

    for (int y = 0; y < height; ++y)
    {
        const uchar *cpu_row = cpu_image.constScanLine(y);
        const int gpu_y = flip_y ? height - 1 - y : y;
        const uchar *gpu_row = gpu_bytes
            + qsizetype(gpu_y) * qsizetype(width) * 4;
        for (int x = 0; x < width; ++x)
        {
            const uchar *cpu_pixel = cpu_row + x * 4;
            const uchar *gpu_pixel = gpu_row + x * 4;
            const int gpu_channels[] = {
                gpu_pixel[gpu_red], gpu_pixel[1],
                gpu_pixel[gpu_blue], gpu_pixel[3]
            };
            if (cpu_pixel[3] > 1)
                ++metrics.cpu_covered_pixels;
            if (gpu_channels[3] > 1)
                ++metrics.gpu_covered_pixels;
            if (cpu_pixel[3] > 1 || gpu_channels[3] > 1)
                ++metrics.active_pixels;
            int pixel_maximum_error = 0;
            for (int channel = 0; channel < 4; ++channel)
            {
                const int error = std::abs(
                    int(cpu_pixel[channel]) - gpu_channels[channel]);
                metrics.absolute_error_sum += quint64(error);
                metrics.maximum_channel_error = qMax(
                    metrics.maximum_channel_error, error);
                pixel_maximum_error = qMax(pixel_maximum_error, error);
                if (channel == 3)
                    metrics.alpha_error_sum += quint64(error);
            }
            if (pixel_maximum_error > GlobeHeatmapValidationTolerance)
                ++metrics.pixels_over_tolerance;

            const int alpha = gpu_channels[3];
            if (gpu_channels[0] > alpha + 1
                || gpu_channels[1] > alpha + 1
                || gpu_channels[2] > alpha + 1)
            {
                ++metrics.premultiplied_violations;
            }
        }
    }
    return metrics;
}

void reportGlobeHeatmapGpuValidation(
    const QRhiReadbackResult &result, const QImage &cpu_reference,
    quint64 revision, int zoom, int tile_x, int tile_y, int stamp_count)
{
    const QImage cpu_image = cpu_reference.convertToFormat(
        QImage::Format_RGBA8888_Premultiplied);
    const QString format_name = globeHeatmapTextureFormatName(result.format);
    QString invalid_reason;
    if (cpu_image.isNull())
        invalid_reason = QStringLiteral("missing_cpu_reference");
    else if (result.format != QRhiTexture::RGBA8
             && result.format != QRhiTexture::BGRA8)
        invalid_reason = QStringLiteral("unsupported_format");
    else if (result.pixelSize != cpu_image.size())
        invalid_reason = QStringLiteral("pixel_size_mismatch");
    else
    {
        const qsizetype expected_bytes = qsizetype(cpu_image.width())
            * qsizetype(cpu_image.height()) * 4;
        if (result.data.size() < expected_bytes)
            invalid_reason = QStringLiteral("short_readback");
    }

    if (!invalid_reason.isEmpty())
    {
        qCDebug(globeHeatmapPerformanceLog).noquote().nospace()
            << "diagnostic_gpu_validation revision=" << revision
            << " tile=" << zoom << "/" << tile_x << "/" << tile_y
            << " stamps=" << stamp_count
            << " format=" << format_name
            << " size=" << result.pixelSize.width() << "x"
            << result.pixelSize.height()
            << " bytes=" << result.data.size()
            << " status=invalid reason=" << invalid_reason;
        return;
    }

    const GlobeHeatmapValidationMetrics direct =
        compareGlobeHeatmapPixels(
            cpu_image, result.data, result.format, false);
    const GlobeHeatmapValidationMetrics flipped =
        compareGlobeHeatmapPixels(
            cpu_image, result.data, result.format, true);
    const quint64 direct_channel_count =
        quint64(direct.active_pixels) * 4;
    const quint64 flipped_channel_count =
        quint64(flipped.active_pixels) * 4;
    const double direct_mean_error = direct_channel_count > 0
        ? double(direct.absolute_error_sum) / double(direct_channel_count)
        : 0.0;
    const double flipped_mean_error = flipped_channel_count > 0
        ? double(flipped.absolute_error_sum) / double(flipped_channel_count)
        : 0.0;
    const bool use_flipped =
        flipped.absolute_error_sum < direct.absolute_error_sum;
    const GlobeHeatmapValidationMetrics &selected = use_flipped
        ? flipped : direct;
    const quint64 selected_channel_count =
        quint64(selected.active_pixels) * 4;
    const double selected_mean_error = selected_channel_count > 0
        ? double(selected.absolute_error_sum) / double(selected_channel_count)
        : 0.0;
    const double selected_alpha_error = selected.active_pixels > 0
        ? double(selected.alpha_error_sum) / double(selected.active_pixels)
        : 0.0;
    const double pixels_over_tolerance_percent = selected.active_pixels > 0
        ? 100.0 * double(selected.pixels_over_tolerance)
            / double(selected.active_pixels)
        : 0.0;
    const double coverage_delta_percent =
        selected.cpu_covered_pixels > 0
        ? 100.0 * double(std::abs(
              selected.cpu_covered_pixels - selected.gpu_covered_pixels))
            / double(selected.cpu_covered_pixels)
        : (selected.gpu_covered_pixels > 0 ? 100.0 : 0.0);
    const bool compatible = selected.active_pixels > 0
        && selected_mean_error <= 3.0
        && selected_alpha_error <= 3.0
        && pixels_over_tolerance_percent <= 5.0
        && coverage_delta_percent <= 5.0
        && selected.premultiplied_violations == 0;

    qCDebug(globeHeatmapPerformanceLog).noquote().nospace()
        << "diagnostic_gpu_validation revision=" << revision
        << " tile=" << zoom << "/" << tile_x << "/" << tile_y
        << " stamps=" << stamp_count
        << " format=" << format_name
        << " selected_orientation="
        << (use_flipped ? "flip_y" : "direct")
        << " direct_mean_abs_error="
        << QString::number(direct_mean_error, 'f', 3)
        << " flip_y_mean_abs_error="
        << QString::number(flipped_mean_error, 'f', 3)
        << " mean_abs_error="
        << QString::number(selected_mean_error, 'f', 3)
        << " mean_alpha_error="
        << QString::number(selected_alpha_error, 'f', 3)
        << " max_abs_error=" << selected.maximum_channel_error
        << " active_pixels=" << selected.active_pixels
        << " cpu_covered_pixels=" << selected.cpu_covered_pixels
        << " gpu_covered_pixels=" << selected.gpu_covered_pixels
        << " coverage_delta_pct="
        << QString::number(coverage_delta_percent, 'f', 3)
        << " pixels_over_" << GlobeHeatmapValidationTolerance << "_pct="
        << QString::number(pixels_over_tolerance_percent, 'f', 3)
        << " premul_violations=" << selected.premultiplied_violations
        << " status=" << (compatible ? "compatible" : "mismatch");
}

quint64 globeHeatmapMarkerBucketKey(int bucket_x, int bucket_y)
{
    return (quint64(quint32(bucket_x)) << 32)
        | quint64(quint32(bucket_y));
}

int wrappedGlobeHeatmapBucketX(qint64 bucket_x, qint64 bucket_count)
{
    if (bucket_count <= 0)
        return 0;
    qint64 wrapped = bucket_x % bucket_count;
    if (wrapped < 0)
        wrapped += bucket_count;
    return int(wrapped);
}

// Each lazily-created Globe imagery or heatmap page has 256 RGBA8 layers.
// Layer 0 is a permanent sentinel, leaving 255 batchable tiles per page. At
// 256x256, each allocated page is 64 MiB when fully resident. Heatmap pages
// pack only tiles that contain actual overlay pixels, independently from the
// imagery-page layout.
constexpr int GlobeTileArrayLayerCount = 256;
constexpr int GlobeTileArrayUsableLayerCount = GlobeTileArrayLayerCount - 1;
// Visible fallback heatmaps are baked into a single horizontal strip. Keep
// the strip to 16 MiB at RGBA8 on backends supporting a 16384-wide texture;
// lower texture-size limits automatically reduce the number of slots.
constexpr int GlobeHeatmapGpuBakeAtlasMaximumSlots = 64;

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

// Every regular Globe tile uses the same row-major grid topology. Keep that
// topology in a retained UInt32 index buffer and let imagery/terrain updates
// replace only the compact unique-vertex range. The winding exactly matches
// the former expanded triangles: p00,p01,p10 then p10,p01,p11.
void appendIndexedGridIndices(
    QVector<quint32> *indices, qsizetype first_vertex, int cell_count)
{
    if (indices == nullptr || first_vertex < 0 || cell_count <= 0)
        return;

    const qsizetype grid_width_size = qsizetype(cell_count) + 1;
    const qsizetype last_vertex = first_vertex
        + grid_width_size * grid_width_size - 1;
    if (last_vertex > qsizetype(std::numeric_limits<quint32>::max()))
        return;

    const quint32 first = quint32(first_vertex);
    const quint32 grid_width = quint32(cell_count + 1);
    for (int row = 0; row < cell_count; ++row)
    {
        for (int column = 0; column < cell_count; ++column)
        {
            const quint32 p00 = first
                + quint32(row) * grid_width + quint32(column);
            const quint32 p10 = p00 + 1;
            const quint32 p01 = p00 + grid_width;
            const quint32 p11 = p01 + 1;
            indices->append(p00);
            indices->append(p01);
            indices->append(p10);
            indices->append(p10);
            indices->append(p01);
            indices->append(p11);
        }
    }
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
constexpr int GlobeTileArrayMaximumPageCount =
    (GlobeQuadtreeMaxLeaves + GlobeTileArrayUsableLayerCount - 1)
    / GlobeTileArrayUsableLayerCount;
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

bool MapRhiGlobeRenderer::setRenderOriginEcef(
    const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef)
{
    if (this->render_origin_ecef.x == origin_ecef.x
        && this->render_origin_ecef.y == origin_ecef.y
        && this->render_origin_ecef.z == origin_ecef.z)
    {
        return false;
    }

    this->render_origin_ecef = origin_ecef;

    // Globe terrain, polar caps, wireframe, and network must all use this
    // same coordinate frame. The camera origin is sticky, so this rebuild
    // happens only after a long translation rather than during ordinary
    // pan/orbit frames. Clearing request ids makes results produced for the
    // old origin harmless when the asynchronous worker later returns them.
    for (GlobeTile &tile : this->window_tiles)
        tile.terrain_mesh_request_id = 0;
    this->window_dirty = true;
    this->caps_built = false;
    this->cap_vertices.clear();
    this->cap_indices.clear();
    this->cap_tiles.clear();
    this->cap_vertex_upload_pending = true;
    this->cap_index_upload_pending = true;
    return true;
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

void MapRhiGlobeRenderer::setHeatmapOverlay(
    const QVector<HeatmapMarker> &markers, double radius_m, double solid_fraction)
{
    const double bounded_radius_m = qMax(0.0, radius_m);
    const double bounded_solid_fraction = qBound(0.0, solid_fraction, 0.9);
    const bool markers_changed = this->heatmap_markers != markers;
    bool marker_layout_changed =
        this->heatmap_markers.size() != markers.size();
    if (!marker_layout_changed)
    {
        for (int marker_index = 0;
             marker_index < markers.size(); ++marker_index)
        {
            const HeatmapMarker &old_marker =
                this->heatmap_markers.at(marker_index);
            const HeatmapMarker &new_marker = markers.at(marker_index);
            if (old_marker.render_id != new_marker.render_id
                || old_marker.longitude_deg != new_marker.longitude_deg
                || old_marker.latitude_deg != new_marker.latitude_deg)
            {
                marker_layout_changed = true;
                break;
            }
        }
    }
    const bool radius_changed = !qFuzzyCompare(
        1.0 + this->heatmap_radius_m, 1.0 + bounded_radius_m);
    const bool solid_fraction_changed = !qFuzzyCompare(
        1.0 + this->heatmap_solid_fraction,
        1.0 + bounded_solid_fraction);
    const bool style_changed = radius_changed || solid_fraction_changed;
    if (!markers_changed && !style_changed)
        return;

    if (markers_changed)
    {
        this->heatmap_markers = markers;
        this->heatmap_active_marker_count = int(std::count_if(
            this->heatmap_markers.cbegin(),
            this->heatmap_markers.cend(),
            [](const HeatmapMarker &marker)
        {
            return marker.active;
        }));
    }

    // Buckets and projected stamp layouts depend on marker coordinates, not
    // their colors or whether a result exists for the current timestep.
    // Simulation playback normally changes only those two properties, so
    // keep both retained across its high-frequency revisions.
    if (marker_layout_changed)
        rebuildHeatmapMarkerBuckets();
    if (marker_layout_changed || radius_changed)
    {
        ++this->heatmap_stamp_layout_revision;
        if (this->heatmap_stamp_layout_revision == 0)
            this->heatmap_stamp_layout_revision = 1;
    }
    this->heatmap_radius_m = bounded_radius_m;
    this->heatmap_solid_fraction = bounded_solid_fraction;
    // Every visible tile's heatmap_texture is compared against this value
    // in ensureHeatmapTexture() and regenerated if stale -- see that
    // function. A real change still invalidates visible tiles once, but
    // renderHeatmapTile() now consults the retained marker index instead of
    // scanning the complete network separately for every tile.
    ++this->heatmap_revision;
    if (this->heatmap_revision == 0)
        this->heatmap_revision = 1;
    this->heatmap_gpu_bake_jobs.clear();
    // Before the once-per-QRhi diagnostic has been scheduled there is
    // nothing useful to retain. Once scheduled, its fixed stamps and CPU
    // reference remain self-contained and must not be cancelled merely
    // because the simulation advances to the next color revision.
    if (!this->diagnostic_heatmap_gpu_validation_attempted)
    {
        this->diagnostic_heatmap_bake_instances.clear();
        this->diagnostic_heatmap_bake_cpu_reference = QImage();
        this->diagnostic_heatmap_bake_pending = false;
        this->diagnostic_heatmap_bake_revision = 0;
    }
    this->heatmap_array_draw_indices_dirty = true;
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


MapRhiGlobeRenderer::TileVertex MapRhiGlobeRenderer::makeTileVertex(
    double lon_deg, double lat_deg, float u, float v) const
{
    const GeoWgs84Ellipsoid::EcefPositionD position =
        GeoWgs84Ellipsoid::geodeticToEcefD(lon_deg, lat_deg, 0.0);
    TileVertex vertex;
    vertex.x = float(position.x - this->render_origin_ecef.x);
    vertex.y = float(position.y - this->render_origin_ecef.y);
    vertex.z = float(position.z - this->render_origin_ecef.z);
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
    cap.first_index = this->cap_indices.size();

    const TileVertex pole_vertex = makeTileVertex(0.0, pole_lat, 0.5f, 0.5f);
    this->cap_vertices.append(pole_vertex);
    for (int segment = 0; segment <= GlobePolarCapSegments; ++segment)
    {
        const double longitude_deg = -180.0
            + 360.0 * double(segment) / double(GlobePolarCapSegments);
        this->cap_vertices.append(
            makeTileVertex(longitude_deg, ring_lat, 0.5f, 0.5f));
    }

    const quint32 pole_index = quint32(cap.first_vertex);
    for (int segment = 0; segment < GlobePolarCapSegments; ++segment)
    {
        const quint32 ring0 = pole_index + 1 + quint32(segment);
        const quint32 ring1 = ring0 + 1;
        this->cap_indices.append(pole_index);
        if (north)
        {
            this->cap_indices.append(ring0);
            this->cap_indices.append(ring1);
        }
        else
        {
            this->cap_indices.append(ring1);
            this->cap_indices.append(ring0);
        }
    }

    cap.vertex_count = this->cap_vertices.size() - cap.first_vertex;
    cap.index_count = this->cap_indices.size() - cap.first_index;
    this->cap_tiles.append(cap);
}

void MapRhiGlobeRenderer::buildCaps()
{
    if (this->caps_built)
        return;

    this->cap_vertices.clear();
    this->cap_indices.clear();
    this->cap_tiles.clear();
    buildPolarCap(true);
    buildPolarCap(false);
    this->caps_built = true;
    this->cap_vertex_upload_pending = true;
    this->cap_index_upload_pending = true;
    if (this->wireframe_visible)
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
    qsizetype estimated_index_count = 0;
    for (const GlobeTile &tile : next_tiles)
    {
        const int subdivisions = !tile.terrain_key.isEmpty()
            ? qMax(1, tile.terrain_cell_count)
            : subdivisionsForZoom(tile.zoom);
        const qsizetype grid_width = qsizetype(subdivisions) + 1;
        estimated_vertex_count += grid_width * grid_width;
        estimated_index_count +=
            qsizetype(subdivisions) * qsizetype(subdivisions) * 6;
    }

    this->window_vertices.clear();
    this->window_vertices.reserve(estimated_vertex_count);
    this->window_indices.clear();
    this->window_indices.reserve(estimated_index_count);
    this->window_tiles.clear();
    this->window_tiles.reserve(next_tiles.size());

    for (GlobeTile &tile : next_tiles)
    {
        const bool terrain_enabled = !tile.terrain_key.isEmpty();
        tile.first_vertex = this->window_vertices.size();
        tile.first_index = this->window_indices.size();
        const int subdivisions = terrain_enabled
            ? qMax(1, tile.terrain_cell_count)
            : subdivisionsForZoom(tile.zoom);
        const int grid_width = subdivisions + 1;
        const int expected_vertex_count = grid_width * grid_width;
        const int expected_index_count = subdivisions * subdivisions * 6;

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
                && previous_tile.index_count == expected_index_count
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
                    request.globe_render_origin_x = this->render_origin_ecef.x;
                    request.globe_render_origin_y = this->render_origin_ecef.y;
                    request.globe_render_origin_z = this->render_origin_ecef.z;

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
                for (int row = 0; row <= subdivisions; ++row)
                {
                    const double v = double(row) / double(subdivisions);
                    const double latitude_deg = GeoWebMercator::tileYToLat(
                        double(tile.tile_y) + v, tile.zoom);

                    for (int column = 0; column <= subdivisions; ++column)
                    {
                        const double u = double(column) / double(subdivisions);
                        const double longitude_deg = GeoWebMercator::tileXToLon(
                            double(tile.virtual_x) + u, tile.zoom);
                        this->window_vertices.append(makeTileVertex(
                            longitude_deg, latitude_deg, float(u), float(v)));
                    }
                }
                tile.vertex_count = expected_vertex_count;
            }
        }

        appendIndexedGridIndices(
            &this->window_indices, tile.first_vertex, subdivisions);
        tile.index_count = this->window_indices.size() - tile.first_index;

        this->window_tiles.append(tile);
    }

    this->window_dirty = false;
    this->window_tiles_requested = false;
    this->window_vertex_upload_pending = true;
    this->window_index_upload_pending = true;
    // Keep the second layer stream lazy: most sessions never enable a
    // heatmap, so they should not allocate or clear one float per terrain
    // vertex merely because the Globe window changed.
    this->window_heatmap_array_layers.clear();
    this->heatmap_array_layer_upload_pending = true;
    this->tile_array_draw_indices_dirty = true;
    this->heatmap_array_draw_indices_dirty = true;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.restart();

    if (this->wireframe_visible)
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

void MapRhiGlobeRenderer::appendWireframeEdges(
    const QVector<TileVertex> &vertices, const QVector<quint32> &indices)
{
    for (qsizetype index = 0; index + 2 < indices.size(); index += 3)
    {
        const quint32 a_index = indices.at(index);
        const quint32 b_index = indices.at(index + 1);
        const quint32 c_index = indices.at(index + 2);
        if (qsizetype(a_index) >= vertices.size()
            || qsizetype(b_index) >= vertices.size()
            || qsizetype(c_index) >= vertices.size())
        {
            continue;
        }

        const TileVertex &a = vertices.at(a_index);
        const TileVertex &b = vertices.at(b_index);
        const TileVertex &c = vertices.at(c_index);
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
        (this->window_indices.size() + this->cap_indices.size()) * 2);
    appendWireframeEdges(this->window_vertices, this->window_indices);
    appendWireframeEdges(this->cap_vertices, this->cap_indices);
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

    std::map<QString, std::unique_ptr<TileResource>>::iterator iterator =
        this->tile_resources.begin();
    while (iterator != this->tile_resources.end())
    {
        if (keys_in_use.contains(iterator->first))
        {
            ++iterator;
        }
        else
        {
            releaseHeatmapArrayLayer(iterator->second.get());
            releaseTileArrayLayer(iterator->second.get());
            iterator = this->tile_resources.erase(iterator);
        }
    }

    trimUnusedTileArrayPages();
    trimUnusedHeatmapArrayPages();
}

bool MapRhiGlobeRenderer::arrayBatchingActive() const
{
    // Heatmaps extend this batching with a fused two-array pipeline, so
    // their activation does not disable imagery batching. The setting is
    // shared with flat RHI batching and remains a pure performance switch.
    if (!guiConfiguration().map_performance.array_batching_enabled
        || !this->array_pipeline
        || this->tile_array_pages.empty())
    {
        return false;
    }

    const TileArrayPage &first_page = this->tile_array_pages.front();
    return first_page.texture && first_page.bindings;
}

void MapRhiGlobeRenderer::setTileArrayReady(GlobeTile &tile, bool ready)
{
    // A heatmap-array draw is only valid on top of an imagery-array draw.
    // Preserve that invariant even when late validation disables imagery.
    if (!ready)
        setTileHeatmapArrayReady(tile, false);
    if (tile.array_ready == ready)
        return;

    tile.array_ready = ready;
    this->tile_array_draw_indices_dirty = true;
    this->heatmap_array_draw_indices_dirty = true;
}

void MapRhiGlobeRenderer::setTileHeatmapArrayReady(
    GlobeTile &tile, bool ready)
{
    if (tile.heatmap_array_ready == ready)
        return;

    tile.heatmap_array_ready = ready;
    this->heatmap_array_draw_indices_dirty = true;
}

void MapRhiGlobeRenderer::releaseTileArrayLayer(TileResource *resource)
{
    if (resource == nullptr)
        return;

    if (resource->array_page >= 0
        && resource->array_page < int(this->tile_array_pages.size())
        && resource->array_layer > 0
        && resource->array_layer < GlobeTileArrayLayerCount)
    {
        TileArrayPage &page = this->tile_array_pages[resource->array_page];
        if (!page.free_layers.contains(resource->array_layer))
            page.free_layers.append(resource->array_layer);
    }

    if (resource->array_page >= 0 || resource->array_layer >= 0)
    {
        this->tile_array_draw_indices_dirty = true;
        this->heatmap_array_draw_indices_dirty = true;
    }
    resource->array_page = -1;
    resource->array_layer = -1;
    resource->array_content_revision = 0;
}

void MapRhiGlobeRenderer::releaseHeatmapArrayLayer(TileResource *resource)
{
    if (resource == nullptr)
        return;

    if (resource->heatmap_array_page >= 0
        && resource->heatmap_array_page
            < int(this->heatmap_array_pages.size())
        && resource->heatmap_array_layer > 0
        && resource->heatmap_array_layer < GlobeTileArrayLayerCount)
    {
        HeatmapArrayPage &page =
            this->heatmap_array_pages[resource->heatmap_array_page];
        if (!page.free_layers.contains(resource->heatmap_array_layer))
            page.free_layers.append(resource->heatmap_array_layer);
    }

    if (resource->heatmap_array_page >= 0
        || resource->heatmap_array_layer >= 0)
    {
        this->heatmap_array_draw_indices_dirty = true;
    }
    resource->heatmap_array_page = -1;
    resource->heatmap_array_layer = -1;
    resource->heatmap_array_revision = 0;
}

void MapRhiGlobeRenderer::trimUnusedTileArrayPages()
{
    while (this->tile_array_pages.size() > 1)
    {
        const TileArrayPage &page = this->tile_array_pages.back();
        if (page.free_layers.size() != GlobeTileArrayUsableLayerCount)
            break;
        this->heatmap_array_draw_batches.clear();
        this->tile_array_pages.pop_back();
        this->tile_array_draw_indices_dirty = true;
        this->heatmap_array_draw_indices_dirty = true;
    }
}

void MapRhiGlobeRenderer::trimUnusedHeatmapArrayPages()
{
    while (this->heatmap_array_pages.size() > 1)
    {
        const HeatmapArrayPage &page = this->heatmap_array_pages.back();
        if (page.free_layers.size() != GlobeTileArrayUsableLayerCount)
            break;
        this->heatmap_array_draw_batches.clear();
        this->heatmap_array_pages.pop_back();
        this->heatmap_array_draw_indices_dirty = true;
    }
}

void MapRhiGlobeRenderer::resetWindowArrayLayers()
{
    bool vertices_changed = false;
    for (GlobeTile &tile : this->window_tiles)
    {
        setTileArrayReady(tile, false);
        setTileHeatmapArrayReady(tile, false);
    }
    for (TileVertex &vertex : this->window_vertices)
    {
        if (vertex.layer != 0.0f)
        {
            vertex.layer = 0.0f;
            vertices_changed = true;
        }
    }
    if (vertices_changed)
        this->window_vertex_upload_pending = true;

    if (!this->window_heatmap_array_layers.isEmpty())
    {
        // A future heatmap assignment lazily restores the correctly sized
        // zero-filled stream before stamping its first page-local layer.
        this->window_heatmap_array_layers.clear();
        this->heatmap_array_layer_upload_pending = true;
    }
    this->tile_array_draw_indices_dirty = true;
    this->heatmap_array_draw_indices_dirty = true;
}

void MapRhiGlobeRenderer::rebuildTileArrayDrawIndices()
{
    this->tile_array_draw_indices.clear();
    for (TileArrayPage &page : this->tile_array_pages)
    {
        page.first_draw_index = 0;
        page.draw_index_count = 0;
    }

    if (!arrayBatchingActive())
    {
        this->tile_array_draw_indices_dirty = false;
        this->tile_array_draw_index_upload_pending = false;
        return;
    }

    // Validate transient readiness before grouping. A bad/stale resource
    // reference must fall back to the per-tile renderer rather than being
    // skipped merely because array_ready was left true.
    for (GlobeTile &tile : this->window_tiles)
    {
        if (!tile.array_ready)
            continue;

        const bool valid_page_index = tile.resource != nullptr
            && tile.resource->array_page >= 0
            && tile.resource->array_page < int(this->tile_array_pages.size());
        bool valid_page = false;
        if (valid_page_index)
        {
            const TileArrayPage &page =
                this->tile_array_pages[tile.resource->array_page];
            valid_page = page.texture && page.bindings;
        }
        const bool valid_resource = valid_page
            && tile.resource->array_layer > 0
            && tile.resource->array_layer < GlobeTileArrayLayerCount;
        const bool valid_geometry = tile.first_index >= 0
            && tile.index_count > 0
            && qsizetype(tile.first_index) + tile.index_count
                <= this->window_indices.size();
        if (!valid_resource || !valid_geometry)
            setTileArrayReady(tile, false);
    }

    this->tile_array_draw_indices.reserve(this->window_indices.size());
    for (int page_index = 0;
         page_index < int(this->tile_array_pages.size()); ++page_index)
    {
        TileArrayPage &page = this->tile_array_pages[page_index];
        page.first_draw_index = this->tile_array_draw_indices.size();
        for (const GlobeTile &tile : this->window_tiles)
        {
            if (!tile.array_ready || tile.resource == nullptr
                || tile.resource->array_page != page_index)
            {
                continue;
            }

            const qsizetype destination_first =
                this->tile_array_draw_indices.size();
            this->tile_array_draw_indices.resize(
                destination_first + tile.index_count);
            std::copy_n(
                this->window_indices.constData() + tile.first_index,
                tile.index_count,
                this->tile_array_draw_indices.data() + destination_first);
        }
        page.draw_index_count =
            this->tile_array_draw_indices.size() - page.first_draw_index;
    }

    this->tile_array_draw_indices_dirty = false;
    this->tile_array_draw_index_upload_pending =
        !this->tile_array_draw_indices.isEmpty();
}

bool MapRhiGlobeRenderer::uploadTileArrayDrawIndices(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource_updates == nullptr)
        return false;
    if (this->tile_array_draw_indices_dirty)
        rebuildTileArrayDrawIndices();
    if (!this->tile_array_draw_index_upload_pending)
        return true;

    if (this->tile_array_draw_indices.isEmpty())
    {
        this->tile_array_draw_index_upload_pending = false;
        return true;
    }

    const int required_bytes = int(
        this->tile_array_draw_indices.size() * qsizetype(sizeof(quint32)));
    if (!this->tile_array_draw_index_buffer
        || this->tile_array_draw_index_buffer_size < required_bytes)
    {
        // Membership grows incrementally while imagery streams in. Reserve
        // enough room for the entire current window so each arriving tile
        // updates this buffer instead of destroying and recreating it.
        const int allocation_bytes = qMax(
            required_bytes,
            int(this->window_indices.size() * qsizetype(sizeof(quint32))));
        this->tile_array_draw_index_buffer.reset(this->rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, allocation_bytes));
        if (!this->tile_array_draw_index_buffer
            || !this->tile_array_draw_index_buffer->create())
        {
            return false;
        }
        this->tile_array_draw_index_buffer_size = allocation_bytes;
    }

    resource_updates->updateDynamicBuffer(
        this->tile_array_draw_index_buffer.get(), 0, required_bytes,
        this->tile_array_draw_indices.constData());
    this->tile_array_draw_index_upload_pending = false;
    return true;
}

bool MapRhiGlobeRenderer::heatmapArrayBatchingActive() const
{
    if (!arrayBatchingActive()
        || this->heatmap_markers.isEmpty()
        || !this->heatmap_array_pipeline
        || !this->heatmap_array_template_bindings
        || this->heatmap_array_pages.empty())
    {
        return false;
    }

    const HeatmapArrayPage &first_page =
        this->heatmap_array_pages.front();
    return first_page.texture != nullptr;
}

bool MapRhiGlobeRenderer::rebuildHeatmapArrayDrawIndices()
{
    this->heatmap_array_draw_indices.clear();
    this->heatmap_array_draw_batches.clear();

    if (!heatmapArrayBatchingActive())
    {
        this->heatmap_array_draw_indices_dirty = false;
        this->heatmap_array_draw_index_upload_pending = false;
        return true;
    }

    bool has_visible_heatmap = false;
    for (GlobeTile &tile : this->window_tiles)
    {
        if (!tile.array_ready)
            continue;

        const bool valid_imagery_page_index = tile.resource != nullptr
            && tile.resource->array_page >= 0
            && tile.resource->array_page
                < int(this->tile_array_pages.size());
        bool valid_imagery_page = false;
        if (valid_imagery_page_index)
        {
            const TileArrayPage &page =
                this->tile_array_pages[tile.resource->array_page];
            valid_imagery_page = page.texture && page.bindings;
        }

        const bool has_heatmap = tile.resource != nullptr
            && tile.resource->heatmap_has_content;
        const bool valid_heatmap_page_index = has_heatmap
            && tile.resource->heatmap_array_page >= 0
            && tile.resource->heatmap_array_page
                < int(this->heatmap_array_pages.size());
        bool valid_heatmap_page = !has_heatmap;
        if (valid_heatmap_page_index)
        {
            const HeatmapArrayPage &page =
                this->heatmap_array_pages[
                    tile.resource->heatmap_array_page];
            valid_heatmap_page = page.texture != nullptr;
        }
        const bool valid_heatmap = valid_heatmap_page
            && (!has_heatmap
                || (tile.heatmap_array_ready
                    && tile.resource->heatmap_revision
                        == this->heatmap_revision
                    && tile.resource->heatmap_array_revision
                        == this->heatmap_revision
                    && tile.resource->heatmap_array_layer > 0
                    && tile.resource->heatmap_array_layer
                        < GlobeTileArrayLayerCount));
        const bool valid_resource = valid_imagery_page
            && valid_heatmap
            && tile.resource->array_layer > 0
            && tile.resource->array_layer < GlobeTileArrayLayerCount;
        const bool valid_geometry = tile.first_index >= 0
            && tile.index_count > 0
            && qsizetype(tile.first_index) + tile.index_count
                <= this->window_indices.size();
        if (!valid_resource || !valid_geometry)
        {
            setTileArrayReady(tile, false);
            continue;
        }
        if (has_heatmap)
            has_visible_heatmap = true;
    }

    if (!has_visible_heatmap)
    {
        this->heatmap_array_draw_indices_dirty = false;
        this->heatmap_array_draw_index_upload_pending = false;
        return true;
    }

    this->heatmap_array_draw_indices.reserve(this->window_indices.size());
    for (int imagery_page_index = 0;
         imagery_page_index < int(this->tile_array_pages.size());
         ++imagery_page_index)
    {
        for (int heatmap_page_index = 0;
             heatmap_page_index < int(this->heatmap_array_pages.size());
             ++heatmap_page_index)
        {
            const int first_draw_index =
                this->heatmap_array_draw_indices.size();
            for (const GlobeTile &tile : this->window_tiles)
            {
                if (!tile.array_ready || tile.resource == nullptr
                    || tile.resource->array_page != imagery_page_index)
                {
                    continue;
                }

                const int effective_heatmap_page =
                    tile.resource->heatmap_has_content
                    ? tile.resource->heatmap_array_page : 0;
                if (effective_heatmap_page != heatmap_page_index)
                    continue;

                const qsizetype destination_first =
                    this->heatmap_array_draw_indices.size();
                this->heatmap_array_draw_indices.resize(
                    destination_first + tile.index_count);
                std::copy_n(
                    this->window_indices.constData() + tile.first_index,
                    tile.index_count,
                    this->heatmap_array_draw_indices.data()
                        + destination_first);
            }

            const int draw_index_count =
                this->heatmap_array_draw_indices.size() - first_draw_index;
            if (draw_index_count <= 0)
                continue;

            HeatmapArrayDrawBatch batch;
            batch.first_draw_index = first_draw_index;
            batch.draw_index_count = draw_index_count;
            batch.bindings.reset(this->rhi->newShaderResourceBindings());
            if (!batch.bindings)
            {
                this->heatmap_array_draw_indices.clear();
                this->heatmap_array_draw_batches.clear();
                return false;
            }
            batch.bindings->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0, QRhiShaderResourceBinding::VertexStage
                        | QRhiShaderResourceBinding::FragmentStage,
                    this->camera_uniform_buffer.get()),
                QRhiShaderResourceBinding::sampledTexture(
                    1, QRhiShaderResourceBinding::FragmentStage,
                    this->tile_array_pages[imagery_page_index].texture.get(),
                    this->sampler.get()),
                QRhiShaderResourceBinding::sampledTexture(
                    2, QRhiShaderResourceBinding::FragmentStage,
                    this->heatmap_array_pages[heatmap_page_index].texture.get(),
                    this->sampler.get())
            });
            if (!batch.bindings->create())
            {
                this->heatmap_array_draw_indices.clear();
                this->heatmap_array_draw_batches.clear();
                return false;
            }
            this->heatmap_array_draw_batches.push_back(std::move(batch));
        }
    }

    this->heatmap_array_draw_indices_dirty = false;
    this->heatmap_array_draw_index_upload_pending =
        !this->heatmap_array_draw_indices.isEmpty();
    return true;
}

bool MapRhiGlobeRenderer::uploadHeatmapArrayDrawIndices(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource_updates == nullptr)
        return false;
    if (this->heatmap_array_draw_indices_dirty)
    {
        if (!rebuildHeatmapArrayDrawIndices())
            return false;
    }
    if (!this->heatmap_array_draw_index_upload_pending)
        return true;

    if (this->heatmap_array_draw_indices.isEmpty())
    {
        this->heatmap_array_draw_index_upload_pending = false;
        return true;
    }

    const int required_bytes = int(
        this->heatmap_array_draw_indices.size()
        * qsizetype(sizeof(quint32)));
    if (!this->heatmap_array_draw_index_buffer
        || this->heatmap_array_draw_index_buffer_size < required_bytes)
    {
        // The fused pass eventually contains every array-ready tile. Grow
        // geometrically while imagery arrives, capped at one full-window
        // index copy.
        const int growth_bytes = this->heatmap_array_draw_index_buffer_size
            + qMax(this->heatmap_array_draw_index_buffer_size / 2, 65536);
        const int maximum_bytes = int(
            this->window_indices.size() * qsizetype(sizeof(quint32)));
        const int allocation_bytes = qMin(
            maximum_bytes, qMax(required_bytes, growth_bytes));
        this->heatmap_array_draw_index_buffer.reset(this->rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, allocation_bytes));
        if (!this->heatmap_array_draw_index_buffer
            || !this->heatmap_array_draw_index_buffer->create())
        {
            return false;
        }
        this->heatmap_array_draw_index_buffer_size = allocation_bytes;
    }

    resource_updates->updateDynamicBuffer(
        this->heatmap_array_draw_index_buffer.get(), 0, required_bytes,
        this->heatmap_array_draw_indices.constData());
    this->heatmap_array_draw_index_upload_pending = false;
    return true;
}

bool MapRhiGlobeRenderer::uploadHeatmapArrayLayers(
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource_updates == nullptr)
        return false;
    if (!this->heatmap_array_layer_upload_pending)
        return true;

    if (this->window_heatmap_array_layers.isEmpty()
        || this->window_heatmap_array_layers.size()
            != this->window_vertices.size())
    {
        return false;
    }

    const int required_bytes = int(
        this->window_heatmap_array_layers.size()
        * qsizetype(sizeof(float)));
    if (!this->heatmap_array_layer_buffer
        || this->heatmap_array_layer_buffer_size != required_bytes)
    {
        this->heatmap_array_layer_buffer.reset(this->rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
            required_bytes));
        if (!this->heatmap_array_layer_buffer
            || !this->heatmap_array_layer_buffer->create())
        {
            return false;
        }
        this->heatmap_array_layer_buffer_size = required_bytes;
    }

    resource_updates->updateDynamicBuffer(
        this->heatmap_array_layer_buffer.get(), 0, required_bytes,
        this->window_heatmap_array_layers.constData());
    this->heatmap_array_layer_upload_pending = false;
    return true;
}

QImage MapRhiGlobeRenderer::currentTileArrayImage(
    const GlobeTile &tile, const TileResource &resource) const
{
    if (this->tile_repository == nullptr || tile.imagery_key.isEmpty())
        return QImage();

    if (!resource.is_provisional)
    {
        const QPixmap *pixmap = this->tile_repository->tile(tile.imagery_key);
        if (pixmap == nullptr || pixmap->isNull()
            || pixmap->cacheKey() != resource.pixmap_cache_key)
        {
            return QImage();
        }
        return pixmap->toImage().convertToFormat(QImage::Format_RGBA8888);
    }

    const QString children_key = QStringLiteral("children:%1/%2/%3")
        .arg(tile.zoom).arg(tile.tile_x).arg(tile.tile_y);
    if (resource.provisional_source_key == children_key)
    {
        const int child_zoom = tile.zoom + 1;
        const int child_x0 = tile.tile_x * 2;
        const int child_y0 = tile.tile_y * 2;
        QImage child_images[2][2];
        int child_w = 0;
        int child_h = 0;
        for (int dx = 0; dx < 2; ++dx)
        {
            for (int dy = 0; dy < 2; ++dy)
            {
                const QString child_key = this->map_model->tileCacheKeyAtZoom(
                    child_x0 + dx, child_y0 + dy, child_zoom);
                const QPixmap *child_pixmap = this->tile_repository->tile(child_key);
                if (child_pixmap == nullptr || child_pixmap->isNull())
                    return QImage();

                child_images[dx][dy] = child_pixmap->toImage().convertToFormat(
                    QImage::Format_RGBA8888);
                if (child_images[dx][dy].isNull())
                    return QImage();
                child_w = qMax(child_w, child_images[dx][dy].width());
                child_h = qMax(child_h, child_images[dx][dy].height());
            }
        }

        if (child_w <= 0 || child_h <= 0)
            return QImage();

        QImage composite(child_w * 2, child_h * 2, QImage::Format_RGBA8888);
        QPainter painter(&composite);
        for (int dx = 0; dx < 2; ++dx)
        {
            for (int dy = 0; dy < 2; ++dy)
            {
                painter.drawImage(
                    QRect(dx * child_w, dy * child_h, child_w, child_h),
                    child_images[dx][dy]);
            }
        }
        painter.end();
        return composite;
    }

    for (int levels_up = 1; tile.zoom - levels_up >= 0; ++levels_up)
    {
        const int ancestor_zoom = tile.zoom - levels_up;
        const quint32 ancestor_x = quint32(tile.tile_x) >> levels_up;
        const quint32 ancestor_y = quint32(tile.tile_y) >> levels_up;
        const QString ancestor_key = this->map_model->tileCacheKeyAtZoom(
            int(ancestor_x), int(ancestor_y), ancestor_zoom);
        if (ancestor_key != resource.provisional_source_key)
            continue;

        const QPixmap *ancestor_pixmap = this->tile_repository->tile(ancestor_key);
        if (ancestor_pixmap == nullptr || ancestor_pixmap->isNull())
            return QImage();

        const int span = 1 << levels_up;
        const int local_x = tile.tile_x & (span - 1);
        const int local_y = tile.tile_y & (span - 1);
        const int source_w = ancestor_pixmap->width();
        const int source_h = ancestor_pixmap->height();
        const QRect crop_rect(
            local_x * source_w / span, local_y * source_h / span,
            qMax(1, source_w / span), qMax(1, source_h / span));
        return ancestor_pixmap->copy(crop_rect)
            .toImage().convertToFormat(QImage::Format_RGBA8888)
            .scaled(source_w, source_h, Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
    }

    return QImage();
}

bool MapRhiGlobeRenderer::stampTileArrayLayer(
    GlobeTile &tile, const TileResource &resource,
    QRhiResourceUpdateBatch *resource_updates)
{
    if (resource.array_layer < 0 || tile.first_vertex < 0
        || tile.vertex_count <= 0
        || qsizetype(tile.first_vertex) + tile.vertex_count
            > this->window_vertices.size())
    {
        return false;
    }

    const float expected_layer = float(resource.array_layer);
    if (this->window_vertices.at(tile.first_vertex).layer == expected_layer)
        return true;

    for (int index = 0; index < tile.vertex_count; ++index)
        this->window_vertices[tile.first_vertex + index].layer = expected_layer;

    // Geometry rebuilds are followed by a full upload later in prepare().
    // Once a stable buffer exists, only patch this tile's contiguous range
    // when its imagery first acquires an array layer.
    if (resource_updates != nullptr && this->window_vertex_buffer
        && !this->window_vertex_upload_pending)
    {
        const int byte_offset = int(
            qsizetype(tile.first_vertex) * qsizetype(sizeof(TileVertex)));
        const int byte_count = int(
            qsizetype(tile.vertex_count) * qsizetype(sizeof(TileVertex)));
        resource_updates->updateDynamicBuffer(
            this->window_vertex_buffer.get(), byte_offset, byte_count,
            this->window_vertices.constData() + tile.first_vertex);
    }
    return true;
}

bool MapRhiGlobeRenderer::ensureTileArrayLayer(
    GlobeTile &tile, const QImage &updated_image,
    QRhiResourceUpdateBatch *resource_updates)
{
    if (!arrayBatchingActive() || tile.is_cap || tile.resource == nullptr
        || !tile.resource->texture || resource_updates == nullptr)
    {
        setTileArrayReady(tile, false);
        return true;
    }

    TileResource *resource = tile.resource;
    bool valid_assignment = resource->array_page >= 0
        && resource->array_page < int(this->tile_array_pages.size())
        && resource->array_layer > 0
        && resource->array_layer < GlobeTileArrayLayerCount;
    if (valid_assignment)
    {
        const TileArrayPage &page =
            this->tile_array_pages[resource->array_page];
        valid_assignment = page.texture && page.bindings;
    }
    if (!valid_assignment
        && (resource->array_page >= 0 || resource->array_layer >= 0))
    {
        releaseTileArrayLayer(resource);
    }

    bool assigned_now = false;
    if (resource->array_page < 0)
    {
        int page_index = -1;
        for (int index = 0;
             index < int(this->tile_array_pages.size()); ++index)
        {
            if (!this->tile_array_pages[index].free_layers.isEmpty())
            {
                page_index = index;
                break;
            }
        }

        if (page_index < 0
            && int(this->tile_array_pages.size())
                < GlobeTileArrayMaximumPageCount
            && createTileArrayPage())
        {
            page_index = int(this->tile_array_pages.size()) - 1;
        }
        if (page_index < 0)
        {
            setTileArrayReady(tile, false);
            return true;
        }

        TileArrayPage &page = this->tile_array_pages[page_index];
        resource->array_page = page_index;
        resource->array_layer = page.free_layers.takeLast();
        resource->array_content_revision = 0;
        assigned_now = true;
    }

    if (resource->content_revision == 0)
    {
        if (assigned_now)
            releaseTileArrayLayer(resource);
        setTileArrayReady(tile, false);
        return true;
    }

    if (resource->array_content_revision != resource->content_revision)
    {
        QImage image = updated_image;
        if (image.isNull())
            image = currentTileArrayImage(tile, *resource);
        if (image.isNull())
        {
            if (assigned_now)
                releaseTileArrayLayer(resource);
            setTileArrayReady(tile, false);
            return true;
        }

        const QSize layer_size(MapModel::TileSize, MapModel::TileSize);
        if (image.size() != layer_size)
        {
            image = image.scaled(
                layer_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        if (image.isNull())
        {
            if (assigned_now)
                releaseTileArrayLayer(resource);
            setTileArrayReady(tile, false);
            return true;
        }

        const QRhiTextureSubresourceUploadDescription subresource(image);
        const QRhiTextureUploadEntry entry(
            resource->array_layer, 0, subresource);
        TileArrayPage &page = this->tile_array_pages[resource->array_page];
        resource_updates->uploadTexture(
            page.texture.get(), QRhiTextureUploadDescription(entry));
        resource->array_content_revision = resource->content_revision;
    }

    if (!stampTileArrayLayer(tile, *resource, resource_updates))
    {
        setTileArrayReady(tile, false);
        return true;
    }
    setTileArrayReady(tile, true);
    return true;
}

bool MapRhiGlobeRenderer::stampTileHeatmapArrayLayer(
    GlobeTile &tile, int layer,
    QRhiResourceUpdateBatch *resource_updates)
{
    if (layer < 0 || tile.first_vertex < 0
        || tile.vertex_count <= 0
        || qsizetype(tile.first_vertex) + tile.vertex_count
            > this->window_vertices.size())
    {
        return false;
    }

    if (this->window_heatmap_array_layers.size()
        != this->window_vertices.size())
    {
        this->window_heatmap_array_layers.fill(
            0.0f, this->window_vertices.size());
        this->heatmap_array_layer_upload_pending = true;
    }

    const float expected_layer = float(layer);
    if (this->window_heatmap_array_layers.at(tile.first_vertex)
        == expected_layer)
    {
        return true;
    }

    for (int index = 0; index < tile.vertex_count; ++index)
    {
        this->window_heatmap_array_layers[tile.first_vertex + index] =
            expected_layer;
    }

    if (resource_updates != nullptr && this->heatmap_array_layer_buffer
        && !this->heatmap_array_layer_upload_pending)
    {
        const int byte_offset = int(
            qsizetype(tile.first_vertex) * qsizetype(sizeof(float)));
        const int byte_count = int(
            qsizetype(tile.vertex_count) * qsizetype(sizeof(float)));
        resource_updates->updateDynamicBuffer(
            this->heatmap_array_layer_buffer.get(), byte_offset, byte_count,
            this->window_heatmap_array_layers.constData()
                + tile.first_vertex);
    }
    return true;
}

bool MapRhiGlobeRenderer::ensureTileHeatmapArray(
    GlobeTile &tile, const QImage &updated_image,
    const QVector<HeatmapStamp> &updated_stamps,
    QRhiResourceUpdateBatch *resource_updates)
{
    ScopedHeatmapProfileTimer profile_timer(
        &this->heatmap_profile.cpu_ns, this->heatmap_profile.enabled);
    if (tile.resource == nullptr)
    {
        setTileHeatmapArrayReady(tile, false);
        return true;
    }

    TileResource *resource = tile.resource;
    if (!resource->heatmap_has_content)
    {
        if (!this->window_heatmap_array_layers.isEmpty()
            && !stampTileHeatmapArrayLayer(tile, 0, resource_updates))
        {
            return false;
        }
        releaseHeatmapArrayLayer(resource);
        setTileHeatmapArrayReady(tile, false);
        return true;
    }

    if (!tile.array_ready || resource_updates == nullptr
        || !createHeatmapArrayResources())
    {
        // An affected tile must retain the ordinary combined
        // imagery/heatmap draw when the heatmap array path is unavailable.
        setTileHeatmapArrayReady(tile, false);
        if (tile.array_ready)
            setTileArrayReady(tile, false);
        return true;
    }

    bool valid_assignment = resource->heatmap_array_page >= 0
        && resource->heatmap_array_page
            < int(this->heatmap_array_pages.size())
        && resource->heatmap_array_layer > 0
        && resource->heatmap_array_layer < GlobeTileArrayLayerCount;
    if (valid_assignment)
    {
        const HeatmapArrayPage &page =
            this->heatmap_array_pages[resource->heatmap_array_page];
        valid_assignment = page.texture != nullptr;
    }
    if (!valid_assignment
        && (resource->heatmap_array_page >= 0
            || resource->heatmap_array_layer >= 0))
    {
        releaseHeatmapArrayLayer(resource);
    }

    bool assigned_now = false;
    if (resource->heatmap_array_page < 0)
    {
        int page_index = -1;
        for (int index = 0;
             index < int(this->heatmap_array_pages.size()); ++index)
        {
            if (!this->heatmap_array_pages[index].free_layers.isEmpty())
            {
                page_index = index;
                break;
            }
        }

        if (page_index < 0
            && int(this->heatmap_array_pages.size())
                < GlobeTileArrayMaximumPageCount
            && createHeatmapArrayPage())
        {
            page_index = int(this->heatmap_array_pages.size()) - 1;
        }
        if (page_index < 0)
        {
            setTileHeatmapArrayReady(tile, false);
            setTileArrayReady(tile, false);
            return true;
        }

        HeatmapArrayPage &page = this->heatmap_array_pages[page_index];
        resource->heatmap_array_page = page_index;
        resource->heatmap_array_layer = page.free_layers.takeLast();
        resource->heatmap_array_revision = 0;
        assigned_now = true;
    }

    if (!stampTileHeatmapArrayLayer(
            tile, resource->heatmap_array_layer, resource_updates))
    {
        setTileHeatmapArrayReady(tile, false);
        setTileArrayReady(tile, false);
        return true;
    }

    if (resource->heatmap_array_revision != this->heatmap_revision)
    {
        if (!this->heatmap_gpu_baking_disabled)
        {
            QVector<HeatmapStamp> regenerated_stamps;
            const QVector<HeatmapStamp> *stamps = &updated_stamps;
            if (stamps->isEmpty())
            {
                regenerated_stamps = heatmapStampsForTileProfiled(
                    tile, resource);
                stamps = &regenerated_stamps;
            }

            if (stamps->isEmpty())
            {
                if (!this->window_heatmap_array_layers.isEmpty()
                    && !stampTileHeatmapArrayLayer(
                        tile, 0, resource_updates))
                {
                    return false;
                }
                resource->heatmap_has_content = false;
                resource->heatmap_revision = this->heatmap_revision;
                releaseHeatmapArrayLayer(resource);
                setTileHeatmapArrayReady(tile, false);
                return true;
            }

            HeatmapArrayPage &page = this->heatmap_array_pages[
                resource->heatmap_array_page];
            if (ensureDiagnosticHeatmapGpuBakeResources()
                && queueHeatmapGpuBake(
                    resource, page.texture.get(), *stamps,
                    resource->heatmap_array_layer))
            {
                // Reserve both revisions so the fused draw list built later
                // in prepare() can include this tile. A discarded frame or
                // failed bake rolls them back from the retained job queue;
                // runPendingHeatmapGpuBakes() confirms them after recording
                // the atlas-to-layer copy.
                resource->heatmap_array_revision = this->heatmap_revision;
                resource->heatmap_revision = this->heatmap_revision;
                setTileHeatmapArrayReady(tile, true);
                return true;
            }

            disableHeatmapGpuBaking();
        }

        // Automatic failure path only: preserve the established CPU raster
        // and host upload if offscreen baking or texture copies are not
        // available on this backend.
        QImage image = updated_image;
        if (image.isNull())
            image = renderHeatmapTileProfiled(tile, resource);
        if (!image.isNull())
        {
            image = image.convertToFormat(
                QImage::Format_RGBA8888_Premultiplied);
        }
        const QSize layer_size(
            GlobeHeatmapTextureSize, GlobeHeatmapTextureSize);
        if (!image.isNull() && image.size() != layer_size)
        {
            image = image.scaled(
                layer_size, Qt::IgnoreAspectRatio,
                Qt::SmoothTransformation);
        }
        if (image.isNull())
        {
            if (assigned_now)
                releaseHeatmapArrayLayer(resource);
            setTileHeatmapArrayReady(tile, false);
            setTileArrayReady(tile, false);
            return true;
        }

        const QRhiTextureSubresourceUploadDescription subresource(image);
        const QRhiTextureUploadEntry entry(
            resource->heatmap_array_layer, 0, subresource);
        HeatmapArrayPage &page =
            this->heatmap_array_pages[resource->heatmap_array_page];
        resource_updates->uploadTexture(
            page.texture.get(), QRhiTextureUploadDescription(entry));
        if (this->heatmap_profile.enabled)
        {
            ++this->heatmap_profile.array_uploads;
            this->heatmap_profile.upload_bytes += quint64(image.sizeInBytes());
        }
        resource->heatmap_array_revision = this->heatmap_revision;
        resource->heatmap_revision = this->heatmap_revision;
    }

    setTileHeatmapArrayReady(tile, true);
    return true;
}

bool MapRhiGlobeRenderer::rebuildTileBindings(TileResource *resource)
{
    resource->bindings.reset(this->rhi->newShaderResourceBindings());
    if (!resource->bindings)
        return false;

    // heatmap_texture is null for the overwhelming majority of tiles at
    // any given moment (only tiles within heatmap_radius_m of an actual
    // marker ever get one -- see renderHeatmapTile()'s bounding check), so
    // this falls back to heatmap_dummy_texture (fully transparent) far
    // more often than not. GlobeCameraBlock's heatmap_settings.y (opacity)
    // being 0 whenever no heatmap is active at all (see prepare()) would
    // already make map_rhi_globe.frag's blend a no-op even without this,
    // but binding *something* valid is still required.
    QRhiTexture *heatmap_texture = resource->heatmap_texture
        ? resource->heatmap_texture.get()
        : this->heatmap_dummy_texture.get();

    resource->bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage
                | QRhiShaderResourceBinding::FragmentStage,
            this->camera_uniform_buffer.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage,
            resource->texture.get(), this->sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(
            2, QRhiShaderResourceBinding::FragmentStage,
            heatmap_texture, this->sampler.get())
    });
    return resource->bindings->create();
}

bool MapRhiGlobeRenderer::ensureTileResource(
    GlobeTile &tile, QRhiResourceUpdateBatch *resource_updates,
    QImage *updated_image, QImage *updated_heatmap_image,
    QVector<HeatmapStamp> *updated_heatmap_stamps)
{
    if (updated_image != nullptr)
        *updated_image = QImage();
    if (updated_heatmap_image != nullptr)
        *updated_heatmap_image = QImage();
    if (updated_heatmap_stamps != nullptr)
        updated_heatmap_stamps->clear();

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

    std::unique_ptr<TileResource> &slot = this->tile_resources[tile.imagery_key];
    if (!slot)
        slot = std::make_unique<TileResource>();
    TileResource *resource = slot.get();

    if (pixmap == nullptr)
    {
        if (!ensureProvisionalTileResource(
                tile, resource, resource_updates, updated_image))
            return false;
        // tile.resource is only actually set on some of
        // ensureProvisionalTileResource()'s success paths -- see its own
        // comment on the "nothing to derive a placeholder from yet" case,
        // which deliberately leaves it untouched so draw() falls back to
        // template_bindings instead. No heatmap texture to prepare for a
        // tile that isn't even going to use this resource.
        if (tile.resource == resource
            && !ensureHeatmapTexture(
                tile, resource, resource_updates,
                updated_heatmap_image, updated_heatmap_stamps,
                !arrayBatchingActive()))
        {
            return false;
        }
        return true;
    }

    const qint64 cache_key = pixmap->cacheKey();
    if (!resource->texture || resource->is_provisional || resource->pixmap_cache_key != cache_key)
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
        resource->is_provisional = false;
        resource->provisional_source_key.clear();
        ++resource->content_revision;
        if (resource->content_revision == 0)
            resource->content_revision = 1;
        if (updated_image != nullptr)
            *updated_image = image;
    }

    if (!resource->bindings && !rebuildTileBindings(resource))
        return false;

    if (!ensureHeatmapTexture(
            tile, resource, resource_updates, updated_heatmap_image,
            updated_heatmap_stamps,
            !arrayBatchingActive()))
        return false;

    tile.resource = resource;
    return true;
}

// Falls back to a placeholder derived from already-loaded neighboring tiles
// while tile's own imagery is still in flight, instead of the flat
// GlobeMissingTileColor fill -- the same "keep showing something real
// instead of a blank/flat placeholder" goal the flat 2D/3D basemap
// renderer's parent/child LOD handoff serves. The resulting image is kept
// in the ordinary per-tile texture and, when a layer is available, uploaded
// into the shared Globe array as well.
//
// Tries two directions, in this order:
//
// 1. Descendants (zooming OUT): the tile being merged into is brand new
//    and was never itself fetched before -- only its children were, at the
//    finer zoom the camera is pulling back from. If all four direct
//    children are already cached, compose them into a 2x2 mosaic. This is
//    likely *better* detail than the real coarse tile will eventually have
//    (assembled from 4x the resolution), and it's almost certainly the
//    exact same imagery that was already on screen a moment ago. Only
//    checks direct children, not grandchildren -- covers ordinary one-step
//    zoom-out; a large jump that skips levels falls through to (2).
// 2. Ancestors (zooming IN): the tile being subdivided into already has a
//    loaded parent (or grandparent, ...) covering the same area at coarser
//    detail -- crop it to this tile's footprint and upscale.
//
// Cheap even though it can run every frame per still-loading tile: at most
// a handful of hash lookups plus, on the frame a composite/crop is first
// produced, one CPU image composite -- no network or disk I/O, and the
// result is cached on the resource (via is_provisional/provisional_source_key)
// so it isn't redone every frame while still waiting.
bool MapRhiGlobeRenderer::ensureProvisionalTileResource(
    GlobeTile &tile, TileResource *resource,
    QRhiResourceUpdateBatch *resource_updates, QImage *updated_image)
{
    const QString children_key = QStringLiteral("children:%1/%2/%3")
        .arg(tile.zoom).arg(tile.tile_x).arg(tile.tile_y);
    if (resource->is_provisional && resource->texture
        && resource->provisional_source_key == children_key)
    {
        tile.resource = resource;
        return true;
    }

    const int child_zoom = tile.zoom + 1;
    const int child_x0 = tile.tile_x * 2;
    const int child_y0 = tile.tile_y * 2;
    QImage child_images[2][2];
    int child_w = 0;
    int child_h = 0;
    bool all_children_cached = true;
    for (int dx = 0; dx < 2 && all_children_cached; ++dx)
    {
        for (int dy = 0; dy < 2 && all_children_cached; ++dy)
        {
            const QString child_key = this->map_model->tileCacheKeyAtZoom(
                child_x0 + dx, child_y0 + dy, child_zoom);
            const QPixmap *child_pixmap = this->tile_repository->tile(child_key);
            if (child_pixmap == nullptr || child_pixmap->isNull())
            {
                all_children_cached = false;
                break;
            }

            const QImage image = child_pixmap->toImage().convertToFormat(QImage::Format_RGBA8888);
            if (image.isNull())
            {
                all_children_cached = false;
                break;
            }

            child_images[dx][dy] = image;
            child_w = qMax(child_w, image.width());
            child_h = qMax(child_h, image.height());
        }
    }

    if (all_children_cached && child_w > 0 && child_h > 0)
    {
        QImage composite(child_w * 2, child_h * 2, QImage::Format_RGBA8888);
        QPainter painter(&composite);
        for (int dx = 0; dx < 2; ++dx)
        {
            for (int dy = 0; dy < 2; ++dy)
            {
                // Tile addressing here is the same XYZ scheme used
                // throughout (Y increasing southward, matching image row
                // order), so child (dx, dy) maps directly onto quadrant
                // (dx, dy) of the composite with no flip.
                painter.drawImage(
                    QRect(dx * child_w, dy * child_h, child_w, child_h), child_images[dx][dy]);
            }
        }
        painter.end();

        resource->bindings.reset();
        resource->texture.reset(this->rhi->newTexture(QRhiTexture::RGBA8, composite.size()));
        if (!resource->texture || !resource->texture->create())
            return false;
        resource_updates->uploadTexture(resource->texture.get(), composite);
        resource->pixmap_cache_key = -1;
        resource->is_provisional = true;
        resource->provisional_source_key = children_key;
        ++resource->content_revision;
        if (resource->content_revision == 0)
            resource->content_revision = 1;
        if (updated_image != nullptr)
            *updated_image = composite;

        if (!resource->bindings && !rebuildTileBindings(resource))
            return false;

        tile.resource = resource;
        return true;
    }

    for (int levels_up = 1; tile.zoom - levels_up >= 0; ++levels_up)
    {
        const int ancestor_zoom = tile.zoom - levels_up;
        const quint32 ancestor_x = quint32(tile.tile_x) >> levels_up;
        const quint32 ancestor_y = quint32(tile.tile_y) >> levels_up;
        const QString ancestor_key = this->map_model->tileCacheKeyAtZoom(
            int(ancestor_x), int(ancestor_y), ancestor_zoom);
        const QPixmap *ancestor_pixmap = this->tile_repository->tile(ancestor_key);
        if (ancestor_pixmap == nullptr || ancestor_pixmap->isNull())
            continue;

        // Already showing exactly this ancestor from a previous frame --
        // nothing to re-derive.
        if (resource->is_provisional && resource->texture
            && resource->provisional_source_key == ancestor_key)
        {
            tile.resource = resource;
            return true;
        }

        const int span = 1 << levels_up;
        const int local_x = tile.tile_x & (span - 1);
        const int local_y = tile.tile_y & (span - 1);
        const int source_w = ancestor_pixmap->width();
        const int source_h = ancestor_pixmap->height();
        const QRect crop_rect(
            local_x * source_w / span, local_y * source_h / span,
            qMax(1, source_w / span), qMax(1, source_h / span));
        const QImage fallback_image = ancestor_pixmap->copy(crop_rect)
            .toImage().convertToFormat(QImage::Format_RGBA8888)
            .scaled(source_w, source_h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (fallback_image.isNull())
            continue;

        resource->bindings.reset();
        resource->texture.reset(this->rhi->newTexture(QRhiTexture::RGBA8, fallback_image.size()));
        if (!resource->texture || !resource->texture->create())
            return false;
        resource_updates->uploadTexture(resource->texture.get(), fallback_image);
        resource->pixmap_cache_key = -1;
        resource->is_provisional = true;
        resource->provisional_source_key = ancestor_key;
        ++resource->content_revision;
        if (resource->content_revision == 0)
            resource->content_revision = 1;
        if (updated_image != nullptr)
            *updated_image = fallback_image;

        if (!resource->bindings && !rebuildTileBindings(resource))
            return false;

        tile.resource = resource;
        return true;
    }

    // Neither direct children nor any ancestor are loaded yet (e.g. the
    // very first tiles requested right after startup, or a fresh area with
    // nothing cached at any nearby zoom) -- nothing to derive a placeholder
    // from. Leaving tile.resource untouched here falls through to
    // template_bindings (the flat GlobeMissingTileColor fill) at draw
    // time, exactly as before this fallback existed.
    return true;
}

void MapRhiGlobeRenderer::rebuildHeatmapMarkerBuckets()
{
    this->heatmap_marker_projections.clear();
    this->heatmap_marker_projections.resize(this->heatmap_markers.size());
    this->heatmap_marker_buckets_by_zoom.clear();
    this->heatmap_marker_buckets_by_zoom.resize(
        GlobeHeatmapMarkerMaximumBucketZoom + 1);
    for (QHash<quint64, QVector<int>> &buckets
         : this->heatmap_marker_buckets_by_zoom)
    {
        buckets.reserve(this->heatmap_markers.size());
    }

    for (int marker_index = 0;
         marker_index < this->heatmap_markers.size(); ++marker_index)
    {
        const HeatmapMarker &marker =
            this->heatmap_markers.at(marker_index);
        if (!std::isfinite(marker.longitude_deg)
            || !std::isfinite(marker.latitude_deg))
        {
            continue;
        }

        const double marker_tile_x_zoom0 = GeoWebMercator::lonToTileX(
            GeoWebMercator::normalizeLongitude(marker.longitude_deg),
            0);
        const double marker_tile_y_zoom0 = GeoWebMercator::latToTileY(
            marker.latitude_deg, 0);
        if (!std::isfinite(marker_tile_x_zoom0)
            || !std::isfinite(marker_tile_y_zoom0))
        {
            continue;
        }

        HeatmapMarkerProjection &projection =
            this->heatmap_marker_projections[marker_index];
        projection.tile_x_zoom0 = marker_tile_x_zoom0;
        projection.tile_y_zoom0 = marker_tile_y_zoom0;
        projection.valid = true;

        for (int bucket_zoom = 0;
             bucket_zoom <= GlobeHeatmapMarkerMaximumBucketZoom;
             ++bucket_zoom)
        {
            const qint64 bucket_count = qint64(1) << bucket_zoom;
            const double bucket_scale = double(bucket_count);
            const int bucket_x = wrappedGlobeHeatmapBucketX(
                qint64(std::floor(marker_tile_x_zoom0 * bucket_scale)),
                bucket_count);
            const int bucket_y = int(qBound(
                qint64(0),
                qint64(std::floor(
                    marker_tile_y_zoom0 * bucket_scale)),
                bucket_count - 1));
            this->heatmap_marker_buckets_by_zoom[bucket_zoom]
                [globeHeatmapMarkerBucketKey(bucket_x, bucket_y)]
                    .append(marker_index);
        }
    }
}

QVector<int> MapRhiGlobeRenderer::heatmapMarkerCandidates(
    const GlobeTile &tile, double radius_tile_fraction,
    int *visited_bucket_cells) const
{
    if (visited_bucket_cells != nullptr)
        *visited_bucket_cells = 0;

    QVector<int> result;
    if (this->heatmap_marker_projections.isEmpty()
        || this->heatmap_marker_buckets_by_zoom.isEmpty()
        || !std::isfinite(radius_tile_fraction)
        || radius_tile_fraction < 0.0)
    {
        return result;
    }

    const auto all_marker_indices = [this]()
    {
        QVector<int> indices;
        indices.reserve(this->heatmap_markers.size());
        for (int marker_index = 0;
             marker_index < this->heatmap_marker_projections.size();
             ++marker_index)
        {
            if (this->heatmap_marker_projections.at(marker_index).valid)
                indices.append(marker_index);
        }
        return indices;
    };

    int bucket_zoom = qBound(
        0, tile.zoom, GlobeHeatmapMarkerMaximumBucketZoom);
    double bucket_scale = std::ldexp(1.0, bucket_zoom - tile.zoom);
    double horizontal_span =
        (1.0 + 2.0 * radius_tile_fraction) * bucket_scale;
    if (!std::isfinite(bucket_scale) || bucket_scale <= 0.0
        || !std::isfinite(horizontal_span))
    {
        return all_marker_indices();
    }

    // Drop to a coarser index level until this radius-expanded tile covers
    // only a small rectangle of buckets. Every marker exists at every level,
    // so this never changes the candidate set's correctness -- only how many
    // empty QHash cells and coarse false positives are inspected.
    while (bucket_zoom > 0
           && horizontal_span > GlobeHeatmapMarkerTargetBucketSpan)
    {
        --bucket_zoom;
        bucket_scale *= 0.5;
        horizontal_span *= 0.5;
    }

    const qint64 bucket_count = qint64(1) << bucket_zoom;
    if (horizontal_span >= double(bucket_count))
        return all_marker_indices();
    const QHash<quint64, QVector<int>> &buckets =
        this->heatmap_marker_buckets_by_zoom.at(bucket_zoom);
    if (buckets.isEmpty())
        return result;

    const double minimum_bucket_x_value =
        (double(tile.virtual_x) - radius_tile_fraction) * bucket_scale;
    const double maximum_bucket_x_value =
        (double(tile.virtual_x) + 1.0 + radius_tile_fraction) * bucket_scale;
    const double minimum_bucket_y_value =
        (double(tile.tile_y) - radius_tile_fraction) * bucket_scale;
    const double maximum_bucket_y_value =
        (double(tile.tile_y) + 1.0 + radius_tile_fraction) * bucket_scale;
    if (!std::isfinite(minimum_bucket_x_value)
        || !std::isfinite(maximum_bucket_x_value)
        || !std::isfinite(minimum_bucket_y_value)
        || !std::isfinite(maximum_bucket_y_value))
    {
        return all_marker_indices();
    }

    const qint64 minimum_bucket_x =
        qint64(std::floor(minimum_bucket_x_value));
    const qint64 maximum_bucket_x =
        qint64(std::floor(maximum_bucket_x_value));
    const qint64 unclamped_minimum_bucket_y =
        qint64(std::floor(minimum_bucket_y_value));
    const qint64 unclamped_maximum_bucket_y =
        qint64(std::floor(maximum_bucket_y_value));
    if (unclamped_maximum_bucket_y < 0
        || unclamped_minimum_bucket_y >= bucket_count)
    {
        return result;
    }

    const qint64 minimum_bucket_y = qMax(
        qint64(0), unclamped_minimum_bucket_y);
    const qint64 maximum_bucket_y = qMin(
        bucket_count - 1,
        unclamped_maximum_bucket_y);
    const qint64 horizontal_bucket_count =
        maximum_bucket_x - minimum_bucket_x + 1;
    const qint64 vertical_bucket_count =
        maximum_bucket_y - minimum_bucket_y + 1;
    if (horizontal_bucket_count <= 0 || vertical_bucket_count <= 0)
        return result;
    if (horizontal_bucket_count >= bucket_count)
        return all_marker_indices();

    for (qint64 bucket_y = minimum_bucket_y;
         bucket_y <= maximum_bucket_y; ++bucket_y)
    {
        for (qint64 bucket_x = minimum_bucket_x;
             bucket_x <= maximum_bucket_x; ++bucket_x)
        {
            if (visited_bucket_cells != nullptr)
                ++*visited_bucket_cells;
            const quint64 key = globeHeatmapMarkerBucketKey(
                wrappedGlobeHeatmapBucketX(bucket_x, bucket_count),
                int(bucket_y));
            const QHash<quint64, QVector<int>>::const_iterator iterator =
                buckets.constFind(key);
            if (iterator == buckets.cend())
                continue;
            result.append(iterator.value());
        }
    }
    return result;
}

QImage MapRhiGlobeRenderer::renderHeatmapTileProfiled(
    const GlobeTile &tile, TileResource *resource)
{
    if (!this->heatmap_profile.enabled)
        return renderHeatmapTile(tile, resource, nullptr);

    QElapsedTimer timer;
    timer.start();
    HeatmapRasterStats stats;
    QVector<HeatmapStamp> diagnostic_stamps;
    QImage diagnostic_cpu_reference;
    QVector<HeatmapStamp> *rendered_stamps = nullptr;
    QImage *premultiplied_image = nullptr;
    if (!this->diagnostic_heatmap_gpu_validation_attempted)
    {
        rendered_stamps = &diagnostic_stamps;
        premultiplied_image = &diagnostic_cpu_reference;
    }
    QImage image = renderHeatmapTile(
        tile, resource, &stats, rendered_stamps, premultiplied_image);
    ++this->heatmap_profile.raster_calls;
    if (!image.isNull())
        ++this->heatmap_profile.raster_tiles_with_content;
    this->heatmap_profile.candidate_markers += stats.candidate_markers;
    this->heatmap_profile.candidate_bucket_cells +=
        stats.candidate_bucket_cells;
    this->heatmap_profile.marker_tile_pairs += stats.marker_tile_pairs;
    if (stats.stamp_layout_cache_hit)
        ++this->heatmap_profile.stamp_layout_cache_hits;
    else
        ++this->heatmap_profile.stamp_layout_cache_misses;
    this->heatmap_profile.raster_ns += timer.nsecsElapsed();
    if (!diagnostic_stamps.isEmpty())
    {
        scheduleDiagnosticHeatmapGpuBake(
            tile, diagnostic_stamps, diagnostic_cpu_reference);
    }
    return image;
}

QImage MapRhiGlobeRenderer::renderHeatmapTile(
    const GlobeTile &tile, TileResource *resource,
    HeatmapRasterStats *stats,
    QVector<HeatmapStamp> *rendered_stamps,
    QImage *premultiplied_image) const
{
    if (premultiplied_image != nullptr)
        *premultiplied_image = QImage();
    const QVector<HeatmapStamp> stamps = heatmapStampsForTile(
        tile, resource, stats);
    if (rendered_stamps != nullptr)
        *rendered_stamps = stamps;
    if (stamps.isEmpty())
        return QImage();

    const QImage image = renderHeatmapStamps(stamps);
    if (premultiplied_image != nullptr)
        *premultiplied_image = image;

    return image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
}

QImage MapRhiGlobeRenderer::renderHeatmapStamps(
    const QVector<HeatmapStamp> &stamps) const
{
    if (stamps.isEmpty())
        return QImage();

    QImage image(
        GlobeHeatmapTextureSize, GlobeHeatmapTextureSize,
        QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setPen(Qt::NoPen);

    const double half_fraction = this->heatmap_solid_fraction
        + (1.0 - this->heatmap_solid_fraction) * 0.4375;
    for (const HeatmapStamp &stamp : stamps)
    {
        const QPointF center_pixels(
            stamp.center_x_pixels, stamp.center_y_pixels);
        QColor full_color = stamp.color;
        full_color.setAlpha(255);
        QColor half_color = stamp.color;
        half_color.setAlpha(128);
        QColor edge_color = stamp.color;
        edge_color.setAlpha(0);

        QRadialGradient gradient(center_pixels, stamp.radius_pixels);
        gradient.setColorAt(0.0, full_color);
        if (this->heatmap_solid_fraction > 0.0)
            gradient.setColorAt(this->heatmap_solid_fraction, full_color);
        gradient.setColorAt(half_fraction, half_color);
        gradient.setColorAt(1.0, edge_color);
        painter.setBrush(gradient);
        painter.drawEllipse(
            center_pixels, stamp.radius_pixels, stamp.radius_pixels);
    }
    painter.end();
    return image;
}

QVector<MapRhiGlobeRenderer::HeatmapStamp>
MapRhiGlobeRenderer::heatmapStampsForTileProfiled(
    const GlobeTile &tile, TileResource *resource)
{
    QElapsedTimer timer;
    timer.start();
    HeatmapRasterStats stats;
    QVector<HeatmapStamp> stamps = heatmapStampsForTile(
        tile, resource, &stats);
    this->heatmap_profile.candidate_markers += stats.candidate_markers;
    this->heatmap_profile.candidate_bucket_cells +=
        stats.candidate_bucket_cells;
    this->heatmap_profile.marker_tile_pairs += stats.marker_tile_pairs;
    if (stats.stamp_layout_cache_hit)
        ++this->heatmap_profile.stamp_layout_cache_hits;
    else
        ++this->heatmap_profile.stamp_layout_cache_misses;
    this->heatmap_profile.stamp_ns += timer.nsecsElapsed();
    if (!stamps.isEmpty())
        ++this->heatmap_profile.raster_tiles_with_content;
    return stamps;
}

QVector<MapRhiGlobeRenderer::HeatmapStamp>
MapRhiGlobeRenderer::heatmapStampsForTile(
    const GlobeTile &tile, TileResource *resource,
    HeatmapRasterStats *stats) const
{
    if (stats != nullptr)
        *stats = HeatmapRasterStats();

    // The retained layout deliberately includes inactive markers. Converting
    // it to the currently renderable list is linear only in the few stamps
    // that actually overlap this tile, rather than in every marker candidate
    // tested while constructing the layout.
    const auto activeStampsFromLayout = [this, stats](
        const QVector<HeatmapStamp> &layout, bool *valid_indices)
    {
        QVector<HeatmapStamp> active_stamps;
        active_stamps.reserve(layout.size());
        *valid_indices = true;
        for (const HeatmapStamp &layout_stamp : layout)
        {
            if (layout_stamp.marker_index < 0
                || layout_stamp.marker_index >= this->heatmap_markers.size())
            {
                *valid_indices = false;
                active_stamps.clear();
                break;
            }

            const HeatmapMarker &marker = this->heatmap_markers.at(
                layout_stamp.marker_index);
            if (marker.render_id != layout_stamp.marker_render_id)
            {
                *valid_indices = false;
                active_stamps.clear();
                break;
            }
            if (!marker.active)
                continue;

            HeatmapStamp active_stamp = layout_stamp;
            active_stamp.color = marker.color;
            active_stamps.append(std::move(active_stamp));
        }
        if (*valid_indices && stats != nullptr)
            stats->marker_tile_pairs = active_stamps.size();
        return active_stamps;
    };

    const bool layout_matches = resource != nullptr
        && resource->heatmap_stamp_layout_revision
            == this->heatmap_stamp_layout_revision
        && resource->heatmap_stamp_layout_zoom == tile.zoom
        && resource->heatmap_stamp_layout_virtual_x == tile.virtual_x
        && resource->heatmap_stamp_layout_tile_y == tile.tile_y;
    if (layout_matches)
    {
        bool valid_indices = false;
        QVector<HeatmapStamp> active_stamps = activeStampsFromLayout(
            resource->heatmap_stamp_layout, &valid_indices);
        if (valid_indices)
        {
            if (stats != nullptr)
                stats->stamp_layout_cache_hit = true;
            return active_stamps;
        }
    }

    const auto retain_layout = [this, &tile, resource,
                                &activeStampsFromLayout](
        QVector<HeatmapStamp> stamps)
    {
        if (resource != nullptr)
        {
            resource->heatmap_stamp_layout = std::move(stamps);
            resource->heatmap_stamp_layout_revision =
                this->heatmap_stamp_layout_revision;
            resource->heatmap_stamp_layout_zoom = tile.zoom;
            resource->heatmap_stamp_layout_virtual_x = tile.virtual_x;
            resource->heatmap_stamp_layout_tile_y = tile.tile_y;
            bool valid_indices = false;
            return activeStampsFromLayout(
                resource->heatmap_stamp_layout, &valid_indices);
        }
        bool valid_indices = false;
        return activeStampsFromLayout(stamps, &valid_indices);
    };

    if (this->heatmap_markers.isEmpty() || !(this->heatmap_radius_m > 0.0) || tile.is_cap)
        return retain_layout({});

    // tile_center_lat_deg only, in degrees -- needed for the
    // latitude-dependent meters-per-pixel conversion just below. The
    // marker-vs-tile math further down works entirely in cached fractional
    // Web Mercator tile coordinates, not degrees, so the tile's own lon/lat
    // *bounds* are never needed as such.
    const double tile_lat_top_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y), tile.zoom);
    const double tile_lat_bottom_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y) + 1.0, tile.zoom);
    const double tile_center_lat_deg = (tile_lat_top_deg + tile_lat_bottom_deg) * 0.5;

    // Same latitude-dependent Web Mercator distortion approximation
    // MapRhiWidget::heatmapRadiusPixels() already accepts for the flat
    // map: exact at this tile's own center row, increasingly approximate
    // toward its top/bottom edges. Good enough for a soft-edged heatmap
    // blob; not something worth a more exact per-marker correction for.
    const double meters_per_pixel = GeoWebMercator::metersPerPixel(
        tile_center_lat_deg, tile.zoom);
    if (!std::isfinite(meters_per_pixel) || meters_per_pixel <= 0.0)
        return retain_layout({});

    const double radius_pixels = this->heatmap_radius_m / meters_per_pixel;
    if (!std::isfinite(radius_pixels) || radius_pixels <= 0.0)
        return retain_layout({});
    // Radius in the same fractional tile-coordinate units used for the
    // marker bounding check below (1.0 == one full tile edge), so that
    // check needs no separate unit conversion of its own.
    const double radius_tile_fraction = radius_pixels / double(GeoWebMercator::TileSize);

    int visited_bucket_cells = 0;
    const QVector<int> candidate_indices = heatmapMarkerCandidates(
        tile, radius_tile_fraction, &visited_bucket_cells);
    if (stats != nullptr)
    {
        stats->candidate_markers = candidate_indices.size();
        stats->candidate_bucket_cells = visited_bucket_cells;
    }
    if (candidate_indices.isEmpty())
        return retain_layout({});

    QVector<HeatmapStamp> stamps;
    stamps.reserve(candidate_indices.size());
    const double pixels_per_fraction = double(GlobeHeatmapTextureSize);
    const double tile_scale = std::ldexp(1.0, tile.zoom);
    if (!std::isfinite(tile_scale))
        return retain_layout({});
    for (int marker_index : candidate_indices)
    {
        // nearestWrappedTileX() picks whichever antimeridian-wrapped copy
        // of this marker's tile-space X sits closest to this tile's own
        // (already unwrapped) virtual_x -- the same reasoning
        // terrainCellCountForTile() and every other per-tile geodetic
        // calculation in this class already applies.
        if (marker_index < 0
            || marker_index >= this->heatmap_markers.size()
            || marker_index >= this->heatmap_marker_projections.size())
            continue;
        const HeatmapMarker &marker = this->heatmap_markers.at(marker_index);
        const HeatmapMarkerProjection &projection =
            this->heatmap_marker_projections.at(marker_index);
        if (!projection.valid)
            continue;
        const double marker_tile_x = GeoWebMercator::nearestWrappedTileX(
            projection.tile_x_zoom0 * tile_scale,
            double(tile.virtual_x), tile.zoom);
        const double marker_tile_y = projection.tile_y_zoom0 * tile_scale;
        if (!std::isfinite(marker_tile_x)
            || !std::isfinite(marker_tile_y))
        {
            continue;
        }
        const double fraction_x = marker_tile_x - double(tile.virtual_x);
        const double fraction_y = marker_tile_y - double(tile.tile_y);

        if (fraction_x + radius_tile_fraction < 0.0
            || fraction_x - radius_tile_fraction > 1.0
            || fraction_y + radius_tile_fraction < 0.0
            || fraction_y - radius_tile_fraction > 1.0)
        {
            continue;
        }

        HeatmapStamp stamp;
        stamp.center_x_pixels = fraction_x * pixels_per_fraction;
        stamp.center_y_pixels = fraction_y * pixels_per_fraction;
        stamp.radius_pixels = radius_pixels;
        stamp.marker_index = marker_index;
        stamp.marker_render_id = marker.render_id;
        stamp.color = marker.color;
        stamps.append(stamp);
    }

    return retain_layout(std::move(stamps));
}

bool MapRhiGlobeRenderer::queueHeatmapGpuBake(
    TileResource *resource, QRhiTexture *destination_texture,
    const QVector<HeatmapStamp> &stamps, int destination_layer)
{
    if (this->heatmap_gpu_baking_disabled || this->rhi == nullptr
        || resource == nullptr || destination_texture == nullptr
        || destination_layer < 0
        || destination_layer >= GlobeTileArrayLayerCount
        || stamps.isEmpty())
    {
        return false;
    }

    HeatmapGpuBakeJob job;
    job.resource = resource;
    job.destination_texture = destination_texture;
    job.destination_layer = destination_layer;
    job.revision = this->heatmap_revision;
    job.instances.reserve(stamps.size());
    const bool flip_for_texture_storage = this->rhi->isYUpInFramebuffer();
    for (const HeatmapStamp &stamp : stamps)
    {
        HeatmapBakeInstance instance;
        instance.center_x_pixels = float(stamp.center_x_pixels);
        instance.center_y_pixels = float(
            flip_for_texture_storage
                ? double(GlobeHeatmapTextureSize) - stamp.center_y_pixels
                : stamp.center_y_pixels);
        instance.radius_pixels = float(stamp.radius_pixels);
        instance.solid_fraction = float(this->heatmap_solid_fraction);
        instance.red = stamp.color.redF();
        instance.green = stamp.color.greenF();
        instance.blue = stamp.color.blueF();
        job.instances.append(instance);
    }
    this->heatmap_gpu_bake_jobs.append(std::move(job));
    return true;
}

void MapRhiGlobeRenderer::disableHeatmapGpuBaking()
{
    for (const HeatmapGpuBakeJob &job : this->heatmap_gpu_bake_jobs)
    {
        if (job.resource == nullptr)
            continue;
        job.resource->heatmap_revision = 0;
        if (job.destination_layer > 0)
            job.resource->heatmap_array_revision = 0;
        else
            job.resource->heatmap_texture_revision = 0;
    }
    this->heatmap_gpu_bake_jobs.clear();
    if (!this->heatmap_gpu_baking_disabled)
    {
        qCWarning(globeHeatmapPerformanceLog)
            << "Visible Globe heatmap GPU baking failed; falling back to CPU rasterization.";
    }
    this->heatmap_gpu_baking_disabled = true;
}

void MapRhiGlobeRenderer::scheduleDiagnosticHeatmapGpuBake(
    const GlobeTile &tile, const QVector<HeatmapStamp> &stamps,
    const QImage &premultiplied_image)
{
    if (!this->heatmap_profile.enabled || stamps.isEmpty()
        || premultiplied_image.isNull()
        || this->diagnostic_heatmap_gpu_validation_attempted)
    {
        return;
    }

    this->diagnostic_heatmap_bake_instances.clear();
    this->diagnostic_heatmap_bake_instances.reserve(stamps.size());
    for (const HeatmapStamp &stamp : stamps)
    {
        HeatmapBakeInstance instance;
        instance.center_x_pixels = float(stamp.center_x_pixels);
        instance.center_y_pixels = float(stamp.center_y_pixels);
        instance.radius_pixels = float(stamp.radius_pixels);
        instance.solid_fraction = float(this->heatmap_solid_fraction);
        instance.red = stamp.color.redF();
        instance.green = stamp.color.greenF();
        instance.blue = stamp.color.blueF();
        this->diagnostic_heatmap_bake_instances.append(instance);
    }

    this->diagnostic_heatmap_bake_revision = this->heatmap_revision;
    this->diagnostic_heatmap_gpu_validation_attempted = true;
    this->diagnostic_heatmap_bake_zoom = tile.zoom;
    this->diagnostic_heatmap_bake_tile_x = tile.virtual_x;
    this->diagnostic_heatmap_bake_tile_y = tile.tile_y;
    this->diagnostic_heatmap_bake_cpu_reference = premultiplied_image;
    this->diagnostic_heatmap_bake_pending = true;
}

void MapRhiGlobeRenderer::scheduleDiagnosticHeatmapGpuSelfTest()
{
    if (!this->heatmap_profile.enabled
        || this->diagnostic_heatmap_gpu_validation_attempted)
    {
        return;
    }

    // This deliberately asymmetric pattern validates Y orientation, clipped
    // stamp quads, radial falloff, color channels, and ordered source-over
    // overlap without depending on the current camera or visible markers.
    QVector<HeatmapStamp> stamps;
    stamps.reserve(4);

    HeatmapStamp stamp;
    stamp.center_x_pixels = 48.25;
    stamp.center_y_pixels = 57.5;
    stamp.radius_pixels = 40.75;
    stamp.color = QColor(239, 74, 62);
    stamps.append(stamp);

    stamp.center_x_pixels = 122.5;
    stamp.center_y_pixels = 91.25;
    stamp.radius_pixels = 63.5;
    stamp.color = QColor(54, 198, 121);
    stamps.append(stamp);

    stamp.center_x_pixels = 182.25;
    stamp.center_y_pixels = 169.75;
    stamp.radius_pixels = 51.25;
    stamp.color = QColor(68, 112, 242);
    stamps.append(stamp);

    stamp.center_x_pixels = 251.0;
    stamp.center_y_pixels = 224.5;
    stamp.radius_pixels = 49.0;
    stamp.color = QColor(231, 174, 48);
    stamps.append(stamp);

    GlobeTile self_test_tile;
    self_test_tile.zoom = -1;
    scheduleDiagnosticHeatmapGpuBake(
        self_test_tile, stamps, renderHeatmapStamps(stamps));
}

void MapRhiGlobeRenderer::releaseDiagnosticHeatmapGpuBakeResources()
{
    this->diagnostic_heatmap_bake_pipeline.reset();
    this->diagnostic_heatmap_bake_bindings.reset();
    this->diagnostic_heatmap_bake_target.reset();
    this->diagnostic_heatmap_bake_render_pass_descriptor.reset();
    this->diagnostic_heatmap_bake_texture.reset();
    this->diagnostic_heatmap_bake_vertex_buffer.reset();
    this->diagnostic_heatmap_bake_instance_buffer.reset();
    this->diagnostic_heatmap_bake_instance_buffer_size = 0;
    this->diagnostic_heatmap_bake_vertex_upload_pending = true;
}

void MapRhiGlobeRenderer::releaseVisibleHeatmapGpuBakeAtlasResources()
{
    this->heatmap_gpu_bake_atlases.clear();
    this->heatmap_gpu_bake_maximum_atlas_slots = 0;
}

MapRhiGlobeRenderer::HeatmapGpuBakeAtlas *
MapRhiGlobeRenderer::ensureVisibleHeatmapGpuBakeAtlasResources(
    int slot_count)
{
    if (slot_count <= 0 || this->rhi == nullptr)
        return nullptr;

    const std::map<int, HeatmapGpuBakeAtlas>::iterator existing =
        this->heatmap_gpu_bake_atlases.find(slot_count);
    if (existing != this->heatmap_gpu_bake_atlases.end())
    {
        HeatmapGpuBakeAtlas &atlas = existing->second;
        if (atlas.texture && atlas.render_pass_descriptor && atlas.target)
            return &atlas;
        this->heatmap_gpu_bake_atlases.erase(existing);
    }

    HeatmapGpuBakeAtlas atlas;
    atlas.slot_count = slot_count;
    const QSize atlas_size(
        slot_count * GlobeHeatmapTextureSize,
        GlobeHeatmapTextureSize);
    atlas.texture.reset(this->rhi->newTexture(
        QRhiTexture::RGBA8, atlas_size, 1,
        QRhiTexture::RenderTarget
            | QRhiTexture::UsedAsTransferSource));
    if (!atlas.texture || !atlas.texture->create())
        return nullptr;

    const QRhiTextureRenderTargetDescription target_description(
        QRhiColorAttachment(atlas.texture.get()));
    atlas.target.reset(
        this->rhi->newTextureRenderTarget(target_description));
    if (!atlas.target)
        return nullptr;
    atlas.render_pass_descriptor.reset(
        atlas.target->newCompatibleRenderPassDescriptor());
    if (!atlas.render_pass_descriptor)
        return nullptr;
    atlas.target->setRenderPassDescriptor(
        atlas.render_pass_descriptor.get());
    if (!atlas.target->create())
        return nullptr;

    this->heatmap_gpu_bake_atlases[slot_count] = std::move(atlas);
    return &this->heatmap_gpu_bake_atlases.at(slot_count);
}

int MapRhiGlobeRenderer::maximumVisibleHeatmapGpuBakeAtlasSlots()
{
    if (this->heatmap_gpu_bake_maximum_atlas_slots > 0)
        return this->heatmap_gpu_bake_maximum_atlas_slots;
    if (this->rhi == nullptr)
        return 0;

    const int maximum_texture_size = qMax(
        GlobeHeatmapTextureSize,
        this->rhi->resourceLimit(QRhi::TextureSizeMax));
    const int maximum_candidate = qBound(
        1, maximum_texture_size / GlobeHeatmapTextureSize,
        GlobeHeatmapGpuBakeAtlasMaximumSlots);
    int slot_count = 1;
    while (slot_count <= maximum_candidate / 2)
        slot_count *= 2;

    while (slot_count > 0)
    {
        if (ensureVisibleHeatmapGpuBakeAtlasResources(slot_count) != nullptr)
        {
            this->heatmap_gpu_bake_maximum_atlas_slots = slot_count;
            return slot_count;
        }
        slot_count /= 2;
    }
    return 0;
}

bool MapRhiGlobeRenderer::ensureDiagnosticHeatmapGpuBakeResources()
{
    static_assert(
        sizeof(HeatmapBakeVertex) == 2 * sizeof(float),
        "Heatmap bake vertex layout must stay tightly packed");
    static_assert(
        sizeof(HeatmapBakeInstance) == 9 * sizeof(float),
        "Heatmap bake instance layout must stay tightly packed");

    if (this->rhi == nullptr)
        return false;

    if (!this->diagnostic_heatmap_bake_texture)
    {
        this->diagnostic_heatmap_bake_texture.reset(this->rhi->newTexture(
            QRhiTexture::RGBA8,
            QSize(GlobeHeatmapTextureSize, GlobeHeatmapTextureSize), 1,
            QRhiTexture::RenderTarget
                | QRhiTexture::UsedAsTransferSource));
        if (!this->diagnostic_heatmap_bake_texture
            || !this->diagnostic_heatmap_bake_texture->create())
        {
            releaseDiagnosticHeatmapGpuBakeResources();
            return false;
        }
    }

    if (!this->diagnostic_heatmap_bake_target)
    {
        const QRhiTextureRenderTargetDescription target_description(
            QRhiColorAttachment(
                this->diagnostic_heatmap_bake_texture.get()));
        this->diagnostic_heatmap_bake_target.reset(
            this->rhi->newTextureRenderTarget(target_description));
        if (!this->diagnostic_heatmap_bake_target)
        {
            releaseDiagnosticHeatmapGpuBakeResources();
            return false;
        }
        this->diagnostic_heatmap_bake_render_pass_descriptor.reset(
            this->diagnostic_heatmap_bake_target
                ->newCompatibleRenderPassDescriptor());
        if (!this->diagnostic_heatmap_bake_render_pass_descriptor)
        {
            releaseDiagnosticHeatmapGpuBakeResources();
            return false;
        }
        this->diagnostic_heatmap_bake_target->setRenderPassDescriptor(
            this->diagnostic_heatmap_bake_render_pass_descriptor.get());
        if (!this->diagnostic_heatmap_bake_target->create())
        {
            releaseDiagnosticHeatmapGpuBakeResources();
            return false;
        }
    }

    if (!this->diagnostic_heatmap_bake_bindings)
    {
        this->diagnostic_heatmap_bake_bindings.reset(
            this->rhi->newShaderResourceBindings());
        if (!this->diagnostic_heatmap_bake_bindings)
        {
            releaseDiagnosticHeatmapGpuBakeResources();
            return false;
        }
        this->diagnostic_heatmap_bake_bindings->setBindings({});
        if (!this->diagnostic_heatmap_bake_bindings->create())
        {
            releaseDiagnosticHeatmapGpuBakeResources();
            return false;
        }
    }

    if (!this->diagnostic_heatmap_bake_vertex_buffer)
    {
        constexpr int VertexCount = 6;
        const int vertex_bytes =
            VertexCount * int(sizeof(HeatmapBakeVertex));
        this->diagnostic_heatmap_bake_vertex_buffer.reset(
            this->rhi->newBuffer(
                QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                vertex_bytes));
        if (!this->diagnostic_heatmap_bake_vertex_buffer
            || !this->diagnostic_heatmap_bake_vertex_buffer->create())
        {
            releaseDiagnosticHeatmapGpuBakeResources();
            return false;
        }
        this->diagnostic_heatmap_bake_vertex_upload_pending = true;
    }

    if (!this->diagnostic_heatmap_bake_pipeline)
    {
        const QShader vertex_shader = loadGlobeShader(QStringLiteral(
            ":/aowis/map/rhi/map_rhi_globe_heatmap_bake.vert.qsb"));
        const QShader fragment_shader = loadGlobeShader(QStringLiteral(
            ":/aowis/map/rhi/map_rhi_globe_heatmap_bake.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
        {
            releaseDiagnosticHeatmapGpuBakeResources();
            return false;
        }

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(HeatmapBakeVertex))},
            {quint32(sizeof(HeatmapBakeInstance)),
             QRhiVertexInputBinding::PerInstance}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(HeatmapBakeVertex, corner_x))},
            {1, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(HeatmapBakeInstance, center_x_pixels))},
            {1, 2, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(HeatmapBakeInstance, radius_pixels))},
            {1, 3, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(HeatmapBakeInstance, red))},
            {1, 4, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(HeatmapBakeInstance, target_scale_x))}
        });

        this->diagnostic_heatmap_bake_pipeline.reset(
            this->rhi->newGraphicsPipeline());
        if (!this->diagnostic_heatmap_bake_pipeline)
        {
            releaseDiagnosticHeatmapGpuBakeResources();
            return false;
        }
        this->diagnostic_heatmap_bake_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->diagnostic_heatmap_bake_pipeline->setVertexInputLayout(
            input_layout);
        this->diagnostic_heatmap_bake_pipeline->setShaderResourceBindings(
            this->diagnostic_heatmap_bake_bindings.get());
        this->diagnostic_heatmap_bake_pipeline->setRenderPassDescriptor(
            this->diagnostic_heatmap_bake_render_pass_descriptor.get());
        this->diagnostic_heatmap_bake_pipeline->setTopology(
            QRhiGraphicsPipeline::Triangles);
        this->diagnostic_heatmap_bake_pipeline->setSampleCount(1);
        this->diagnostic_heatmap_bake_pipeline->setCullMode(
            QRhiGraphicsPipeline::None);
        this->diagnostic_heatmap_bake_pipeline->setDepthTest(false);
        this->diagnostic_heatmap_bake_pipeline->setDepthWrite(false);
        QRhiGraphicsPipeline::TargetBlend heatmap_blend;
        heatmap_blend.enable = true;
        this->diagnostic_heatmap_bake_pipeline->setTargetBlends({
            heatmap_blend
        });
        if (!this->diagnostic_heatmap_bake_pipeline->create())
        {
            releaseDiagnosticHeatmapGpuBakeResources();
            return false;
        }
    }

    return true;
}

bool MapRhiGlobeRenderer::runPendingHeatmapGpuBakes(
    QRhiCommandBuffer *command_buffer)
{
    if (this->heatmap_gpu_bake_jobs.isEmpty())
        return true;

    qsizetype total_instance_count = 0;
    for (const HeatmapGpuBakeJob &job : this->heatmap_gpu_bake_jobs)
    {
        if (job.resource == nullptr || job.destination_texture == nullptr
            || job.destination_layer < 0
            || job.destination_layer >= GlobeTileArrayLayerCount
            || job.revision != this->heatmap_revision
            || job.instances.isEmpty()
            || total_instance_count
                > std::numeric_limits<qsizetype>::max()
                    - job.instances.size())
        {
            disableHeatmapGpuBaking();
            return false;
        }
        total_instance_count += job.instances.size();
    }
    if (total_instance_count
        > qsizetype(std::numeric_limits<int>::max())
            / qsizetype(sizeof(HeatmapBakeInstance)))
    {
        disableHeatmapGpuBaking();
        return false;
    }
    const qsizetype required_bytes_qsize = total_instance_count
        * qsizetype(sizeof(HeatmapBakeInstance));
    if (command_buffer == nullptr || required_bytes_qsize <= 0
        || !ensureDiagnosticHeatmapGpuBakeResources())
    {
        disableHeatmapGpuBaking();
        return false;
    }
    const int maximum_atlas_slots =
        maximumVisibleHeatmapGpuBakeAtlasSlots();
    if (maximum_atlas_slots <= 0)
    {
        disableHeatmapGpuBaking();
        return false;
    }

    const int required_bytes = int(required_bytes_qsize);
    if (!this->heatmap_gpu_bake_instance_buffer
        || this->heatmap_gpu_bake_instance_buffer_size < required_bytes)
    {
        this->heatmap_gpu_bake_instance_buffer.reset(
            this->rhi->newBuffer(
                QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                required_bytes));
        if (!this->heatmap_gpu_bake_instance_buffer
            || !this->heatmap_gpu_bake_instance_buffer->create())
        {
            this->heatmap_gpu_bake_instance_buffer.reset();
            this->heatmap_gpu_bake_instance_buffer_size = 0;
            disableHeatmapGpuBaking();
            return false;
        }
        this->heatmap_gpu_bake_instance_buffer_size = required_bytes;
    }

    struct HeatmapGpuBakePage
    {
        int first_job = 0;
        int last_job = 0;
        HeatmapGpuBakeAtlas *atlas = nullptr;
        qsizetype first_instance = 0;
        qsizetype instance_count = 0;
    };

    const int job_count = int(this->heatmap_gpu_bake_jobs.size());
    QVector<HeatmapGpuBakePage> pages;
    pages.reserve(
        (job_count + maximum_atlas_slots - 1) / maximum_atlas_slots);
    QVector<HeatmapBakeInstance> all_instances;
    all_instances.reserve(int(total_instance_count));
    for (int first_job = 0; first_job < job_count;)
    {
        HeatmapGpuBakePage page;
        page.first_job = first_job;
        page.last_job = qMin(
            first_job + maximum_atlas_slots, job_count);

        const int page_tile_count = page.last_job - page.first_job;
        int requested_slots = 1;
        while (requested_slots < page_tile_count)
            requested_slots *= 2;
        page.atlas = ensureVisibleHeatmapGpuBakeAtlasResources(
            requested_slots);
        if (page.atlas == nullptr)
        {
            page.atlas = ensureVisibleHeatmapGpuBakeAtlasResources(
                maximum_atlas_slots);
        }
        if (page.atlas == nullptr
            || page.atlas->slot_count < page_tile_count)
        {
            disableHeatmapGpuBaking();
            return false;
        }

        page.first_instance = all_instances.size();
        const float atlas_slot_scale =
            1.0f / float(page.atlas->slot_count);
        for (int job_index = page.first_job;
             job_index < page.last_job; ++job_index)
        {
            const HeatmapGpuBakeJob &job =
                this->heatmap_gpu_bake_jobs.at(job_index);
            const int slot = job_index - page.first_job;
            for (const HeatmapBakeInstance &source_instance : job.instances)
            {
                HeatmapBakeInstance instance = source_instance;
                instance.target_scale_x = atlas_slot_scale;
                instance.target_offset_x =
                    float(slot) * atlas_slot_scale;
                all_instances.append(instance);
            }
        }
        page.instance_count =
            all_instances.size() - page.first_instance;
        pages.append(page);
        first_job = page.last_job;
    }

    // QRhi dynamic-buffer writes may accumulate within a frame, so later
    // writes to an overlapping range are not guaranteed to stay invisible
    // to earlier passes. Upload every tile's instances once into disjoint
    // ranges and select those ranges with vertex-buffer offsets below.
    QRhiResourceUpdateBatch *bake_updates =
        this->rhi->nextResourceUpdateBatch();
    if (bake_updates == nullptr)
    {
        disableHeatmapGpuBaking();
        return false;
    }
    const bool upload_bake_vertices =
        this->diagnostic_heatmap_bake_vertex_upload_pending;
    if (upload_bake_vertices)
    {
        const HeatmapBakeVertex vertices[] = {
            {-1.0f, -1.0f}, {1.0f, -1.0f},
            {-1.0f, 1.0f}, {-1.0f, 1.0f},
            {1.0f, -1.0f}, {1.0f, 1.0f}
        };
        bake_updates->uploadStaticBuffer(
            this->diagnostic_heatmap_bake_vertex_buffer.get(), vertices);
    }
    bake_updates->updateDynamicBuffer(
        this->heatmap_gpu_bake_instance_buffer.get(), 0,
        required_bytes, all_instances.constData());

    QVector<QRhiResourceUpdateBatch *> page_copy_updates;
    page_copy_updates.reserve(pages.size());
    for (const HeatmapGpuBakePage &page : pages)
    {
        QRhiResourceUpdateBatch *copy_updates =
            this->rhi->nextResourceUpdateBatch();
        if (copy_updates == nullptr)
        {
            for (QRhiResourceUpdateBatch *allocated_updates
                 : page_copy_updates)
            {
                allocated_updates->release();
            }
            bake_updates->release();
            if (upload_bake_vertices)
                this->diagnostic_heatmap_bake_vertex_upload_pending = true;
            disableHeatmapGpuBaking();
            return false;
        }

        for (int job_index = page.first_job;
             job_index < page.last_job; ++job_index)
        {
            const HeatmapGpuBakeJob &job =
                this->heatmap_gpu_bake_jobs.at(job_index);
            const int slot = job_index - page.first_job;
            QRhiTextureCopyDescription copy_description;
            copy_description.setSourceTopLeft(QPoint(
                slot * GlobeHeatmapTextureSize, 0));
            copy_description.setDestinationLayer(job.destination_layer);
            copy_description.setPixelSize(QSize(
                GlobeHeatmapTextureSize, GlobeHeatmapTextureSize));
            copy_updates->copyTexture(
                job.destination_texture, page.atlas->texture.get(),
                copy_description);
        }
        page_copy_updates.append(copy_updates);
    }
    if (upload_bake_vertices)
        this->diagnostic_heatmap_bake_vertex_upload_pending = false;

    int recorded_tiles = 0;
    int recorded_stamps = 0;
    int recorded_passes = 0;
    int recorded_copy_batches = 0;
    int recorded_array_copies = 0;
    for (int page_index = 0; page_index < pages.size(); ++page_index)
    {
        const HeatmapGpuBakePage &page = pages.at(page_index);
        const QSize atlas_size(
            page.atlas->slot_count * GlobeHeatmapTextureSize,
            GlobeHeatmapTextureSize);

        command_buffer->beginPass(
            page.atlas->target.get(),
            Qt::transparent, {1.0f, 0},
            page_index == 0 ? bake_updates : nullptr);
        command_buffer->setViewport(QRhiViewport(
            0.0f, 0.0f, float(atlas_size.width()),
            float(atlas_size.height())));
        command_buffer->setGraphicsPipeline(
            this->diagnostic_heatmap_bake_pipeline.get());
        command_buffer->setShaderResources(
            this->diagnostic_heatmap_bake_bindings.get());
        const QRhiCommandBuffer::VertexInput bindings[] = {
            {this->diagnostic_heatmap_bake_vertex_buffer.get(), 0},
            {this->heatmap_gpu_bake_instance_buffer.get(),
             quint32(page.first_instance
                 * qsizetype(sizeof(HeatmapBakeInstance)))}
        };
        command_buffer->setVertexInput(0, 2, bindings);
        command_buffer->draw(6, quint32(page.instance_count));
        command_buffer->endPass(page_copy_updates.at(page_index));

        for (int job_index = page.first_job;
             job_index < page.last_job; ++job_index)
        {
            const HeatmapGpuBakeJob &job =
                this->heatmap_gpu_bake_jobs.at(job_index);
            if (job.destination_layer > 0)
            {
                job.resource->heatmap_array_revision = job.revision;
                ++recorded_array_copies;
            }
            else
            {
                job.resource->heatmap_texture_revision = job.revision;
            }
            job.resource->heatmap_revision = job.revision;
        }

        const int page_tile_count = page.last_job - page.first_job;
        recorded_tiles += page_tile_count;
        recorded_stamps += int(page.instance_count);
        ++recorded_passes;
        ++recorded_copy_batches;
    }

    this->heatmap_profile.gpu_visible_bake_passes += recorded_passes;
    this->heatmap_profile.gpu_visible_bake_stamps += recorded_stamps;
    this->heatmap_profile.gpu_visible_copies += recorded_tiles;
    this->heatmap_profile.gpu_visible_copy_batches +=
        recorded_copy_batches;
    this->heatmap_profile.gpu_visible_array_copies +=
        recorded_array_copies;

    qCDebug(globeHeatmapPerformanceLog).noquote().nospace()
        << "visible_gpu_bakes revision=" << this->heatmap_revision
        << " tiles=" << recorded_tiles
        << " passes=" << recorded_passes
        << " stamps=" << recorded_stamps
        << " copies=" << recorded_tiles
        << " array_copies=" << recorded_array_copies
        << " copy_batches=" << recorded_copy_batches
        << " atlas_slots_max=" << maximum_atlas_slots
        << " array_config="
        << (guiConfiguration().map_performance.array_batching_enabled ? 1 : 0)
        << " texture_arrays="
        << (this->rhi != nullptr
                && this->rhi->isFeatureSupported(QRhi::TextureArrays)
            ? 1 : 0)
        << " imagery_array_pipeline="
        << (this->array_pipeline ? 1 : 0)
        << " imagery_array_pages=" << this->tile_array_pages.size()
        << " imagery_array_active=" << (arrayBatchingActive() ? 1 : 0)
        << " status=recorded";
    this->heatmap_gpu_bake_jobs.clear();
    return true;
}

void MapRhiGlobeRenderer::runDiagnosticHeatmapGpuBake(
    QRhiCommandBuffer *command_buffer)
{
    if (!this->diagnostic_heatmap_bake_pending
        && !this->heatmap_profile_report_pending)
    {
        return;
    }

    QString failure;
    if (this->diagnostic_heatmap_bake_pending
        && this->heatmap_profile.enabled)
    {
        const qsizetype required_bytes_qsize =
            this->diagnostic_heatmap_bake_instances.size()
            * qsizetype(sizeof(HeatmapBakeInstance));
        if (command_buffer == nullptr)
        {
            failure = QStringLiteral("missing_command_buffer");
        }
        else if (required_bytes_qsize <= 0
                 || required_bytes_qsize
                    > qsizetype(std::numeric_limits<int>::max()))
        {
            failure = QStringLiteral("invalid_instance_buffer_size");
        }
        else if (!ensureDiagnosticHeatmapGpuBakeResources())
        {
            failure = QStringLiteral("resource_creation_failed");
        }
        else
        {
            const int required_bytes = int(required_bytes_qsize);
            if (!this->diagnostic_heatmap_bake_instance_buffer
                || this->diagnostic_heatmap_bake_instance_buffer_size
                    < required_bytes)
            {
                this->diagnostic_heatmap_bake_instance_buffer.reset(
                    this->rhi->newBuffer(
                        QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                        required_bytes));
                if (!this->diagnostic_heatmap_bake_instance_buffer
                    || !this->diagnostic_heatmap_bake_instance_buffer
                        ->create())
                {
                    this->diagnostic_heatmap_bake_instance_buffer.reset();
                    this->diagnostic_heatmap_bake_instance_buffer_size = 0;
                    failure = QStringLiteral(
                        "instance_buffer_creation_failed");
                }
                else
                {
                    this->diagnostic_heatmap_bake_instance_buffer_size =
                        required_bytes;
                }
            }

            if (failure.isEmpty())
            {
                QRhiResourceUpdateBatch *resource_updates =
                    this->rhi->nextResourceUpdateBatch();
                if (resource_updates == nullptr)
                {
                    failure = QStringLiteral("update_batch_unavailable");
                }
                else
                {
                    if (this->diagnostic_heatmap_bake_vertex_upload_pending)
                    {
                        const HeatmapBakeVertex vertices[] = {
                            {-1.0f, -1.0f}, {1.0f, -1.0f},
                            {-1.0f, 1.0f}, {-1.0f, 1.0f},
                            {1.0f, -1.0f}, {1.0f, 1.0f}
                        };
                        resource_updates->uploadStaticBuffer(
                            this->diagnostic_heatmap_bake_vertex_buffer.get(),
                            vertices);
                        this->diagnostic_heatmap_bake_vertex_upload_pending =
                            false;
                    }
                    resource_updates->updateDynamicBuffer(
                        this->diagnostic_heatmap_bake_instance_buffer.get(),
                        0, required_bytes,
                        this->diagnostic_heatmap_bake_instances.constData());

                    command_buffer->beginPass(
                        this->diagnostic_heatmap_bake_target.get(),
                        Qt::transparent, {1.0f, 0}, resource_updates);
                    command_buffer->setViewport(QRhiViewport(
                        0.0f, 0.0f, float(GlobeHeatmapTextureSize),
                        float(GlobeHeatmapTextureSize)));
                    command_buffer->setGraphicsPipeline(
                        this->diagnostic_heatmap_bake_pipeline.get());
                    command_buffer->setShaderResources(
                        this->diagnostic_heatmap_bake_bindings.get());
                    const QRhiCommandBuffer::VertexInput bindings[] = {
                        {this->diagnostic_heatmap_bake_vertex_buffer.get(), 0},
                        {this->diagnostic_heatmap_bake_instance_buffer.get(), 0}
                    };
                    command_buffer->setVertexInput(0, 2, bindings);
                    command_buffer->draw(
                        6,
                        quint32(
                            this->diagnostic_heatmap_bake_instances.size()));
                    command_buffer->endPass();

                    if (this->diagnostic_heatmap_bake_cpu_reference.isNull())
                    {
                        qCDebug(globeHeatmapPerformanceLog)
                            .noquote().nospace()
                            << "diagnostic_gpu_validation revision="
                            << this->diagnostic_heatmap_bake_revision
                            << " tile="
                            << this->diagnostic_heatmap_bake_zoom << "/"
                            << this->diagnostic_heatmap_bake_tile_x << "/"
                            << this->diagnostic_heatmap_bake_tile_y
                            << " status=skipped reason="
                               "missing_cpu_reference";
                    }
                    else
                    {
                        QRhiResourceUpdateBatch *readback_updates =
                            this->rhi->nextResourceUpdateBatch();
                        if (readback_updates == nullptr)
                        {
                            qCDebug(globeHeatmapPerformanceLog)
                                .noquote().nospace()
                                << "diagnostic_gpu_validation revision="
                                << this->diagnostic_heatmap_bake_revision
                                << " tile="
                                << this->diagnostic_heatmap_bake_zoom << "/"
                                << this->diagnostic_heatmap_bake_tile_x << "/"
                                << this->diagnostic_heatmap_bake_tile_y
                                << " status=skipped reason="
                                   "update_batch_unavailable";
                        }
                        else
                        {
                            QRhiReadbackResult *readback_result =
                                new QRhiReadbackResult{};
                            const QImage cpu_reference =
                                this->diagnostic_heatmap_bake_cpu_reference;
                            const quint64 revision =
                                this->diagnostic_heatmap_bake_revision;
                            const int zoom =
                                this->diagnostic_heatmap_bake_zoom;
                            const int tile_x =
                                this->diagnostic_heatmap_bake_tile_x;
                            const int tile_y =
                                this->diagnostic_heatmap_bake_tile_y;
                            const int stamp_count = int(
                                this->diagnostic_heatmap_bake_instances.size());
                            readback_result->completed = [
                                readback_result, cpu_reference, revision, zoom,
                                tile_x, tile_y, stamp_count]()
                            {
                                reportGlobeHeatmapGpuValidation(
                                    *readback_result, cpu_reference, revision,
                                    zoom, tile_x, tile_y, stamp_count);
                                delete readback_result;
                            };
                            readback_updates->readBackTexture(
                                QRhiReadbackDescription(
                                    this->diagnostic_heatmap_bake_texture.get()),
                                readback_result);
                            command_buffer->resourceUpdate(readback_updates);
                            ++this->heatmap_profile.gpu_validation_readbacks;
                        }
                    }

                    ++this->heatmap_profile.gpu_bake_passes;
                    this->heatmap_profile.gpu_bake_stamps +=
                        int(this->diagnostic_heatmap_bake_instances.size());
                    qCDebug(globeHeatmapPerformanceLog).noquote().nospace()
                        << "diagnostic_gpu_bake revision="
                        << this->diagnostic_heatmap_bake_revision
                        << " tile=" << this->diagnostic_heatmap_bake_zoom
                        << "/" << this->diagnostic_heatmap_bake_tile_x
                        << "/" << this->diagnostic_heatmap_bake_tile_y
                        << " stamps="
                        << this->diagnostic_heatmap_bake_instances.size()
                        << " target=" << GlobeHeatmapTextureSize << "x"
                        << GlobeHeatmapTextureSize
                        << " status=recorded";
                }
            }
        }

        if (!failure.isEmpty())
        {
            qCDebug(globeHeatmapPerformanceLog).noquote().nospace()
                << "diagnostic_gpu_bake revision="
                << this->diagnostic_heatmap_bake_revision
                << " tile=" << this->diagnostic_heatmap_bake_zoom
                << "/" << this->diagnostic_heatmap_bake_tile_x
                << "/" << this->diagnostic_heatmap_bake_tile_y
                << " stamps="
                << this->diagnostic_heatmap_bake_instances.size()
                << " status=" << failure;
        }
    }

    this->diagnostic_heatmap_bake_instances.clear();
    this->diagnostic_heatmap_bake_cpu_reference = QImage();
    this->diagnostic_heatmap_bake_pending = false;
    this->diagnostic_heatmap_bake_revision = 0;
    if (this->heatmap_profile_report_pending)
    {
        reportHeatmapProfile();
        this->heatmap_profile_report_pending = false;
    }
}

bool MapRhiGlobeRenderer::ensureHeatmapTexture(
    const GlobeTile &tile, TileResource *resource,
    QRhiResourceUpdateBatch *resource_updates, QImage *updated_image,
    QVector<HeatmapStamp> *updated_stamps,
    bool upload_fallback_texture)
{
    ScopedHeatmapProfileTimer profile_timer(
        &this->heatmap_profile.cpu_ns, this->heatmap_profile.enabled);
    if (updated_image != nullptr)
        *updated_image = QImage();
    if (updated_stamps != nullptr)
        updated_stamps->clear();
    if (resource == nullptr || resource_updates == nullptr)
        return false;
    // Cheap common case: this tile's heatmap state already reflects the
    // current revision, so there is no CPU raster to regenerate.
    if (resource->heatmap_revision == this->heatmap_revision)
        return resource->bindings != nullptr || rebuildTileBindings(resource);

    if (this->heatmap_profile.enabled)
        ++this->heatmap_profile.dirty_tiles;

    // Both visible destinations consume the same small ordered stamp list:
    // the ordinary fallback texture can be queued immediately, while the
    // array destination is assigned by ensureTileHeatmapArray() later in
    // this prepare(). No pixels are rasterized or uploaded by the CPU on
    // either successful GPU path.
    if (!this->heatmap_gpu_baking_disabled)
    {
        QVector<HeatmapStamp> local_stamps;
        QVector<HeatmapStamp> *stamps = updated_stamps != nullptr
            ? updated_stamps : &local_stamps;
        *stamps = heatmapStampsForTileProfiled(tile, resource);
        resource->heatmap_has_content = !stamps->isEmpty();
        bool bindings_changed = false;
        if (stamps->isEmpty())
        {
            if (resource->heatmap_texture)
            {
                resource->bindings.reset();
                resource->heatmap_texture.reset();
                bindings_changed = true;
            }
            resource->heatmap_texture_revision = this->heatmap_revision;
            resource->heatmap_revision = this->heatmap_revision;
            if (bindings_changed || resource->bindings == nullptr)
                return rebuildTileBindings(resource);
            return true;
        }

        if (!upload_fallback_texture)
            return resource->bindings != nullptr
                || rebuildTileBindings(resource);

        if (!ensureDiagnosticHeatmapGpuBakeResources())
        {
            disableHeatmapGpuBaking();
        }
        else
        {
            if (!resource->heatmap_texture)
            {
                resource->bindings.reset();
                resource->heatmap_texture.reset(this->rhi->newTexture(
                    QRhiTexture::RGBA8,
                    QSize(GlobeHeatmapTextureSize,
                          GlobeHeatmapTextureSize)));
                if (!resource->heatmap_texture
                    || !resource->heatmap_texture->create())
                {
                    resource->heatmap_texture.reset();
                    disableHeatmapGpuBaking();
                }
                else
                {
                    bindings_changed = true;
                }
            }

            if (!this->heatmap_gpu_baking_disabled
                && queueHeatmapGpuBake(
                    resource, resource->heatmap_texture.get(), *stamps))
            {
                // Treat the sampled texture as current for the fallback
                // check later in this prepare(), but do not commit the
                // tile's logical revision until the copy is recorded.
                resource->heatmap_texture_revision =
                    this->heatmap_revision;
                if (bindings_changed || resource->bindings == nullptr)
                    return rebuildTileBindings(resource);
                return true;
            }
        }
    }

    QImage image = renderHeatmapTileProfiled(tile, resource);
    resource->heatmap_has_content = !image.isNull();
    bool bindings_changed = false;
    if (!image.isNull() && upload_fallback_texture)
    {
        if (!resource->heatmap_texture)
        {
            resource->heatmap_texture.reset(
                this->rhi->newTexture(QRhiTexture::RGBA8, image.size()));
            if (!resource->heatmap_texture || !resource->heatmap_texture->create())
                return false;
            bindings_changed = true;
        }
        resource_updates->uploadTexture(resource->heatmap_texture.get(), image);
        if (this->heatmap_profile.enabled)
        {
            ++this->heatmap_profile.fallback_uploads;
            this->heatmap_profile.upload_bytes += quint64(image.sizeInBytes());
        }
        resource->heatmap_texture_revision = this->heatmap_revision;
    }
    else if (image.isNull() && resource->heatmap_texture)
    {
        // A now-empty tile can bind the shared transparent dummy. Destroying
        // its old private texture avoids a clear upload and releases memory.
        resource->bindings.reset();
        resource->heatmap_texture.reset();
        resource->heatmap_texture_revision = this->heatmap_revision;
        bindings_changed = true;
    }

    if (updated_image != nullptr)
        *updated_image = image;

    resource->heatmap_revision = this->heatmap_revision;
    if (bindings_changed || resource->bindings == nullptr)
        return rebuildTileBindings(resource);
    return true;
}

bool MapRhiGlobeRenderer::ensureHeatmapFallbackTexture(
    const GlobeTile &tile, TileResource *resource,
    const QImage &updated_image,
    const QVector<HeatmapStamp> &updated_stamps,
    QRhiResourceUpdateBatch *resource_updates)
{
    ScopedHeatmapProfileTimer profile_timer(
        &this->heatmap_profile.cpu_ns, this->heatmap_profile.enabled);
    if (resource == nullptr || resource_updates == nullptr
        || !resource->heatmap_has_content)
    {
        return true;
    }
    if (resource->heatmap_texture
        && resource->heatmap_texture_revision == this->heatmap_revision)
    {
        return resource->bindings != nullptr || rebuildTileBindings(resource);
    }

    if (!this->heatmap_gpu_baking_disabled)
    {
        QVector<HeatmapStamp> regenerated_stamps;
        const QVector<HeatmapStamp> *stamps = &updated_stamps;
        if (stamps->isEmpty())
        {
            regenerated_stamps = heatmapStampsForTileProfiled(
                tile, resource);
            stamps = &regenerated_stamps;
        }

        if (stamps->isEmpty())
        {
            const bool bindings_changed =
                resource->heatmap_texture != nullptr;
            if (bindings_changed)
            {
                resource->bindings.reset();
                resource->heatmap_texture.reset();
            }
            resource->heatmap_has_content = false;
            resource->heatmap_texture_revision = this->heatmap_revision;
            resource->heatmap_revision = this->heatmap_revision;
            if (bindings_changed || resource->bindings == nullptr)
                return rebuildTileBindings(resource);
            return true;
        }

        if (ensureDiagnosticHeatmapGpuBakeResources())
        {
            bool bindings_changed = false;
            if (!resource->heatmap_texture)
            {
                resource->bindings.reset();
                resource->heatmap_texture.reset(this->rhi->newTexture(
                    QRhiTexture::RGBA8,
                    QSize(GlobeHeatmapTextureSize,
                          GlobeHeatmapTextureSize)));
                if (resource->heatmap_texture
                    && resource->heatmap_texture->create())
                {
                    bindings_changed = true;
                }
                else
                {
                    resource->heatmap_texture.reset();
                }
            }

            if (resource->heatmap_texture
                && queueHeatmapGpuBake(
                    resource, resource->heatmap_texture.get(), *stamps))
            {
                resource->heatmap_texture_revision =
                    this->heatmap_revision;
                if (bindings_changed || resource->bindings == nullptr)
                    return rebuildTileBindings(resource);
                return true;
            }
        }

        disableHeatmapGpuBaking();
    }

    // Automatic backend/resource failure fallback. This is the only
    // remaining visible path that rasterizes heatmap pixels on the CPU.
    QImage image = updated_image;
    if (image.isNull())
        image = renderHeatmapTileProfiled(tile, resource);
    if (image.isNull())
        return false;

    bool bindings_changed = false;
    if (!resource->heatmap_texture)
    {
        resource->heatmap_texture.reset(
            this->rhi->newTexture(QRhiTexture::RGBA8, image.size()));
        if (!resource->heatmap_texture
            || !resource->heatmap_texture->create())
        {
            return false;
        }
        bindings_changed = true;
    }
    resource_updates->uploadTexture(resource->heatmap_texture.get(), image);
    if (this->heatmap_profile.enabled)
    {
        ++this->heatmap_profile.fallback_uploads;
        this->heatmap_profile.upload_bytes += quint64(image.sizeInBytes());
    }
    resource->heatmap_texture_revision = this->heatmap_revision;
    resource->heatmap_revision = this->heatmap_revision;
    if (bindings_changed || resource->bindings == nullptr)
        return rebuildTileBindings(resource);
    return true;
}

bool MapRhiGlobeRenderer::requestMissingTiles(QRhiResourceUpdateBatch *resource_updates)
{
    // A normal frame consumes this queue before the next prepare(). If a
    // preceding frame aborted later in preparation, invalidate its queued
    // texture revisions so the same tiles are queued again instead of
    // sampling data that was never recorded.
    for (const HeatmapGpuBakeJob &job : this->heatmap_gpu_bake_jobs)
    {
        if (job.resource == nullptr)
            continue;
        job.resource->heatmap_revision = 0;
        if (job.destination_layer > 0)
            job.resource->heatmap_array_revision = 0;
        else
            job.resource->heatmap_texture_revision = 0;
    }
    this->heatmap_gpu_bake_jobs.clear();

    this->heatmap_profile = HeatmapProfileCounters();
    this->heatmap_profile.enabled =
        globeHeatmapPerformanceLog().isDebugEnabled();
    this->heatmap_profile.visible_tiles = this->window_tiles.size();
    this->heatmap_profile_report_pending = this->heatmap_profile.enabled;
    scheduleDiagnosticHeatmapGpuSelfTest();

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
    {
        QImage updated_image;
        QImage updated_heatmap_image;
        QVector<HeatmapStamp> updated_heatmap_stamps;
        if (!ensureTileResource(
                tile, resource_updates, &updated_image,
                &updated_heatmap_image, &updated_heatmap_stamps))
            return false;
        if (!ensureTileArrayLayer(tile, updated_image, resource_updates))
            return false;
        if (!ensureTileHeatmapArray(
                tile, updated_heatmap_image, updated_heatmap_stamps,
                resource_updates))
        {
            return false;
        }
        if (!tile.array_ready && tile.resource != nullptr
            && !ensureHeatmapFallbackTexture(
                tile, tile.resource, updated_heatmap_image,
                updated_heatmap_stamps,
                resource_updates))
        {
            return false;
        }
    }
    for (GlobeTile &tile : this->cap_tiles)
    {
        if (!ensureTileResource(tile, resource_updates))
            return false;
    }
    trimUnusedHeatmapArrayPages();
    return true;
}

void MapRhiGlobeRenderer::reportHeatmapProfile() const
{
    if (!this->heatmap_profile.enabled)
        return;
    if (this->heatmap_profile.dirty_tiles <= 0
        && this->heatmap_profile.raster_calls <= 0
        && this->heatmap_profile.fallback_uploads <= 0
        && this->heatmap_profile.array_uploads <= 0
        && this->heatmap_profile.gpu_visible_bake_passes <= 0
        && this->heatmap_profile.gpu_bake_passes <= 0
        && this->heatmap_profile.gpu_validation_readbacks <= 0)
    {
        return;
    }

    constexpr double NsecsPerMillisecond = 1000000.0;
    constexpr double BytesPerMebibyte = 1024.0 * 1024.0;
    qCDebug(globeHeatmapPerformanceLog).noquote().nospace()
        << "revision=" << this->heatmap_revision
        << " markers=" << this->heatmap_markers.size()
        << " active_markers=" << this->heatmap_active_marker_count
        << " stamp_layout_revision="
        << this->heatmap_stamp_layout_revision
        << " visible_tiles=" << this->heatmap_profile.visible_tiles
        << " dirty_tiles=" << this->heatmap_profile.dirty_tiles
        << " raster_calls=" << this->heatmap_profile.raster_calls
        << " content_tiles="
        << this->heatmap_profile.raster_tiles_with_content
        << " candidate_markers="
        << this->heatmap_profile.candidate_markers
        << " candidate_bucket_cells="
        << this->heatmap_profile.candidate_bucket_cells
        << " marker_tile_pairs="
        << this->heatmap_profile.marker_tile_pairs
        << " radius_m="
        << QString::number(this->heatmap_radius_m, 'f', 3)
        << " marker_index_levels="
        << this->heatmap_marker_buckets_by_zoom.size()
        << " stamp_layout_cache_hits="
        << this->heatmap_profile.stamp_layout_cache_hits
        << " stamp_layout_cache_misses="
        << this->heatmap_profile.stamp_layout_cache_misses
        << " stamp_ms="
        << QString::number(
               double(this->heatmap_profile.stamp_ns)
                   / NsecsPerMillisecond,
               'f', 3)
        << " raster_ms="
        << QString::number(
               double(this->heatmap_profile.raster_ns)
                   / NsecsPerMillisecond,
               'f', 3)
        << " cpu_ms="
        << QString::number(
               double(this->heatmap_profile.cpu_ns)
                   / NsecsPerMillisecond,
               'f', 3)
        << " fallback_uploads="
        << this->heatmap_profile.fallback_uploads
        << " array_uploads=" << this->heatmap_profile.array_uploads
        << " upload_mib="
        << QString::number(
               double(this->heatmap_profile.upload_bytes)
                   / BytesPerMebibyte,
               'f', 3)
        << " gpu_visible_bake_passes="
        << this->heatmap_profile.gpu_visible_bake_passes
        << " gpu_visible_bake_stamps="
        << this->heatmap_profile.gpu_visible_bake_stamps
        << " gpu_visible_copies="
        << this->heatmap_profile.gpu_visible_copies
        << " gpu_visible_copy_batches="
        << this->heatmap_profile.gpu_visible_copy_batches
        << " gpu_visible_array_copies="
        << this->heatmap_profile.gpu_visible_array_copies
        << " gpu_bake_passes="
        << this->heatmap_profile.gpu_bake_passes
        << " gpu_bake_stamps="
        << this->heatmap_profile.gpu_bake_stamps
        << " gpu_validation_readbacks="
        << this->heatmap_profile.gpu_validation_readbacks
        << " array_config="
        << (guiConfiguration().map_performance.array_batching_enabled ? 1 : 0)
        << " texture_arrays="
        << (this->rhi != nullptr
                && this->rhi->isFeatureSupported(QRhi::TextureArrays)
            ? 1 : 0)
        << " imagery_array_pipeline="
        << (this->array_pipeline ? 1 : 0)
        << " imagery_array_pages=" << this->tile_array_pages.size()
        << " imagery_array_active=" << (arrayBatchingActive() ? 1 : 0)
        << " heatmap_array_pipeline="
        << (this->heatmap_array_pipeline ? 1 : 0)
        << " heatmap_array_pages=" << this->heatmap_array_pages.size();
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
        request.globe_render_origin_x = this->render_origin_ecef.x;
        request.globe_render_origin_y = this->render_origin_ecef.y;
        request.globe_render_origin_z = this->render_origin_ecef.z;

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
                TileVertex &target = this->window_vertices[first_vertex + index];
                target.x = vertex.x;
                target.y = vertex.y;
                target.z = vertex.z;
                target.u = vertex.u;
                target.v = vertex.v;
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

bool MapRhiGlobeRenderer::createTileArrayPage()
{
    if (this->rhi == nullptr || !this->camera_uniform_buffer || !this->sampler
        || int(this->tile_array_pages.size())
            >= GlobeTileArrayMaximumPageCount)
    {
        return false;
    }

    TileArrayPage page;
    page.texture.reset(this->rhi->newTextureArray(
        QRhiTexture::RGBA8, GlobeTileArrayLayerCount,
        QSize(MapModel::TileSize, MapModel::TileSize)));
    if (!page.texture || !page.texture->create())
        return false;

    page.bindings.reset(this->rhi->newShaderResourceBindings());
    if (!page.bindings)
        return false;
    page.bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage
                | QRhiShaderResourceBinding::FragmentStage,
            this->camera_uniform_buffer.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage,
            page.texture.get(), this->sampler.get())
    });
    if (!page.bindings->create())
        return false;

    page.free_layers.reserve(GlobeTileArrayUsableLayerCount);
    for (int layer = GlobeTileArrayLayerCount - 1; layer >= 1; --layer)
        page.free_layers.append(layer);

    this->tile_array_pages.push_back(std::move(page));
    this->tile_array_draw_indices_dirty = true;
    this->heatmap_array_draw_indices_dirty = true;
    return true;
}

bool MapRhiGlobeRenderer::createTileArrayResources()
{
    // MapRhiWidget owns both planar and Globe renderers at once and calls
    // initialize() on both. Avoid reserving even the first 64 MiB page until
    // Globe is actually selected, and honor the shared batching switch before
    // any optional resource allocation occurs. Further pages are created only
    // by ensureTileArrayLayer() after every existing page is full.
    if (this->map_model == nullptr
        || this->map_model->viewMode() != MapViewMode::Globe
        || !guiConfiguration().map_performance.array_batching_enabled)
    {
        return false;
    }
    if (this->rhi == nullptr || this->render_pass_descriptor == nullptr
        || !this->camera_uniform_buffer || !this->sampler)
    {
        return false;
    }

    if (this->tile_array_pages.empty())
    {
        if (!this->rhi->isFeatureSupported(QRhi::TextureArrays))
            return false;
        if (!createTileArrayPage())
            return false;
    }

    if (!this->array_pipeline)
    {
        const QShader vertex_shader = loadGlobeShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe_array.vert.qsb"));
        const QShader fragment_shader = loadGlobeShader(
            QStringLiteral(":/aowis/map/rhi/map_rhi_globe_array.frag.qsb"));
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
             quint32(offsetof(TileVertex, u))},
            {0, 2, QRhiVertexInputAttribute::Float,
             quint32(offsetof(TileVertex, layer))}
        });

        this->array_pipeline.reset(this->rhi->newGraphicsPipeline());
        if (!this->array_pipeline)
            return false;
        this->array_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->array_pipeline->setVertexInputLayout(input_layout);
        this->array_pipeline->setShaderResourceBindings(
            this->tile_array_pages.front().bindings.get());
        this->array_pipeline->setRenderPassDescriptor(this->render_pass_descriptor);
        this->array_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        this->array_pipeline->setSampleCount(this->sample_count);
        this->array_pipeline->setDepthTest(true);
        this->array_pipeline->setDepthWrite(true);
        this->array_pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if (!this->array_pipeline->create())
        {
            this->array_pipeline.reset();
            return false;
        }
    }

    return true;
}

bool MapRhiGlobeRenderer::createHeatmapArrayPage()
{
    if (this->rhi == nullptr || !this->camera_uniform_buffer || !this->sampler
        || int(this->heatmap_array_pages.size())
            >= GlobeTileArrayMaximumPageCount)
    {
        return false;
    }

    HeatmapArrayPage page;
    page.texture.reset(this->rhi->newTextureArray(
        QRhiTexture::RGBA8, GlobeTileArrayLayerCount,
        QSize(GlobeHeatmapTextureSize, GlobeHeatmapTextureSize)));
    if (!page.texture || !page.texture->create())
        return false;

    page.free_layers.reserve(GlobeTileArrayUsableLayerCount);
    for (int layer = GlobeTileArrayLayerCount - 1; layer >= 1; --layer)
        page.free_layers.append(layer);

    this->heatmap_array_pages.push_back(std::move(page));
    this->heatmap_array_draw_indices_dirty = true;
    return true;
}

bool MapRhiGlobeRenderer::createHeatmapArrayResources()
{
    if (!arrayBatchingActive() || this->heatmap_markers.isEmpty()
        || this->rhi == nullptr || this->render_pass_descriptor == nullptr
        || !this->camera_uniform_buffer || !this->sampler)
    {
        return false;
    }

    if (this->heatmap_array_pages.empty()
        && !createHeatmapArrayPage())
    {
        return false;
    }

    if (!this->heatmap_array_template_bindings)
    {
        this->heatmap_array_template_bindings.reset(
            this->rhi->newShaderResourceBindings());
        if (!this->heatmap_array_template_bindings)
            return false;
        this->heatmap_array_template_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                this->camera_uniform_buffer.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage,
                this->tile_array_pages.front().texture.get(),
                this->sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage,
                this->heatmap_array_pages.front().texture.get(),
                this->sampler.get())
        });
        if (!this->heatmap_array_template_bindings->create())
        {
            this->heatmap_array_template_bindings.reset();
            return false;
        }
    }

    if (!this->heatmap_array_pipeline)
    {
        const QShader vertex_shader = loadGlobeShader(
            QStringLiteral(
                ":/aowis/map/rhi/map_rhi_globe_heatmap_array.vert.qsb"));
        const QShader fragment_shader = loadGlobeShader(
            QStringLiteral(
                ":/aowis/map/rhi/map_rhi_globe_heatmap_array.frag.qsb"));
        if (!vertex_shader.isValid() || !fragment_shader.isValid())
            return false;

        QRhiVertexInputLayout input_layout;
        input_layout.setBindings({
            {quint32(sizeof(TileVertex))},
            {quint32(sizeof(float))}
        });
        input_layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float3,
             quint32(offsetof(TileVertex, x))},
            {0, 1, QRhiVertexInputAttribute::Float2,
             quint32(offsetof(TileVertex, u))},
            {0, 2, QRhiVertexInputAttribute::Float,
             quint32(offsetof(TileVertex, layer))},
            {1, 3, QRhiVertexInputAttribute::Float, 0}
        });

        this->heatmap_array_pipeline.reset(
            this->rhi->newGraphicsPipeline());
        if (!this->heatmap_array_pipeline)
            return false;
        this->heatmap_array_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, vertex_shader},
            {QRhiShaderStage::Fragment, fragment_shader}
        });
        this->heatmap_array_pipeline->setVertexInputLayout(input_layout);
        this->heatmap_array_pipeline->setShaderResourceBindings(
            this->heatmap_array_template_bindings.get());
        this->heatmap_array_pipeline->setRenderPassDescriptor(
            this->render_pass_descriptor);
        this->heatmap_array_pipeline->setTopology(
            QRhiGraphicsPipeline::Triangles);
        this->heatmap_array_pipeline->setSampleCount(this->sample_count);
        this->heatmap_array_pipeline->setDepthTest(true);
        this->heatmap_array_pipeline->setDepthWrite(true);
        this->heatmap_array_pipeline->setDepthOp(
            QRhiGraphicsPipeline::LessOrEqual);
        if (!this->heatmap_array_pipeline->create())
        {
            this->heatmap_array_pipeline.reset();
            return false;
        }
    }

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

    if (!this->heatmap_dummy_texture)
    {
        this->heatmap_dummy_texture.reset(
            this->rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!this->heatmap_dummy_texture || !this->heatmap_dummy_texture->create())
            return false;
        this->heatmap_dummy_texture_upload_pending = true;
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
                this->dummy_texture.get(), this->sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage,
                this->heatmap_dummy_texture.get(), this->sampler.get())
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

    // Texture arrays are optional. Unsupported backends and allocation or
    // shader failures retain the already-created per-tile pipeline above.
    createTileArrayResources();

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
    const QSize &viewport_size, float heatmap_opacity,
    const QColor &background_color, float background_opacity)
{
    if (this->rhi == nullptr || resource_updates == nullptr || this->map_model == nullptr)
        return false;
    if (!ensureSharedResources())
        return false;
    this->heatmap_opacity = qBound(0.0f, heatmap_opacity, 1.0f);
    buildCaps();

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

    // Resolve imagery and stamp array layers before a pending full geometry
    // upload. A rebuilt window then carries every already-ready layer in its
    // single upload instead of issuing one follow-up buffer patch per tile.
    if (this->map_visible && !requestMissingTiles(resource_updates))
        return false;

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

    if (this->window_index_upload_pending && !this->window_indices.isEmpty())
    {
        const int required_bytes =
            int(this->window_indices.size() * qsizetype(sizeof(quint32)));
        if (!this->window_index_buffer || this->window_index_buffer_size != required_bytes)
        {
            this->window_index_buffer.reset(this->rhi->newBuffer(
                QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, required_bytes));
            if (!this->window_index_buffer || !this->window_index_buffer->create())
                return false;
            this->window_index_buffer_size = required_bytes;
        }
        resource_updates->updateDynamicBuffer(
            this->window_index_buffer.get(), 0, required_bytes,
            this->window_indices.constData());
        this->window_index_upload_pending = false;
    }

    if (this->map_visible
        && !uploadHeatmapArrayDrawIndices(resource_updates))
    {
        return false;
    }
    // Heatmap validation can move a tile back to the combined fallback.
    // Rebuild the imagery index stream afterwards so that tile is not also
    // submitted through a stale array range in this same frame.
    if (this->map_visible && !uploadTileArrayDrawIndices(resource_updates))
        return false;
    if (this->map_visible
        && !this->heatmap_array_draw_indices.isEmpty()
        && !uploadHeatmapArrayLayers(resource_updates))
    {
        return false;
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

    if (this->cap_index_upload_pending && !this->cap_indices.isEmpty())
    {
        const int required_bytes =
            int(this->cap_indices.size() * qsizetype(sizeof(quint32)));
        if (!this->cap_index_buffer)
        {
            this->cap_index_buffer.reset(this->rhi->newBuffer(
                QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, required_bytes));
            if (!this->cap_index_buffer || !this->cap_index_buffer->create())
                return false;
        }
        resource_updates->updateDynamicBuffer(
            this->cap_index_buffer.get(), 0, required_bytes,
            this->cap_indices.constData());
        this->cap_index_upload_pending = false;
    }

    if (!uploadWireframeVertices(resource_updates))
        return false;

    if (this->heatmap_dummy_texture_upload_pending && this->heatmap_dummy_texture)
    {
        QImage image(1, 1, QImage::Format_RGBA8888);
        image.fill(Qt::transparent);
        resource_updates->uploadTexture(this->heatmap_dummy_texture.get(), image);
        this->heatmap_dummy_texture_upload_pending = false;
    }

    // 24 floats: the 16-float view_projection matrix, heatmap_settings, and
    // basemap_settings. basemap_settings mirrors the flat RHI renderer: rgb
    // is the UI window color and .a is the map-background opacity controlled
    // by the sidebar slider. Keeping this in the per-frame camera block means
    // globe imagery and the polar caps fade immediately without regenerating
    // any tile textures.
    float uniform_data[24] = {};
    std::copy(view_projection.constData(), view_projection.constData() + 16, uniform_data);
    uniform_data[17] = this->heatmap_opacity;
    uniform_data[20] = background_color.redF();
    uniform_data[21] = background_color.greenF();
    uniform_data[22] = background_color.blueF();
    uniform_data[23] = qBound(0.0f, background_opacity, 1.0f);
    resource_updates->updateDynamicBuffer(
        this->camera_uniform_buffer.get(), 0, GlobeCameraUniformBytes, uniform_data);

    return true;
}

void MapRhiGlobeRenderer::draw(QRhiCommandBuffer *command_buffer)
{
    if (command_buffer == nullptr)
        return;

    if (this->map_visible && this->pipeline)
    {
        bool use_array = false;
        bool use_heatmap_array = false;
        if (this->window_vertex_buffer && this->window_index_buffer)
        {
            use_array = arrayBatchingActive()
                && this->tile_array_draw_index_buffer
                && !this->tile_array_draw_indices.isEmpty();
            use_heatmap_array = use_array
                && heatmapArrayBatchingActive()
                && this->heatmap_opacity > 0.0f
                && this->heatmap_array_layer_buffer
                && this->heatmap_array_draw_index_buffer
                && !this->heatmap_array_draw_indices.isEmpty()
                && !this->heatmap_array_draw_batches.empty();
            if (use_heatmap_array)
            {
                // Imagery and heatmap are sampled in the same terrain pass.
                // Batches are grouped by (imagery page, heatmap page), while
                // a zero heatmap layer keeps unaffected tiles in that same
                // pass without sampling the heatmap array.
                command_buffer->setGraphicsPipeline(
                    this->heatmap_array_pipeline.get());
                const QRhiCommandBuffer::VertexInput heatmap_bindings[] = {
                    {this->window_vertex_buffer.get(), 0},
                    {this->heatmap_array_layer_buffer.get(), 0}
                };
                command_buffer->setVertexInput(
                    0, 2, heatmap_bindings,
                    this->heatmap_array_draw_index_buffer.get(), 0,
                    QRhiCommandBuffer::IndexUInt32);
                for (const HeatmapArrayDrawBatch &batch :
                     this->heatmap_array_draw_batches)
                {
                    if (!batch.bindings || batch.draw_index_count <= 0)
                        continue;
                    command_buffer->setShaderResources(batch.bindings.get());
                    command_buffer->drawIndexed(
                        quint32(batch.draw_index_count), 1,
                        quint32(batch.first_draw_index));
                }
            }
            else if (use_array)
            {
                // The compact index buffer groups every array-ready leaf by
                // stable page ownership. Bind the shared geometry once, then
                // submit one range per non-empty page. Still-loading leaves
                // are absent from these ranges and continue below through
                // the ordinary per-tile fallback.
                command_buffer->setGraphicsPipeline(this->array_pipeline.get());
                const QRhiCommandBuffer::VertexInput array_binding(
                    this->window_vertex_buffer.get(), 0);
                command_buffer->setVertexInput(
                    0, 1, &array_binding,
                    this->tile_array_draw_index_buffer.get(), 0,
                    QRhiCommandBuffer::IndexUInt32);
                for (const TileArrayPage &page : this->tile_array_pages)
                {
                    if (!page.bindings || page.draw_index_count <= 0)
                        continue;
                    command_buffer->setShaderResources(page.bindings.get());
                    command_buffer->drawIndexed(
                        quint32(page.draw_index_count), 1,
                        quint32(page.first_draw_index));
                }
            }

            command_buffer->setGraphicsPipeline(this->pipeline.get());
            for (const GlobeTile &tile : this->window_tiles)
            {
                if (tile.vertex_count <= 0 || tile.index_count <= 0)
                    continue;
                if (use_array && tile.array_ready)
                    continue;

                QRhiShaderResourceBindings *bindings = this->template_bindings.get();
                if (tile.resource != nullptr && tile.resource->bindings)
                    bindings = tile.resource->bindings.get();
                if (bindings == nullptr)
                    continue;

                command_buffer->setShaderResources(bindings);
                const QRhiCommandBuffer::VertexInput binding(
                    this->window_vertex_buffer.get(), 0);
                const quint32 index_byte_offset = quint32(
                    tile.first_index * int(sizeof(quint32)));
                command_buffer->setVertexInput(
                    0, 1, &binding, this->window_index_buffer.get(),
                    index_byte_offset, QRhiCommandBuffer::IndexUInt32);
                command_buffer->drawIndexed(quint32(tile.index_count));
            }
        }

        if (this->cap_vertex_buffer && this->cap_index_buffer)
        {
            command_buffer->setGraphicsPipeline(this->pipeline.get());
            for (const GlobeTile &tile : this->cap_tiles)
            {
                if (tile.resource == nullptr || !tile.resource->bindings
                    || tile.vertex_count <= 0 || tile.index_count <= 0)
                {
                    continue;
                }

                command_buffer->setShaderResources(tile.resource->bindings.get());
                const QRhiCommandBuffer::VertexInput binding(
                    this->cap_vertex_buffer.get(), 0);
                const quint32 index_byte_offset = quint32(
                    tile.first_index * int(sizeof(quint32)));
                command_buffer->setVertexInput(
                    0, 1, &binding, this->cap_index_buffer.get(),
                    index_byte_offset, QRhiCommandBuffer::IndexUInt32);
                command_buffer->drawIndexed(quint32(tile.index_count));
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
    this->heatmap_gpu_bake_jobs.clear();
    resetWindowArrayLayers();
    this->tile_resources.clear();
    this->cap_resource = TileResource();
    for (TileArrayPage &page : this->tile_array_pages)
    {
        page.free_layers.clear();
        page.free_layers.reserve(GlobeTileArrayUsableLayerCount);
        for (int layer = GlobeTileArrayLayerCount - 1; layer >= 1; --layer)
            page.free_layers.append(layer);
        page.first_draw_index = 0;
        page.draw_index_count = 0;
    }
    trimUnusedTileArrayPages();
    this->tile_array_draw_indices.clear();
    this->tile_array_draw_indices_dirty = true;
    this->tile_array_draw_index_upload_pending = false;
    for (HeatmapArrayPage &page : this->heatmap_array_pages)
    {
        page.free_layers.clear();
        page.free_layers.reserve(GlobeTileArrayUsableLayerCount);
        for (int layer = GlobeTileArrayLayerCount - 1; layer >= 1; --layer)
            page.free_layers.append(layer);
    }
    trimUnusedHeatmapArrayPages();
    this->heatmap_array_draw_indices.clear();
    this->heatmap_array_draw_batches.clear();
    this->heatmap_array_draw_indices_dirty = true;
    this->heatmap_array_draw_index_upload_pending = false;
    for (GlobeTile &tile : this->window_tiles)
        tile.resource = nullptr;
    for (GlobeTile &tile : this->cap_tiles)
        tile.resource = nullptr;
    this->window_tiles_requested = false;
}

void MapRhiGlobeRenderer::releaseResources()
{
    releaseVisibleHeatmapGpuBakeAtlasResources();
    releaseDiagnosticHeatmapGpuBakeResources();
    this->heatmap_gpu_bake_jobs.clear();
    this->heatmap_gpu_bake_instance_buffer.reset();
    this->heatmap_gpu_bake_instance_buffer_size = 0;
    this->heatmap_gpu_baking_disabled = false;
    this->diagnostic_heatmap_bake_instances.clear();
    this->diagnostic_heatmap_bake_cpu_reference = QImage();
    this->diagnostic_heatmap_bake_pending = false;
    this->diagnostic_heatmap_bake_revision = 0;
    this->diagnostic_heatmap_gpu_validation_attempted = false;
    this->heatmap_profile_report_pending = false;
    this->heatmap_array_draw_batches.clear();
    this->heatmap_array_template_bindings.reset();
    this->heatmap_array_pipeline.reset();
    this->heatmap_array_pages.clear();
    this->heatmap_array_draw_indices.clear();
    this->heatmap_array_draw_index_buffer.reset();
    this->heatmap_array_draw_index_buffer_size = 0;
    this->heatmap_array_draw_indices_dirty = true;
    this->heatmap_array_draw_index_upload_pending = false;
    this->heatmap_array_layer_buffer.reset();
    this->heatmap_array_layer_buffer_size = 0;
    this->heatmap_array_layer_upload_pending = true;
    this->array_pipeline.reset();
    this->tile_array_pages.clear();
    this->tile_array_draw_indices.clear();
    this->tile_array_draw_index_buffer.reset();
    this->tile_array_draw_index_buffer_size = 0;
    this->tile_array_draw_indices_dirty = true;
    this->tile_array_draw_index_upload_pending = false;
    this->pipeline.reset();
    this->wireframe_pipeline.reset();
    this->template_bindings.reset();
    this->wireframe_bindings.reset();
    this->dummy_texture.reset();
    this->dummy_texture_upload_pending = true;
    this->heatmap_dummy_texture.reset();
    this->heatmap_dummy_texture_upload_pending = true;
    this->sampler.reset();
    this->camera_uniform_buffer.reset();
    this->window_vertex_buffer.reset();
    this->window_index_buffer.reset();
    this->window_vertex_buffer_size = 0;
    this->window_index_buffer_size = 0;
    this->window_vertex_upload_pending = true;
    this->window_index_upload_pending = true;
    this->wireframe_vertex_buffer.reset();
    this->wireframe_vertex_buffer_size = 0;
    this->wireframe_vertex_upload_pending = true;
    this->window_dirty = true;
    this->terrain_lod_rebuild_pending = false;
    this->terrain_lod_rebuild_clock.invalidate();
    this->previously_subdivided_quadtree_nodes.clear();
    this->cap_vertex_buffer.reset();
    this->cap_index_buffer.reset();
    this->cap_vertex_upload_pending = true;
    this->cap_index_upload_pending = true;
    invalidateImagery();
}
