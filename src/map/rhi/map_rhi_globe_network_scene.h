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
// tank/reservoir/junction meshes, underground x-ray mode, and coincident-
// node decluttering (map_node_declutter.h). Follow-ups, not omissions this
// class silently papers over -- see MapRhiWidget::renderGlobe().
class MapRhiGlobeNetworkScene
{
public:
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

    const QVector<MapRhiScene::LinkVertex> &linkVertices() const;
    const QVector<MapRhiScene::NodeVertex> &nodeVertices() const;
    const QVector<MapRhiScene::LinkVertex> &selectedLinkVertices() const;
    const QVector<MapRhiScene::NodeVertex> &selectedNodeVertices() const;
    const QVector<MapRhiScene::LinkVertex> &diagnosticLinkVertices() const;
    const QVector<MapRhiScene::NodeVertex> &diagnosticNodeVertices() const;
    const QVector<MapRhiScene::IconVertex> &iconVertices() const;
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
};

#endif // MAP_RHI_GLOBE_NETWORK_SCENE_H
