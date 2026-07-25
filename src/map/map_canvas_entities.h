#ifndef MAP_CANVAS_ENTITIES_H
#define MAP_CANVAS_ENTITIES_H

#include <optional>
#include <QObject>
#include <QPointer>
#include <QPainter>
#include <QLabel>
#include <QPixmap>
#include <QUuid>
#include <QMouseEvent>
#include <QCursor>

#include "map_model.h"
#include "map_entity_marker_label.h"
#include "../_enums_structs.h"
#include "../hydraulic_data.h"
#include "map_models.h"

enum RectangleSelectMode
{
    Add,
    Replace
};

class MapCanvasWidget;
class MapCanvasPipes;
class MapCanvasDeviceLinks;

class MapCanvasEntities : public QObject
{
    Q_OBJECT
    
public:
    explicit MapCanvasEntities(MapModel *map_model, HydraulicData *hydraulic_data, MapCanvasWidget *map_canvas);
    
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
    void startEntityPositioningInternal();
    bool anchorDeviceLink(QMouseEvent *event);
    bool anchorPipe(QMouseEvent *event);
    bool anchorPipeVertexMove(QMouseEvent *event);
    
    void setPointMarkerMouseTransparency(bool transparent);
    void setMoveCursor(bool enabled);
    void moveSelectedEntities(const QPointF &from_position, const QPointF &to_position);
    void setSelectedEntitiesMouseTransparency(bool transparent);
    
    MapModel *map_model = nullptr;
    HydraulicData *hydraulic_data = nullptr;
    QPointer<MapCanvasWidget> map_canvas;
    MapCanvasPipes *pipes = nullptr;
    MapCanvasDeviceLinks *device_links = nullptr;
    
    QList<MapEntityMarker> list_entity_markers;
    QList<MapEntityMarker> list_entity_markers_selected;
    
    MapEntityPlacementMode entity_placement_mode = MapEntityPlacementMode::None;
    MapEntityMarkerLabel *entity_floating = nullptr;
    InfrastructureEntity entity_current;
    std::optional<InfrastructureEntityReference> selected_entity;
    
    int calculateEntityWidth();
    QPoint entity_floating_hide_until;
    bool entity_draw_immediately = true;
    QPointF mouse_pos_last;
    bool move_cursor_active = false;
    bool move_selected_entities = false;
    
    QString pixmapPathForEntity(InfrastructureEntity entity) const;
    void deleteMarker(MapEntityMarkerLabel *label);
    QPointer<MapEntityMarkerLabel> connection_target_label;
    
    void selectPipe(const QUuid &pipe_uuid);
    void startPipeVertexMove(const QUuid &pipe_uuid, int vertex_index);
    void convertPipeVertexToJunction(const QUuid &pipe_uuid, int vertex_index);
    bool isMarkerSelected(MapEntityMarkerLabel *label) const;
    bool hasSelection() const;
    void clearSelection();
    void updateConnectionTarget(const QPointF &mouse_pos);
    void clearConnectionTarget();
    
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
