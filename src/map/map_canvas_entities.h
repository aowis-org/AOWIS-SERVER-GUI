#ifndef MAP_CANVAS_ENTITIES_H
#define MAP_CANVAS_ENTITIES_H

#include <QObject>
#include <QPointer>
#include <QPainter>
#include <QLabel>
#include <QPixmap>
#include <QUuid>
#include <QMouseEvent>
#include <QCursor>

#include <functional>

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

// to avoid circular includes
class MapCanvasWidget;

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
    
    MapEntityMarker markerByLabel(MapEntityMarkerLabel *label);
    
private:
    struct DeviceLinkCanvasItem
    {
        InfrastructureEntityReference entity;
        DeviceLinkGeometry geometry;
        QPointer<MapEntityMarkerLabel> start_label;
        QPointer<MapEntityMarkerLabel> end_label;
        QPointer<MapEntityMarkerLabel> device_label;
        QString path_pixmap;
    };
    
    void startEntityPositioningInternal();
    bool anchorDeviceLink(QMouseEvent *event);
    void paintDeviceLinks(QPainter &paint);
    void positionDeviceLinks();
    void positionDeviceLabel(MapEntityMarkerLabel *label, const QPointF &center);
    void setPointMarkerMouseTransparency(bool transparent);
    
    MapModel *map_model = nullptr;
    HydraulicData *hydraulic_data = nullptr;
    //NetworkHydraulic network_hydraulic;
    // QPointer to avoid circular includes
    QPointer<MapCanvasWidget> map_canvas;
    
    QList<MapEntityMarker> list_entity_markers;
    QList<MapEntityMarker> list_entity_markers_selected;
    QList<DeviceLinkCanvasItem> list_device_links;
    
    MapEntityPlacementMode entity_placement_mode = MapEntityPlacementMode::None;
    MapEntityMarkerLabel *entity_floating = nullptr;
    
    InfrastructureEntity entity_current;
    std::optional<InfrastructureEntityReference> selected_entity;
    
    int calculateEntityWidth();
    
    QPoint entity_floating_hide_until;
    // on tool change, the rearming should not be active
    bool entity_draw_immediately = true;
    QPointF mouse_pos_last;
    
    QString pixmapPathForEntity(InfrastructureEntity entity) const;
    void deleteMarker(MapEntityMarkerLabel *label);
    
    QPointer<MapEntityMarkerLabel> connection_target_label;
    QPointer<MapEntityMarkerLabel> device_link_start_label;
    
    MapEntityMarkerLabel *deviceLinkLabelAt(const QPointF &position);
    
    bool isMarkerSelected(MapEntityMarkerLabel *label) const;
    
    void updateConnectionTarget(const QPointF &mouse_pos);
    void clearConnectionTarget();
    
private slots:
    void onMarkerClicked(MapEntityMarkerLabel *label);
    void onMarkerMoveRequested(MapEntityMarkerLabel *label);
    void onMarkerDeleteRequested(MapEntityMarkerLabel *label);
    
public slots:
    void onMarkerSelectedDeleteRequested();
    
    void onRectangleSelect(const CoordinateWGS84Rect &rect, RectangleSelectMode mode);
    
signals:
    void signalEntityMarkerSelected(bool status);
};

#endif // MAP_CANVAS_ENTITIES_H
