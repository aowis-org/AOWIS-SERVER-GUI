#ifndef MAP_CANVAS_ENTITIES_H
#define MAP_CANVAS_ENTITIES_H

#include <QObject>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QPainter>
#include <QPointer>
#include <QUuid>

#include "../_enums_structs.h"
#include "../hydraulic_data.h"
#include "map_models.h"

class MapCanvasDeviceLinks;
class MapCanvasMarkers;
class MapCanvasPipes;
class MapCanvasPlacement;
class MapCanvasSelection;
class MapCanvasWidget;
class MapEntityMarkerLabel;
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
    
    void startEntityPositioning(InfrastructureEntity entity);
    void stopEntityPositioning();
    void floatEntity(QMouseEvent *event);
    bool anchorMarker(QMouseEvent *event);
    
    void scaleMarkers();
    void positionMarkers();
    void paintMarkers(QPainter &paint);
    bool selectDeviceLinkAt(const QPointF &position);
    bool isDeviceLinkAt(const QPointF &position);
    bool selectPipeAt(const QPointF &position);
    bool isPipeAt(const QPointF &position);
    bool showPipeContextMenuAt(const QPointF &position, const QPoint &global_position);
    
    MapEntityMarker markerByLabel(MapEntityMarkerLabel *label);
    
private:
    bool anchorDeviceLink(QMouseEvent *event);
    bool anchorPipe(QMouseEvent *event);
    bool anchorPipeVertexMove(QMouseEvent *event);
    QUuid createHydraulicNode(InfrastructureEntity entity, const CoordinateWGS84 &coordinate);
    QUuid createHydraulicDeviceLink(InfrastructureEntity entity, const DeviceLinkGeometry &geometry);
    bool deleteHydraulicLink(const InfrastructureEntityReference &reference);
    bool synchronizeMarkerCoordinate(MapEntityMarkerLabel *label);
    void synchronizeSelectedGeometry();
    void updateConnectionTarget(const QPointF &mouse_position);
    void deleteMarker(MapEntityMarkerLabel *label);
    void selectPipe(const QUuid &pipe_uuid);
    void startPipeVertexMove(const QUuid &pipe_uuid, int vertex_index);
    void convertPipeVertexToJunction(const QUuid &pipe_uuid, int vertex_index);
    void updateCanvas();
    
    MapModel *map_model = nullptr;
    HydraulicData *hydraulic_data = nullptr;
    QPointer<MapCanvasWidget> map_canvas;
    MapCanvasMarkers *point_markers = nullptr;
    MapCanvasDeviceLinks *device_links = nullptr;
    MapCanvasPipes *pipes = nullptr;
    MapCanvasSelection *selection = nullptr;
    MapCanvasPlacement *placement = nullptr;
    
private slots:
    void onMarkerClicked(MapEntityMarkerLabel *label);
    void onMarkerContextMenuRequested(MapEntityMarkerLabel *label, const QPoint &global_position);
    void onMarkerMoveRequested(MapEntityMarkerLabel *label);
    void onMarkerMoveSelectedRequested(MapEntityMarkerLabel *label);
    void onMarkerDeleteRequested(MapEntityMarkerLabel *label);
    
public slots:
    void onMarkerSelectedDeleteRequested();
    void onRectangleSelect(const CoordinateWGS84Rect &rect, RectangleSelectMode mode);
    
signals:
    void signalEntityMarkerSelected(bool status);
};

#endif // MAP_CANVAS_ENTITIES_H
