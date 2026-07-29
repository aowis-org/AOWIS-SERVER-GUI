#ifndef MAP_CANVAS_DEVICELINKS_H
#define MAP_CANVAS_DEVICELINKS_H

#include <optional>
#include <QObject>
#include <QList>
#include <QPainter>
#include <QPointer>
#include <QPoint>
#include <QPointF>

#include "map_entity_marker_label.h"
#include "map_model.h"
#include "map_models.h"

class MapCanvasWidget;

class MapCanvasDeviceLinks : public QObject
{
    Q_OBJECT
    
public:
    enum class AnchorStatus
    {
        NoChange,
        StartSet,
        Completed
    };
    
    struct AnchorResult
    {
        AnchorStatus status = AnchorStatus::NoChange;
        QPointer<MapEntityMarkerLabel> device_label;
    };
    
    explicit MapCanvasDeviceLinks(MapModel *map_model, MapCanvasWidget *map_canvas,
                                  QObject *parent = nullptr);
    
    void clearPlacement();
    bool hasStartLabel() const;
    MapEntityMarkerLabel *startLabel() const;
    AnchorResult anchor(const InfrastructureEntityReference &entity,
                        MapEntityMarkerLabel *connection_target_label,
                        MapEntityMarkerLabel *floating_label,
                        const QList<MapEntityMarker> &markers,
                        const QString &pixmap_path,
                        int label_width);
    std::optional<DeviceLinkGeometry> completionGeometry(
        MapEntityMarkerLabel *connection_target_label,
        const QList<MapEntityMarker> &markers) const;
    
    bool updateMove(MapEntityMarkerLabel *label, const QPointF &screen_position);
    bool setCenterCoordinate(MapEntityMarkerLabel *label,
                             const CoordinateWGS84 &coordinate);
    bool positionFloatingLabel(MapEntityMarkerLabel *floating_label,
                               const QPointF &mouse_position,
                               MapEntityMarkerLabel *connection_target_label,
                               const QList<MapEntityMarker> &markers) const;
    
    void scaleLabels(int width);
    void positionLabels();
    void paint(QPainter &paint,
               const QList<MapEntityMarker> &markers,
               const QList<MapEntityMarker> &selected_markers,
               bool placing_device_link,
               const QPointF &mouse_position,
               MapEntityMarkerLabel *connection_target_label) const;
    
    bool moveCenterByDelta(MapEntityMarkerLabel *label,
                           double longitude_delta,
                           double latitude_delta);
    void removeConnectedToLabel(MapEntityMarkerLabel *label);
    
    QList<MapEntityMarker> markers() const;
    std::optional<MapEntityMarker> markerByLabel(MapEntityMarkerLabel *label) const;
    MapEntityMarkerLabel *labelAt(const QPointF &position,
                                  const QList<MapEntityMarker> &markers) const;
    
signals:
    void markerClicked(MapEntityMarkerLabel *label);
    void markerContextMenuRequested(MapEntityMarkerLabel *label, const QPoint &global_position);
    void markerMoveRequested(MapEntityMarkerLabel *label);
    void markerMoveSelectedRequested(MapEntityMarkerLabel *label);
    void markerDeleteRequested(MapEntityMarkerLabel *label);
    
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
    
    MapEntityMarker pointMarkerByLabel(MapEntityMarkerLabel *label,
                                       const QList<MapEntityMarker> &markers) const;
    bool markerIsSelected(MapEntityMarkerLabel *label,
                          const QList<MapEntityMarker> &selected_markers) const;
    void positionDeviceLabel(MapEntityMarkerLabel *label, const QPointF &center) const;
    void configureLabel(MapEntityMarkerLabel *label);
    void updateCanvas();
    
    MapModel *map_model = nullptr;
    QPointer<MapCanvasWidget> map_canvas;
    QList<DeviceLinkCanvasItem> list_device_links;
    QPointer<MapEntityMarkerLabel> device_link_start_label;
};

#endif // MAP_CANVAS_DEVICELINKS_H
