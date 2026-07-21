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

#include "map_model.h"
#include "map_entity_marker_label.h"

#include "../_enums_structs.h"
#include "../hydraulic_data.h"
#include "map_models.h"

// to avoid circular includes
class MapCanvasWidget;

class MapCanvasEntities : public QObject
{
    Q_OBJECT
public:
    explicit MapCanvasEntities(MapModel *map_model, HydraulicData *hydraulic_data, MapCanvasWidget *map_canvas);
    
    void startEntityPositioning(InfrastructureEntity entity);
    void startEntityPositioningInternal();
    void stopEntityPositioning();
    void floatEntity(QMouseEvent *event);
    bool anchorMarker(QMouseEvent *event);
    void scaleMarkers();
    //void updateMarkersTank(QPainter &paint);
    void positionMarkers();
    void paintMarkers(QPainter &paint);
    
    MapEntityMarker markerByLabel(MapEntityMarkerLabel *label);
    
private:
    MapModel *map_model = nullptr;
    HydraulicData *hydraulic_data = nullptr;
    //NetworkHydraulic network_hydraulic;
    // QPointer to avoid circular includes
    QPointer<MapCanvasWidget> map_canvas;
    
    QList<MapEntityMarker> list_entity_markers;
    
    MapEntityPlacementMode entity_placement_mode = MapEntityPlacementMode::None;
    MapEntityMarkerLabel *entity_floating = nullptr;
    
    InfrastructureEntity entity_current;
    std::optional<InfrastructureEntityReference> selected_entity;
    
    int calculateEntityWidth();
    
    QPoint entity_floating_hide_until;
    // on tool change, the rearming should not be active
    bool entity_draw_immediately = true;
    QPointF mouse_pos_last;
    
    QString pixmapPathForSymbol(const QString &symbol_id) const;
    
private slots:
    void onMarkerClicked(MapEntityMarkerLabel *label);
    void onMarkerDeleteRequested(MapEntityMarkerLabel *label);
    void onMarkerMoveRequested(MapEntityMarkerLabel *label);
    
signals:
};

#endif // MAP_CANVAS_ENTITIES_H
