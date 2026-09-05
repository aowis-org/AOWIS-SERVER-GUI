#ifndef MAP_RHI_GLOBE_NETWORK_SCENE_H
#define MAP_RHI_GLOBE_NETWORK_SCENE_H

#include "network/network_render_snapshot.h"
#include "map/rhi/map_rhi_symbology.h"
#include "map/rhi/map_rhi_scene.h"

#include <QColor>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QVector>
#include <QVector3D>

#include <functional>

// Builds EPANET network render geometry (pipes, junctions, tanks,
// reservoirs, pumps, valves) for the "Globe" map view mode.
//
// This mirrors MapRhiScene (map_rhi_scene.h), which does the equivalent job
// for the flat TwoD/ThreeD views, but every vertex is placed on the actual
// WGS84 ellipsoid in ECEF meters (via GeoWgs84Ellipsoid::geodeticToEcef())
// instead of MapRhiScene's local Web Mercator tangent-plane world units.
// There is consequently no "origin_world"/wrap-reference bookkeeping here:
// ECEF coordinates are globally unambiguous, so every node/link vertex is
// converted independently and there is no antimeridian seam to manage.
//
// Deliberately reuses MapRhiScene::LinkVertex/NodeVertex/IconVertex
// byte-for-byte (rather than declaring parallel structs) so MapRhiWidget can
// draw this geometry with the exact same RHI pipelines/shaders
// (map_rhi_link/node/icon.vert+.frag) it already built for the ThreeD view.
// Those vertex shaders compute stroke thickness/node radius/icon size in
// screen-space pixels purely from the projected clip-space positions of the
// vertices they are given -- they do not know or care whether the world
// positions they were handed came from a flat tangent plane or an
// ellipsoid -- so no shader changes are needed to reuse them here. The one
// exception is the optional "size in meters" path in map_rhi_link.vert,
// which derives a world-space perpendicular offset from the raw XY
// components of the link direction; that shortcut assumes a horizontal
// ground plane (true for the flat ThreeD view) and is only an approximation
// on the curved globe. Meters-unit link/node/icon sizing is still passed
// through unmodified for consistency with MapRhiScene, since on a
// planet-scale view the resulting error is visually negligible outside
// extreme grazing angles.
//
// Not yet implemented for the globe (all render as their flat pixel-marker
// fallback, matching what the ThreeD view already does when its 3D-model
// toggles are off): the heatmap overlay, flow-direction chevrons, 3D
// tank/reservoir/junction meshes, and coincident-node decluttering
// (map_node_declutter.h). Underground X-Ray *is* implemented, but for links
// only -- see setUndergroundXRayEnabled() -- since ThreeD's underground
// junction indicator is tied to its 3D junction mesh model, which the globe
// doesn't have yet either. Follow-ups, not omissions this class silently
// papers over -- see MapRhiWidget::renderGlobe().
class MapRhiGlobeNetworkScene
{
public:
    // Read-only elevation lookup at an arbitrary coordinate, used solely to
    // classify link segments as underground for X-Ray mode -- see
    // setUndergroundXRayEnabled(). Never influences where any geometry is
    // actually placed.
    using TerrainElevationResolver =
        std::function<bool(const CoordinateWGS84 &coordinate, double *elevation_m)>;

    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot);
    bool setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids);
    void setSymbology(const MapRhiSymbology &symbology);
    void setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid);
    void setSimulationErrorEntities(
        const QHash<QUuid, InfrastructureEntity> &error_entities,
        const QSet<QUuid> &stale_entity_uuids);
    // Height, in meters, added above each entity's own elevation before it is
    // placed on the ellipsoid -- the Globe counterpart of
    // MapRhiScene::setNetworkGroundOffsetM(), sharing the same
    // MapModel::view3dNetworkGroundOffsetM() control since both exist to
    // keep network geometry from z-fighting with the terrain mesh beneath
    // it. Returns true if the (bounded) value actually changed.
    bool setGroundOffsetM(double offset_m);
    // Multiplies each entity's elevation before placement, mirroring how
    // MapRhiGlobeRenderer's own terrain mesh scales DEM elevation by this
    // same MapModel::view3dVerticalExaggeration() factor (see
    // globeTerrainPositionAt() in map_rhi_basemap_renderer.cpp). Terrain and
    // network must apply the identical factor or they drift apart the
    // moment exaggeration != 1 -- ThreeD avoids this by literally reusing
    // MapRhiScene's own affine coefficients to place its terrain mesh, but
    // the Globe terrain mesh does not share code with this class, so the
    // factor has to be kept in sync by hand here instead. Returns true if
    // the (bounded) value actually changed.
    bool setVerticalExaggeration(double exaggeration);
    // Holds every entity at the bare ellipsoid surface (elevation treated as
    // 0) while false, regardless of its real elevation_m. This exists for
    // exactly one reason: imagery tiles for a newly-visible area arrive
    // before the DEM/relief mesh for the same tiles does (the latter is
    // fetched and meshed on a background thread -- see
    // MapRhiGlobeRenderer::isVisibleTerrainReady()), so anything drawn at
    // its real elevation during that window appears to float above the
    // still-flat terrain until relief catches up. The caller (see
    // MapRhiWidget::renderGlobe()) feeds this from
    // MapRhiGlobeRenderer::isVisibleTerrainReady() every frame; flipping it
    // rebuilds geometry with the entities' real elevation once relief has
    // actually loaded. Returns true if the value actually changed.
    bool setTerrainReady(bool ready);
    // Injects the read-only terrain-elevation lookup used by X-Ray
    // classification (see setUndergroundXRayEnabled()). Safe to leave unset
    // -- X-Ray will simply never find anything to highlight without it.
    // Set once by MapRhiWidget's constructor; the resolver closure itself
    // always queries live repository state, so there's no need to re-set it
    // as terrain tiles load in.
    void setTerrainElevationResolver(TerrainElevationResolver resolver);
    // While true (and only once terrain is ready -- see setTerrainReady()),
    // rebuildNetworkGeometry() also walks each link in short subdivisions,
    // compares each subdivision's own implied elevation against the
    // resolver's terrain sample at the same point, and collects the
    // contiguous "below terrain" runs into undergroundLinkVertices(), for
    // the caller to draw through terrain with a no-depth-test pipeline
    // (mirroring MapRhiWidget's own ThreeD-only underground_link_vertices).
    // A plain bool rather than MapRhiWidget's MapRhiUndergroundMode enum, to
    // avoid a widget<->scene header cycle -- the caller maps XRay to true
    // and Hide/Solid to false ("Solid" needs no per-segment classification
    // at all; see MapRhiWidget::drawGlobeNetwork()). Returns true if
    // changed.
    bool setUndergroundXRayEnabled(bool enabled);

    const QVector<MapRhiScene::LinkVertex> &linkVertices() const;
    const QVector<MapRhiScene::NodeVertex> &nodeVertices() const;
    const QVector<MapRhiScene::LinkVertex> &selectedLinkVertices() const;
    const QVector<MapRhiScene::NodeVertex> &selectedNodeVertices() const;
    const QVector<MapRhiScene::LinkVertex> &diagnosticLinkVertices() const;
    const QVector<MapRhiScene::NodeVertex> &diagnosticNodeVertices() const;
    const QVector<MapRhiScene::IconVertex> &iconVertices() const;
    const QVector<MapRhiScene::LinkVertex> &undergroundLinkVertices() const;
    quint64 geometryRevision() const;
    bool hasGeometry() const;

    NetworkSymbologySizeUnit nodeSizeUnit() const;
    int nodeSizePx() const;
    double nodeSizeM() const;
    NetworkSymbologySizeUnit iconSizeUnit() const;
    int iconSizePx() const;
    double iconSizeM() const;
    NetworkSymbologySizeUnit linkThicknessUnit() const;
    int linkThicknessPx() const;
    double linkThicknessM() const;

