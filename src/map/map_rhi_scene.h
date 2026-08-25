#ifndef MAP_RHI_SCENE_H
#define MAP_RHI_SCENE_H

#include "../network_render_snapshot.h"
#include "map_rhi_symbology.h"
#include "map_rhi_icon_atlas.h"

#include <QColor>
#include <QHash>
#include <QLineF>
#include <QPointF>
#include <QSet>
#include <QUuid>
#include <QVector>

class MapRhiScene
{
public:
    struct LinkVertex
    {
        float start_x = 0.0f;
        float start_y = 0.0f;
        float start_z = 0.0f;
        float end_x = 0.0f;
        float end_y = 0.0f;
        float end_z = 0.0f;
        float along = 0.0f;
        float side = 0.0f;
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 1.0f;
        float size_adjust_px = 0.0f;
        quint32 render_id = 0;
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    };

    struct IconVertex
    {
        float center_x = 0.0f;
        float center_y = 0.0f;
        float center_z = 0.0f;
        float offset_x_px = 0.0f;
        float offset_y_px = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 1.0f;
        quint32 render_id = 0;
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    };

    struct HeatmapVertex
    {
        float center_x = 0.0f;
        float center_y = 0.0f;
        float center_z = 0.0f;
        float corner_x = 0.0f;
        float corner_y = 0.0f;
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
    };

    struct NodeVertex
    {
        float center_x = 0.0f;
        float center_y = 0.0f;
        float center_z = 0.0f;
        float corner_x = 0.0f;
        float corner_y = 0.0f;
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 1.0f;
        float size_adjust_px = 0.0f;
        quint32 render_id = 0;
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    };

    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot);
    bool setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids);
    void setSymbology(const MapRhiSymbology &symbology);
    void setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid);
    bool setViewZoom(int zoom);
    void setSimulationErrorEntities(
        const QHash<QUuid, InfrastructureEntity> &error_entities,
        const QSet<QUuid> &stale_entity_uuids);

    const QVector<LinkVertex> &linkVertices() const;
    const QVector<NodeVertex> &nodeVertices() const;
    const QVector<LinkVertex> &selectedLinkVertices() const;
    const QVector<NodeVertex> &selectedNodeVertices() const;
    const QVector<LinkVertex> &diagnosticLinkVertices() const;
    const QVector<NodeVertex> &diagnosticNodeVertices() const;
    const QVector<LinkVertex> &flowDirectionVertices() const;
    const QVector<IconVertex> &iconVertices() const;
    const QVector<HeatmapVertex> &heatmapVertices() const;
    QPointF originWorld() const;
    quint64 geometryRevision() const;
    bool hasGeometry() const;
    int nodeSizePercent() const;
    int linkThicknessPx() const;

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

    struct LinkPath
    {
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        quint32 render_id = 0;
        QVector<QLineF> segments;
    };

    void rebuildNetworkGeometry();
    QPointF chooseOriginWorld(const NetworkRenderSnapshot &snapshot) const;
    QPointF localWorldPosition(const CoordinateWGS84 &coordinate, double wrap_reference_x,
                               double *resolved_world_x) const;
    void appendLinkSegment(InfrastructureEntity entity_type, quint32 render_id,
                           const QPointF &start, const QPointF &end);
    void appendNode(InfrastructureEntity entity_type, quint32 render_id, const QPointF &center);
    void applyLinkColor(LinkVertex *vertex) const;
    void applyNodeColor(NodeVertex *vertex) const;
    void rebuildHeatmap();
    void appendHeatmap(const HeatmapMarker &marker);
    void rebuildIcons();
    void appendIcon(const IconMarker &marker);
    void rebuildFlowDirections();
    void appendFlowDirectionStroke(
        const QPointF &start, const QPointF &end, QRgb color, float half_width_px);
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
    QVector<HeatmapMarker> heatmap_markers;
    QVector<IconMarker> icon_markers;
    QVector<LinkPath> link_paths;
    QPointF origin_world;
    MapRhiSymbology symbology;
    InfrastructureEntity selected_entity_type = InfrastructureEntity::Unknown;
    QUuid selected_entity_uuid;
    QHash<QUuid, InfrastructureEntity> simulation_error_entities;
    QSet<QUuid> simulation_stale_entity_uuids;
    QHash<QUuid, quint64> entity_keys_by_uuid;
    QHash<quint64, QVector<int>> link_vertex_indices_by_entity;
    QHash<quint64, QVector<int>> node_vertex_indices_by_entity;
    quint64 geometry_revision = 0;
    int view_zoom = 0;
    bool origin_valid = false;
};

#endif // MAP_RHI_SCENE_H
