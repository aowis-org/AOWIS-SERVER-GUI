#ifndef MAP_CANVAS_ENTITIES_H
#define MAP_CANVAS_ENTITIES_H

#include <QObject>
#include <QHash>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QUuid>

#include <optional>

#include "../_enums_structs.h"
#include "../hydraulic_data.h"
#include "map_editor_visual_state.h"
#include "map_entity_pixmap_renderer.h"
#include "map_models.h"

class MapCanvasDeviceLinks;
class MapCanvasMarkers;
class MapCanvasPipes;
class MapCanvasPlacement;
class MapCanvasSelection;
class MapCanvasWidget;
class MapModel;

enum RectangleSelectMode
{
    Add,
    Replace
};

class MapCanvasEntities : public QObject
{
    Q_OBJECT

public:
    explicit MapCanvasEntities(MapModel *map_model, HydraulicData *hydraulic_data,
                               MapCanvasWidget *map_canvas);

    void loadNetwork(const NetworkHydraulic &network);
    void startEntityPositioning(InfrastructureEntity entity);
    void stopEntityPositioning();
    bool cancelActiveMove();
    bool floatEntity(const QPointF &position);
    bool anchorMarker(const QPointF &position);

    void scaleMarkers();
    void positionMarkers();
    MapEditorVisualState visualState() const;

    bool selectMarkerAt(const QPointF &position);
    bool isMarkerAt(const QPointF &position) const;
    bool showMarkerContextMenuAt(const QPointF &position, const QPoint &global_position);
    bool selectDeviceLinkAt(const QPointF &position);
    bool isDeviceLinkAt(const QPointF &position) const;
    bool selectPipeAt(const QPointF &position);
    bool isPipeAt(const QPointF &position) const;
    bool showPipeContextMenuAt(const QPointF &position, const QPoint &global_position);

    std::optional<MapEntityMarker> markerByUuid(const QUuid &uuid) const;

private:
    bool anchorDeviceLink(const QPointF &position);
    bool anchorPipe(const QPointF &position);
    bool anchorPipeVertexMove(const QPointF &position);
    QUuid createHydraulicNode(InfrastructureEntity entity,
                              const CoordinateWGS84 &coordinate);
    QUuid createHydraulicDeviceLink(InfrastructureEntity entity,
                                    const DeviceLinkGeometry &geometry);
    bool deleteHydraulicNode(const InfrastructureEntityReference &reference);
    bool deleteHydraulicLink(const InfrastructureEntityReference &reference);
    void addPipeVertex(const QUuid &pipe_uuid, int insert_index,
                       const CoordinateWGS84 &coordinate);
    void deletePipeVertex(const QUuid &pipe_uuid, int vertex_index);
    bool synchronizeMarkerCoordinate(const QUuid &uuid);
    bool synchronizeSelectedGeometry();
    void captureMarkerMoveSnapshot(const QUuid &uuid);
    void captureSelectedMoveSnapshot();
    void capturePipeMoveSnapshot(const QUuid &pipe_uuid);
    void restoreMoveSnapshot();
    void clearMoveSnapshot();
    void updateConnectionTarget(const QPointF &mouse_position);
    void recalculateWrapReferenceLongitude();
    void setWrapReferenceLongitude(double longitude);
    void deleteMarker(const QUuid &uuid);
    void selectMarker(const QUuid &uuid);
    void selectPipe(const QUuid &pipe_uuid);
    void startMarkerMove(const QUuid &uuid);
    void startSelectedMarkerMove(const QUuid &uuid);
    void startPipeVertexMove(const QUuid &pipe_uuid, int vertex_index);
    void convertPipeVertexToJunction(const QUuid &pipe_uuid, int vertex_index);
    void showMarkerContextMenu(const QUuid &uuid, const QPoint &global_position);
    void repaintCanvas();
    void updateCanvas();

    MapModel *map_model = nullptr;
    HydraulicData *hydraulic_data = nullptr;
    QPointer<MapCanvasWidget> map_canvas;
    MapEntityPixmapRenderer pixmap_renderer;
    MapCanvasMarkers *point_markers = nullptr;
    MapCanvasDeviceLinks *device_links = nullptr;
    MapCanvasPipes *pipes = nullptr;
    MapCanvasSelection *selection = nullptr;
    MapCanvasPlacement *placement = nullptr;
    QList<MapEntityMarker> move_marker_snapshot;
    QHash<QUuid, QList<CoordinateWGS84>> move_pipe_vertices_snapshot;
    double wrap_reference_longitude = 0.0;
    quint64 visual_state_revision = 0;
    bool synchronizing_geometry = false;

private slots:
    void onNodeChanged(InfrastructureEntity entity_type, const QUuid &uuid);
    void onLinkChanged(InfrastructureEntity entity_type, const QUuid &uuid);
    void onNodeLocateRequested(InfrastructureEntity entity_type, const QUuid &uuid);

public slots:
    void onMarkerSelectedDeleteRequested();
    void onRectangleSelect(const QRect &rect, RectangleSelectMode mode);

signals:
    void signalEntityMarkerSelected(bool status);
    void signalVisualStateChanged(quint64 revision);
};

#endif // MAP_CANVAS_ENTITIES_H
