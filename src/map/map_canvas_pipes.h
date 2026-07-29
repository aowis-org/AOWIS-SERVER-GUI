#ifndef MAP_CANVAS_PIPES_H
#define MAP_CANVAS_PIPES_H

#include <optional>
#include <QObject>
#include <QList>
#include <QPainter>
#include <QPointer>
#include <QPoint>
#include <QPointF>
#include <QUuid>

#include "map_entity_marker_label.h"
#include "map_model.h"
#include "map_models.h"

class MapCanvasWidget;

class MapCanvasPipes : public QObject
{
    Q_OBJECT
    
public:
    struct PipeVertexHit
    {
        QUuid pipe_uuid;
        int vertex_index = -1;
        
        bool isValid() const;
    };
    
    struct PipeSegmentHit
    {
        QUuid pipe_uuid;
        int insert_index = -1;
        QPointF nearest_point;
        
        bool isValid() const;
    };
    
    explicit MapCanvasPipes(MapModel *map_model, MapCanvasWidget *map_canvas, QObject *parent = nullptr);
    
    void clearPlacement();
    bool hasStartLabel() const;
    MapEntityMarkerLabel *startLabel() const;
    void startPipe(MapEntityMarkerLabel *start_label);
    void appendIntermediateVertex(const CoordinateWGS84 &coordinate);
    QList<CoordinateWGS84> intermediateVertices() const;
    QList<CoordinateWGS84> intermediateVertices(const QUuid &pipe_uuid) const;
    bool completePipe(const InfrastructureEntityReference &pipe_reference, const InfrastructureEntityReference &start_node,
                      const InfrastructureEntityReference &end_node, MapEntityMarkerLabel *end_label);
    
    void paint(QPainter &paint,
               const QList<MapEntityMarker> &markers,
               bool placing_pipe,
               const QPointF &mouse_position,
               MapEntityMarkerLabel *connection_target_label) const;
    
    bool hasSelection() const;
    QList<QUuid> selectedPipeUuids() const;
    void clearSelection();
    std::optional<InfrastructureEntityReference> selectPipe(const QUuid &pipe_uuid);
    bool removePipe(const QUuid &pipe_uuid);
    void selectPipesWithSelectedEndpoints(const QList<MapEntityMarker> &selected_markers);
    void moveIntermediateVerticesWithSelectedEndpoints(const QList<MapEntityMarker> &selected_markers,
                                                       double longitude_delta,
                                                       double latitude_delta);
    
    std::optional<InfrastructureEntityReference> pipeAt(
        const QPointF &position, const QList<MapEntityMarker> &markers) const;
    PipeVertexHit pipeVertexAt(const QPointF &position) const;
    PipeSegmentHit pipeSegmentAt(const QPointF &position,
                                 const QList<MapEntityMarker> &markers) const;
    bool showContextMenuAt(const QPointF &position, const QPoint &global_position, const QList<MapEntityMarker> &markers);
    
    bool addPipeVertex(const QUuid &pipe_uuid, int insert_index,
                       const CoordinateWGS84 &coordinate);
    bool deletePipeVertex(const QUuid &pipe_uuid, int vertex_index);
    bool setIntermediateVertices(const QUuid &pipe_uuid,
                                 const QList<CoordinateWGS84> &intermediate_vertices);
    std::optional<CoordinateWGS84> pipeVertexCoordinate(const QUuid &pipe_uuid,
                                                        int vertex_index) const;
    bool splitPipeAtVertex(const QUuid &pipe_uuid, int vertex_index, const InfrastructureEntityReference &junction_reference,
                           const InfrastructureEntityReference &second_pipe_reference, MapEntityMarkerLabel *junction_label);
    
    bool startPipeVertexMove(const QUuid &pipe_uuid, int vertex_index);
    bool isPipeVertexMoveActive() const;
    std::optional<QUuid> activePipeVertexMoveUuid() const;
    int activePipeVertexMoveIndex() const;
    bool updatePipeVertexMove(const QPointF &screen_position);
    bool finishPipeVertexMove(const QPointF &screen_position);
    void cancelPipeVertexMove();
    
    void removeConnectedToLabel(MapEntityMarkerLabel *label);
    
signals:
    void pipeSelectionRequested(const QUuid &pipe_uuid);
    void pipeVertexAddRequested(const QUuid &pipe_uuid, int insert_index,
                                const CoordinateWGS84 &coordinate);
    void pipeVertexDeleteRequested(const QUuid &pipe_uuid, int vertex_index);
    void pipeVertexMoveRequested(const QUuid &pipe_uuid, int vertex_index);
    void pipeVertexConversionRequested(const QUuid &pipe_uuid, int vertex_index);
    
private:
    struct PipeCanvasItem
    {
        InfrastructureEntityReference entity;
        PipeGeometry geometry;
        QPointer<MapEntityMarkerLabel> start_label;
        QPointer<MapEntityMarkerLabel> end_label;
        bool selected = false;
    };
    
    PipeCanvasItem *pipeByUuid(const QUuid &pipe_uuid);
    const PipeCanvasItem *pipeByUuid(const QUuid &pipe_uuid) const;
    int pipeIndexByUuid(const QUuid &pipe_uuid) const;
    MapEntityMarker markerByLabel(MapEntityMarkerLabel *label,
                                  const QList<MapEntityMarker> &markers) const;
    bool markerIsSelected(MapEntityMarkerLabel *label,
                          const QList<MapEntityMarker> &selected_markers) const;
    void updateCanvas();
    
    MapModel *map_model = nullptr;
    QPointer<MapCanvasWidget> map_canvas;
    QList<PipeCanvasItem> list_pipes;
    
    QPointer<MapEntityMarkerLabel> pipe_start_label;
    QList<CoordinateWGS84> pipe_intermediate_vertices;
    
    std::optional<QUuid> pipe_vertex_move_pipe_uuid;
    int pipe_vertex_move_index = -1;
};

#endif // MAP_CANVAS_PIPES_H
