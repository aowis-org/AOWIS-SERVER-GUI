#ifndef MAP_RHI_GLOBE_NETWORK_SCENE_H
#define MAP_RHI_GLOBE_NETWORK_SCENE_H

#include "network/network_render_snapshot.h"
#include "map/rhi/map_rhi_symbology.h"
#include "map/rhi/map_rhi_scene.h"
#include "map/rhi/map_rhi_junction_model.h"
#include "map/rhi/map_rhi_reservoir_model.h"
#include "map/rhi/map_rhi_tank_model.h"
#include "map/render/map_globe_vertical_transform.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <QColor>
#include <QHash>
#include <QPointF>
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
// WGS84 ellipsoid in ECEF meters instead of MapRhiScene's local Web Mercator
// tangent-plane world units. There is no antimeridian seam to manage here --
// ECEF coordinates are globally unambiguous -- but there *is* still an
// origin, for a different reason than MapRhiScene's: MapRhiScene's
// origin_world exists to give TwoD/ThreeD a manageable coordinate range in
// the first place (Web Mercator world units span the whole projected
// planet), whereas ECEF's origin (Earth's center) already gives every
// vertex a magnitude of ~6.378e6 m regardless of where on the planet it is
// -- far enough from zero that float32 (which every RHI vertex buffer here
// ultimately is) only has ~0.5-1m of resolution left once you're at that
// magnitude, before any camera-relative arithmetic is even done. See
// setRenderOriginEcef() and ecefPosition() for how this class avoids that:
// every vertex is placed relative to a caller-supplied ECEF origin (kept in
// sync with whatever MapRhiCamera's Globe view/projection matrix itself
// rendered relative to) rather than at its raw ECEF position, so the
// float32 narrowing happens only after the large-magnitude part of the
// position has already been subtracted off in double precision.
//
// Deliberately reuses MapRhiScene::LinkVertex/NodeVertex/IconVertex
// byte-for-byte (rather than declaring parallel structs) so MapRhiWidget can
// draw this geometry with the exact same RHI pipelines/shaders
// (map_rhi_link/node/icon.vert+.frag) it already built for the ThreeD view.
// Most of those shader paths compute stroke thickness/node radius/icon size
// in screen-space pixels purely from the projected clip-space positions of
// the vertices they are given, so flat and Globe geometry can share them
// unchanged. The one path that needs Globe-specific geometric input is the
// optional "size in meters" path in map_rhi_link.vert.
// Globe link vertices provide that shader with a per-segment local tangent
// width direction derived from the WGS84 east/north/up frame, so a metre of
// pipe width means a metre sideways along the local Earth surface rather
// than along the raw global ECEF XY plane. Long digitized spans are also
// adaptively subdivided along the WGS84 geodesic before those vertices are
// built, preventing one long ECEF chord from cutting through the curved
// Earth. Flat TwoD/ThreeD vertices leave the optional tangent direction zero
// and retain their legacy XY-plane behavior.
//
// Coincident-node decluttering uses the same one-metre policy as the flat
// RHI scene, but clusters nodes in a local east/north tangent plane and
// applies the resulting metre offsets back on WGS84 before ECEF conversion.
// Junctions are analytic sphere impostors -- see junctionInstances() -- and
// tanks/reservoirs can be represented by their Globe-oriented 3D models.
// Underground X-Ray classifies both link segments and junctions against the
// visible terrain surface, matching the retained planar ThreeD behavior.
// Solid mode remains the mode for seeing the complete network through terrain.
class MapRhiGlobeNetworkScene
{
public:
    // Read-only terrain lookup at an arbitrary coordinate, used solely to
    // classify link segments and junctions as underground for X-Ray mode. In addition to
    // elevation it reports the effective terrain cell size at that point so
    // classification density follows terrain LOD. Never influences where
    // any geometry is actually placed.
    using TerrainElevationResolver = std::function<bool(
        const CoordinateWGS84 &coordinate,
        double *elevation_m,
        double *cell_size_m)>;

    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot);
    bool setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids);
    void setSymbology(const MapRhiSymbology &symbology);
    void setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid);
    void setSimulationErrorEntities(
        const QHash<QUuid, InfrastructureEntity> &error_entities,
        const QSet<QUuid> &stale_entity_uuids);
    bool setUse3dIconModels(bool enabled);
    bool setNodeDeclutteringEnabled(bool enabled);
    // Height, in meters, added above each entity's own elevation before it is
    // placed on the ellipsoid -- the Globe counterpart of
    // MapRhiScene::setNetworkGroundOffsetM(), sharing the same
    // MapModel::view3dNetworkGroundOffsetM() control since both exist to
    // keep network geometry from z-fighting with the terrain mesh beneath
    // it. Returns true if the (bounded) value actually changed.
    bool setGroundOffsetM(double offset_m);
    // Updates the shared Globe vertical transform used by terrain and network
    // placement. Returns true if the (bounded) value actually changed.
    bool setVerticalExaggeration(double exaggeration);
    // Injects the read-only terrain-elevation lookup used by X-Ray
    // classification (see setUndergroundXRayEnabled()). Safe to leave unset
    // -- X-Ray will simply never find anything to highlight without it.
    // Set once by MapRhiWidget's constructor; the resolver closure itself
    // always queries live repository state, so there's no need to re-set it
    // as terrain tiles load in.
    void setTerrainElevationResolver(TerrainElevationResolver resolver);
    // While true, rebuildNetworkGeometry() also walks each link in short
    // subdivisions, compares each subdivision's own implied elevation
    // against the resolver's terrain sample at the same point, and
    // collects the contiguous "below terrain" runs into
    // undergroundLinkVertices()/undergroundJunctionInstances(), for the
    // caller to draw through terrain with no-depth-test X-Ray pipelines
    // (mirroring MapRhiWidget's own ThreeD underground geometry). A plain bool rather than
    // MapRhiWidget's MapRhiUndergroundMode enum, to avoid a
    // widget<->scene header cycle -- the caller maps XRay to true and
    // Hide/Solid to false ("Solid" needs no per-segment classification at
    // all; see MapRhiWidget::drawGlobeNetwork()). Returns true if changed.
    bool setUndergroundXRayEnabled(bool enabled);
    // Re-runs only the underground classification against the terrain
    // resolver's current cache. Normal Globe network geometry is left
    // untouched, so terrain streaming cannot force a full network rebuild.
    // Returns true when classification was performed.
    bool refreshUndergroundXRayGeometry();
    // The ECEF point every vertex this class builds is placed *relative
    // to*, in place of the raw (Earth-center-relative) ECEF position
    // GeoWgs84Ellipsoid::geodeticToEcef() would otherwise hand back
    // directly. Must be kept equal to whatever MapRhiCamera::
    // globeNetworkViewProjectionMatrix() itself rendered relative to for
    // this frame (see MapRhiCamera::globeRenderOriginEcef()/
    // updateGlobeRenderOrigin()) -- the caller (MapRhiWidget::
    // renderGlobe()) is responsible for that, every frame, before drawing.
    // Globe terrain uses this exact origin and matrix too. Keeping both
    // geometry sets in one coordinate frame is required for stable and
    // directly comparable depth during pan/orbit; raw float32 ECEF terrain
    // would reintroduce the precision loss described in ecefPosition().
    // Comparing the previous and new origin for exact equality (rather
    // than some epsilon) is intentional and correct: globeRenderOriginEcef()
    // is a sticky value that MapRhiCamera::updateGlobeRenderOrigin() only
    // ever updates by wholesale replacement (never incrementally nudged),
    // so as long as the camera hasn't drifted far enough to trigger a
    // rebase -- the common case, true for the entire duration of ordinary
    // orbiting/panning around one local area -- every call here sees the
    // exact same double bit pattern as last time, letting this skip the
    // (comparatively expensive) full geometry rebuild on every such frame
    // and only actually rebuild on the rare frame where a rebase just
    // happened. Returns true if the origin actually changed (and geometry
    // was rebuilt).
    bool setRenderOriginEcef(const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef);
    // Screen scale at the Globe orbit target, used to give flow-direction
    // chevrons the same pixel-based length, spacing, and terrain clearance
    // as their ThreeD counterparts. Returns true when the scale changed and
    // flowDirectionVertices() was rebuilt.
    bool setFlowDirectionPixelsPerMeter(double pixels_per_meter);

    const QVector<MapRhiScene::LinkVertex> &linkVertices() const;
    const QVector<MapRhiScene::NodeVertex> &nodeVertices() const;
    const QVector<MapRhiScene::LinkVertex> &selectedLinkVertices() const;
    const QVector<MapRhiScene::NodeVertex> &selectedNodeVertices() const;
    const QVector<MapRhiScene::LinkVertex> &diagnosticLinkVertices() const;
    const QVector<MapRhiScene::NodeVertex> &diagnosticNodeVertices() const;
    const QVector<MapRhiScene::LinkVertex> &flowDirectionVertices() const;
    const QVector<MapRhiScene::IconVertex> &iconVertices() const;
    const QVector<MapRhiTankInstance> &tankInstances() const;
    const QVector<MapRhiReservoirInstance> &reservoirInstances() const;
    const QVector<MapRhiScene::LinkVertex> &undergroundLinkVertices() const;
    const QVector<MapRhiJunctionInstance> &undergroundJunctionInstances() const;
    // Analytic sphere-impostor instances for junction entities -- see the class
    // comment above. Drawn with the exact same MapRhiJunctionInstance
    // layout and impostor quad (mapRhiJunctionImpostorVertices())
    // MapRhiWidget already built for ThreeD. Junction entities are NOT also present in
    // nodeVertices() with a visible alpha -- see applyNodeColor() -- so
    // they render exactly once, as an impostor, never as a flat marker
    // underneath it.
    const QVector<MapRhiJunctionInstance> &junctionInstances() const;
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

    // Mirrors MapRhiScene::JunctionMarker -- a lightweight record of just
    // what rebuildJunctionInstances() needs (render_id and position),
    // kept separate from MapRhiJunctionInstance, which additionally carries
    // the radius and stable GPU style-table index.
    struct JunctionMarker
    {
        quint32 render_id = 0;
        QVector3D center;
    };

    struct SceneSegment
    {
        QVector3D start;
        QVector3D end;
        float length_m = 0.0f;
    };

    struct LinkPath
    {
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        quint32 render_id = 0;
        QVector<SceneSegment> segments;
        double total_length_m = 0.0;
    };

    void rebuildNetworkGeometry();
    void rebuildUndergroundXRayGeometry();
    QVector3D ecefPosition(const CoordinateWGS84 &coordinate, double elevation_m) const;
    void appendLinkSegment(InfrastructureEntity entity_type, quint32 render_id,
                           const QVector3D &start, const QVector3D &end);
    QVector3D linkWidthDirection(
        const QVector3D &start, const QVector3D &end) const;
    void appendNode(InfrastructureEntity entity_type, quint32 render_id,
                    const QVector3D &center);
    void applyLinkColor(MapRhiScene::LinkVertex *vertex) const;
    void applyNodeColor(MapRhiScene::NodeVertex *vertex) const;
    void rebuildFlowDirections();
    void appendFlowDirectionStroke(
        const QVector3D &start, const QVector3D &end,
        QRgb color, float half_width_px);
    QVector3D ellipsoidNormalAt(const QVector3D &relative_ecef) const;
    QRgb flowDirectionColor(quint32 render_id) const;
    void rebuildIcons();
    void appendIcon(const IconMarker &marker);
    void rebuildTankInstances();
    void rebuildReservoirInstances();
    bool modelBasisAt(
        const QVector3D &center,
        QVector3D *basis_x,
        QVector3D *basis_y,
        QVector3D *basis_z) const;
    // Rebuilds compact placement/radius/style-index instances from the
    // junction markers. Color, selection, diagnostics and visibility now
    // live in MapRhiScene's shared GPU style table.
    void rebuildJunctionInstances();
    void rebuildHighlights();
    void appendEntityHighlight(InfrastructureEntity entity_type, quint32 render_id,
                               const QColor &color, float link_size_adjust_px,
                               float node_size_adjust_px,
                               QVector<MapRhiScene::LinkVertex> *link_target,
                               QVector<MapRhiScene::NodeVertex> *node_target) const;
    void appendUndergroundLinkSegment(InfrastructureEntity entity_type, quint32 render_id,
                                      const QVector3D &start, const QVector3D &end);
    void appendUndergroundCurvedRun(
        InfrastructureEntity entity_type, quint32 render_id,
        const CoordinateWGS84 &start_coordinate, double start_elevation_m,
        const QVector3D &start_ecef,
        const CoordinateWGS84 &end_coordinate, double end_elevation_m,
        const QVector3D &end_ecef);
    // Subdivides one already-placed link segment (from consecutive digitized
    // vertices) into short spans and appends each contiguous "below terrain"
    // run to underground_link_vertices. The classification interval follows
    // the terrain cell size resolved for the currently rendered Globe LOD,
    // mirroring the adaptive policy used by the legacy planar ThreeD path.
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
    QVector<MapRhiScene::LinkVertex> flow_direction_vertices;
    QVector<MapRhiScene::IconVertex> icon_vertices;
    QVector<IconMarker> icon_markers;
    QVector<MapRhiTankInstance> tank_instances;
    QVector<MapRhiReservoirInstance> reservoir_instances;
    QVector<JunctionMarker> junction_markers;
    QVector<MapRhiJunctionInstance> junction_instances;
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
    // Deliberately NOT default-constructed to a plausible ECEF value (e.g.
    // the equator/prime-meridian point) -- (0,0,0) is Earth's *center*,
    // nowhere any real network geometry could ever legitimately be placed
    // relative to, so the very first setRenderOriginEcef() call (made
    // before the first rebuildNetworkGeometry() -- see
    // MapRhiWidget::renderGlobe()) is guaranteed to see a change and
    // rebuild, rather than possibly matching a real starting origin by
    // coincidence and skipping a rebuild that needs to happen.
    GeoWgs84Ellipsoid::EcefPositionD render_origin_ecef;
    MapGlobeVerticalTransform vertical_transform;
    double flow_direction_pixels_per_meter = 0.0;
    TerrainElevationResolver terrain_elevation_resolver;
    bool underground_xray_enabled = false;
    bool use_3d_icon_models = false;
    bool node_decluttering_enabled = true;
    QHash<quint32, QPointF> node_declutter_offsets_m;
    double fallback_elevation_m = 0.0;
    QVector<MapRhiScene::LinkVertex> underground_link_vertices;
    QVector<MapRhiJunctionInstance> underground_junction_instances;
};

#endif // MAP_RHI_GLOBE_NETWORK_SCENE_H