private:
    struct IconMarker
    {
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        quint32 render_id = 0;
        QVector3D center;
    };

    struct SceneSegment
    {
        QVector3D start;
        QVector3D end;
    };

    struct LinkPath
    {
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        quint32 render_id = 0;
        QVector<SceneSegment> segments;
    };

    void rebuildNetworkGeometry();
    QVector3D ecefPosition(const CoordinateWGS84 &coordinate, double elevation_m) const;
    void appendLinkSegment(InfrastructureEntity entity_type, quint32 render_id,
                           const QVector3D &start, const QVector3D &end);
    void appendNode(InfrastructureEntity entity_type, quint32 render_id,
                    const QVector3D &center);
    void applyLinkColor(MapRhiScene::LinkVertex *vertex) const;
    void applyNodeColor(MapRhiScene::NodeVertex *vertex) const;
    void rebuildIcons();
    void appendIcon(const IconMarker &marker);
    void rebuildHighlights();
    void appendEntityHighlight(InfrastructureEntity entity_type, quint32 render_id,
                               const QColor &color, float link_size_adjust_px,
                               float node_size_adjust_px,
                               QVector<MapRhiScene::LinkVertex> *link_target,
                               QVector<MapRhiScene::NodeVertex> *node_target) const;
    void appendUndergroundLinkSegment(InfrastructureEntity entity_type, quint32 render_id,
                                      const QVector3D &start, const QVector3D &end);
    // Subdivides one already-placed link segment (from consecutive digitized
    // vertices) into short spans and appends each contiguous "below terrain"
    // run to underground_link_vertices. Coarser than MapRhiScene's ThreeD
    // equivalent, which sizes subdivisions to the actual terrain cell size;
    // this uses a fixed target length instead, since the globe has no single
    // "current terrain zoom" the way the flat view does (see the class
    // comment on globeTerrainElevationAtCoordinate() in map_rhi_widget.cpp).
    void appendUndergroundSubdivisions(
        InfrastructureEntity entity_type, quint32 render_id,
        const CoordinateWGS84 &start_coordinate, double start_elevation_m,
        const QVector3D &start_ecef,
        const CoordinateWGS84 &end_coordinate, double end_elevation_m,
        const QVector3D &end_ecef);

    NetworkRenderSnapshot network_snapshot;
    QSet<QUuid> hidden_entity_uuids;
    QVector<MapRhiScene::LinkVertex> link_vertices;
    QVector<MapRhiScene::NodeVertex> node_vertices;
    QVector<MapRhiScene::LinkVertex> selected_link_vertices;
    QVector<MapRhiScene::NodeVertex> selected_node_vertices;
    QVector<MapRhiScene::LinkVertex> diagnostic_link_vertices;
    QVector<MapRhiScene::NodeVertex> diagnostic_node_vertices;
    QVector<MapRhiScene::IconVertex> icon_vertices;
    QVector<IconMarker> icon_markers;
    QVector<LinkPath> link_paths;
    MapRhiSymbology symbology;
    InfrastructureEntity selected_entity_type = InfrastructureEntity::Unknown;
    QUuid selected_entity_uuid;
    QHash<QUuid, InfrastructureEntity> simulation_error_entities;
    QSet<QUuid> simulation_stale_entity_uuids;
    QHash<QUuid, quint64> entity_keys_by_uuid;
    QHash<quint64, QVector<int>> link_vertex_indices_by_entity;
    QHash<quint64, QVector<int>> node_vertex_indices_by_entity;
    quint64 geometry_revision = 0;
    double ground_offset_m = 0.0;
    double vertical_exaggeration = 1.0;
    bool terrain_ready = false;
    TerrainElevationResolver terrain_elevation_resolver;
    bool underground_xray_enabled = false;
    QVector<MapRhiScene::LinkVertex> underground_link_vertices;
};

#endif // MAP_RHI_GLOBE_NETWORK_SCENE_H
