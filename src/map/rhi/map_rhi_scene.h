#ifndef MAP_RHI_SCENE_H
#define MAP_RHI_SCENE_H

#include "network/network_render_snapshot.h"
#include "map/render/map_network_render_symbology.h"
#include "map/render/map_icon_atlas.h"
#include "map/rhi/map_rhi_network_style.h"
#include "map/render/map_network_render_data.h"

#include <QColor>
#include <QHash>
#include <QLineF>
#include <QPointF>
#include <QSet>
#include <QUuid>
#include <QVector>
#include <QVector3D>

class MapRhiScene
{
public:
    // Local shorthand for the backend-neutral retained vertex layouts.
    using LinkVertex = MapNetworkLinkVertex;
    using IconVertex = MapNetworkIconVertex;
    using HeatmapVertex = MapNetworkHeatmapVertex;
    using NodeVertex = MapNetworkNodeVertex;

    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot);
    bool setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids);
    void setSymbology(const MapNetworkRenderSymbology &symbology);
    void setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid);
    bool setViewZoom(int zoom);
    void setSimulationErrorEntities(
        const QHash<QUuid, InfrastructureEntity> &error_entities,
        const QSet<QUuid> &stale_entity_uuids);
    bool setNodeDeclutteringEnabled(bool enabled);

    const QVector<LinkVertex> &linkVertices() const;
    const QVector<NodeVertex> &nodeVertices() const;
    const QVector<LinkVertex> &selectedLinkVertices() const;
    const QVector<NodeVertex> &selectedNodeVertices() const;
    const QVector<LinkVertex> &diagnosticLinkVertices() const;
    const QVector<NodeVertex> &diagnosticNodeVertices() const;
    const QVector<LinkVertex> &flowDirectionVertices() const;
    const QVector<IconVertex> &iconVertices() const;
    const QVector<HeatmapVertex> &heatmapVertices() const;
    const MapRhiNetworkStyleTable &networkStyleTable() const;
    QPointF originWorld() const;
    const NetworkRenderSnapshot &networkSnapshot() const;
    QVector3D worldPosition(const CoordinateWGS84 &coordinate,
                            double wrap_reference_x,
                            double *resolved_world_x = nullptr) const;
    bool isEntityHidden(const QUuid &uuid) const;
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
    double worldUnitsPerMeter() const;

private:
    struct HeatmapMarker
    {
        quint32 render_id = 0;
        QPointF center;
    };

    struct IconMarker
    {
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        quint32 render_id = 0;
        QPointF center;
    };

    struct SceneSegment
    {
        QPointF start;
        QPointF end;
    };

    struct LinkPath
    {
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        quint32 render_id = 0;
        QVector<SceneSegment> segments;
    };

    void rebuildNetworkGeometry();
    QPointF chooseOriginWorld(const NetworkRenderSnapshot &snapshot) const;
    QPointF localWorldPosition(const CoordinateWGS84 &coordinate, double wrap_reference_x,
                               double *resolved_world_x) const;
    void appendLinkSegment(InfrastructureEntity entity_type, quint32 render_id,
                           const QPointF &start, const QPointF &end);
    void appendNode(InfrastructureEntity entity_type, quint32 render_id,
                    const QPointF &center);
    void applyLinkColor(LinkVertex *vertex) const;
    void applyNodeColor(NodeVertex *vertex) const;
    void rebuildHeatmap();
    void appendHeatmap(const HeatmapMarker &marker);
    void rebuildIcons();
    void rebuildNetworkStyles();
    void appendIcon(const IconMarker &marker);
    void rebuildFlowDirections();
    void appendFlowDirectionStroke(
        const QPointF &start, const QPointF &end, float z,
        QRgb color, float half_width_px);
    QRgb flowDirectionColor(quint32 render_id) const;
    void rebuildHighlights();
    void appendEntityHighlight(InfrastructureEntity entity_type, quint32 render_id,
                               const QColor &color, float link_size_adjust_px,
                               float node_size_adjust_px,
                               QVector<LinkVertex> *link_target,
                               QVector<NodeVertex> *node_target) const;

    NetworkRenderSnapshot network_snapshot;
    QSet<QUuid> hidden_entity_uuids;
    QVector<LinkVertex> link_vertices;
    QVector<NodeVertex> node_vertices;
    QVector<LinkVertex> selected_link_vertices;
    QVector<NodeVertex> selected_node_vertices;
    QVector<LinkVertex> diagnostic_link_vertices;
    QVector<NodeVertex> diagnostic_node_vertices;
    QVector<LinkVertex> flow_direction_vertices;
    QVector<IconVertex> icon_vertices;
    QVector<HeatmapVertex> heatmap_vertices;
    MapRhiNetworkStyleTable network_style_table;
    QVector<HeatmapMarker> heatmap_markers;
    QVector<IconMarker> icon_markers;
    QVector<LinkPath> link_paths;
    QPointF origin_world;
    MapNetworkRenderSymbology symbology;
    InfrastructureEntity selected_entity_type = InfrastructureEntity::Unknown;
    QUuid selected_entity_uuid;
    QHash<QUuid, InfrastructureEntity> simulation_error_entities;
    QSet<QUuid> simulation_stale_entity_uuids;
    QHash<QUuid, quint64> entity_keys_by_uuid;
    QHash<quint64, QVector<int>> link_vertex_indices_by_entity;
    QHash<quint64, QVector<int>> node_vertex_indices_by_entity;
    quint64 geometry_revision = 0;
    int view_zoom = 0;
    double reference_latitude_deg = 0.0;
    bool origin_valid = false;
    bool node_decluttering_enabled = true;
};

#endif // MAP_RHI_SCENE_H
