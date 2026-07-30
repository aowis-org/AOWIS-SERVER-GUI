#ifndef MAP_CANVAS_MARKERS_H
#define MAP_CANVAS_MARKERS_H

#include <optional>
#include <QObject>
#include <QList>
#include <QPainter>
#include <QPointer>
#include <QPoint>
#include <QPointF>
#include <QString>

#include "map_entity_marker_label.h"
#include "map_model.h"
#include "map_models.h"

class MapCanvasWidget;

class MapCanvasMarkers : public QObject
{
    Q_OBJECT
    
public:
    explicit MapCanvasMarkers(MapModel *map_model, MapCanvasWidget *map_canvas,
                              QObject *parent = nullptr);

    void setWrapReferenceLongitude(double longitude);
    
    const QList<MapEntityMarker> &markers() const;
    void clear();
    int entityWidth() const;
    QString pixmapPathForEntity(InfrastructureEntity entity) const;
    MapEntityMarkerLabel *nearestConnectionTarget(const QPointF &mouse_position,
                                                  MapEntityMarkerLabel *excluded_label = nullptr,
                                                  double max_distance = 18.0) const;
    std::optional<MapEntityMarker> markerByLabel(MapEntityMarkerLabel *label) const;
    MapEntityMarker addMarker(const InfrastructureEntityReference &entity,
                              const CoordinateWGS84 &coordinate,
                              const QString &pixmap_path,
                              int width,
                              MapEntityMarkerLabel *label = nullptr);
    bool removeMarker(MapEntityMarkerLabel *label);
    bool setCoordinate(MapEntityMarkerLabel *label, const CoordinateWGS84 &coordinate);
    bool moveByDelta(MapEntityMarkerLabel *label,
                     double longitude_delta,
                     double latitude_delta);
    
    void scaleLabels(int width);
    void positionLabels(MapEntityMarkerLabel *label_to_skip = nullptr);
    void setMouseTransparency(bool transparent);
    void paintConnectionPoints(QPainter &paint,
                               MapEntityMarkerLabel *connection_target_label,
                               MapEntityMarkerLabel *moving_label,
                               bool draw_moving_label_at_mouse,
                               const QPointF &mouse_position) const;
    
signals:
    void markerClicked(MapEntityMarkerLabel *label);
    void markerContextMenuRequested(MapEntityMarkerLabel *label,
                                    const QPoint &global_position);
    void markerMoveRequested(MapEntityMarkerLabel *label);
    void markerMoveSelectedRequested(MapEntityMarkerLabel *label);
    void markerDeleteRequested(MapEntityMarkerLabel *label);
    
private:
    QPointF screenFromWgs84(const CoordinateWGS84 &coordinate) const;
    void configureLabel(MapEntityMarkerLabel *label,
                        const QString &pixmap_path,
                        int width);
    
    MapModel *map_model = nullptr;
    QPointer<MapCanvasWidget> map_canvas;
    double wrap_reference_lon = 0.0;
    QList<MapEntityMarker> list_markers;
};

#endif // MAP_CANVAS_MARKERS_H
