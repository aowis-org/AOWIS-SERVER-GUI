#include "map_canvas_entities.h"
#include "map_canvas_devicelinks.h"
#include "map_canvas_markers.h"
#include "map_canvas_pipes.h"
#include "map_canvas_placement.h"
#include "map_canvas_selection.h"
#include "map_canvas_widget.h"

#include <QApplication>
#include <QCursor>
#include <QScopedValueRollback>

#include <cmath>
#include <functional>

#include <QtMath>

namespace
{
bool isHydraulicConnectionNode(InfrastructureEntity entity)
{
    return entity == InfrastructureEntity::Junction ||
           entity == InfrastructureEntity::Reservoir ||
           entity == InfrastructureEntity::Tank;
}

bool isHydraulicDeviceLink(InfrastructureEntity entity)
{
    return entity == InfrastructureEntity::Pump || entity == InfrastructureEntity::Valve;
}

bool isHydraulicPipeGeometry(InfrastructureEntity entity)
{
    return entity == InfrastructureEntity::Pipe;
}

bool isHydraulicCanvasLink(InfrastructureEntity entity)
{
    return isHydraulicDeviceLink(entity) || isHydraulicPipeGeometry(entity);
}

CoordinateWGS84 midpoint(const CoordinateWGS84 &from, const CoordinateWGS84 &to)
{
    CoordinateWGS84 coordinate;
    coordinate.latitude_deg = (from.latitude_deg + to.latitude_deg) / 2.0;
    const double longitude_delta = GeoWebMercator::normalizeLongitude(
        to.longitude_deg - from.longitude_deg);
    coordinate.longitude_deg = GeoWebMercator::normalizeLongitude(
        from.longitude_deg + longitude_delta / 2.0);
    return coordinate;
}
}

MapCanvasEntities::MapCanvasEntities(MapModel *map_model, HydraulicData *hydraulic_data,
                                     MapCanvasWidget *map_canvas)
    : QObject(map_canvas), map_model(map_model), hydraulic_data(hydraulic_data),
    map_canvas(map_canvas)
{
    this->point_markers = new MapCanvasMarkers(this->map_model, this->map_canvas, this);
    this->device_links = new MapCanvasDeviceLinks(this->map_model, this->map_canvas, this);
    this->pipes = new MapCanvasPipes(this->map_model, this->map_canvas, this);
    this->selection = new MapCanvasSelection(this->map_model, this->map_canvas,
                                             this->point_markers, this->device_links,
                                             this->pipes, this);
    this->placement = new MapCanvasPlacement(this->map_canvas, this);

    this->setWrapReferenceLongitude(this->map_model->centerLon());
    
    connect(this->point_markers, &MapCanvasMarkers::markerDeleteRequested,
            this, &MapCanvasEntities::onMarkerDeleteRequested);
    connect(this->point_markers, &MapCanvasMarkers::markerMoveRequested,
            this, &MapCanvasEntities::onMarkerMoveRequested);
    connect(this->point_markers, &MapCanvasMarkers::markerMoveSelectedRequested,
            this, &MapCanvasEntities::onMarkerMoveSelectedRequested);
    connect(this->point_markers, &MapCanvasMarkers::markerClicked,
            this, &MapCanvasEntities::onMarkerClicked);
    connect(this->point_markers, &MapCanvasMarkers::markerContextMenuRequested,
            this, &MapCanvasEntities::onMarkerContextMenuRequested);
    
    connect(this->device_links, &MapCanvasDeviceLinks::markerDeleteRequested,
            this, &MapCanvasEntities::onMarkerDeleteRequested);
    connect(this->device_links, &MapCanvasDeviceLinks::markerMoveRequested,
            this, &MapCanvasEntities::onMarkerMoveRequested);
    connect(this->device_links, &MapCanvasDeviceLinks::markerMoveSelectedRequested,
            this, &MapCanvasEntities::onMarkerMoveSelectedRequested);
    connect(this->device_links, &MapCanvasDeviceLinks::markerClicked,
            this, &MapCanvasEntities::onMarkerClicked);
    connect(this->device_links, &MapCanvasDeviceLinks::markerContextMenuRequested,
            this, &MapCanvasEntities::onMarkerContextMenuRequested);
    
    connect(this->pipes, &MapCanvasPipes::pipeSelectionRequested,
            this, &MapCanvasEntities::selectPipe);
    connect(this->pipes, &MapCanvasPipes::pipeVertexAddRequested,
            this, &MapCanvasEntities::addPipeVertex);
    connect(this->pipes, &MapCanvasPipes::pipeVertexDeleteRequested,
            this, &MapCanvasEntities::deletePipeVertex);
    connect(this->pipes, &MapCanvasPipes::pipeVertexMoveRequested,
            this, &MapCanvasEntities::startPipeVertexMove);
    connect(this->pipes, &MapCanvasPipes::pipeVertexConversionRequested,
            this, &MapCanvasEntities::convertPipeVertexToJunction);
    
    connect(this->map_model, &MapModel::zoomChanged, this, &MapCanvasEntities::scaleMarkers);
    connect(this->map_model, &MapModel::centerChangedWGS84, this,
            [this](const CoordinateWGS84 &)
    {
        positionMarkers();
        updateCanvas();
    });

    if (this->hydraulic_data)
    {
        connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, [this]()
        {
            loadNetwork(this->hydraulic_data->networkHydraulic());
        });
        connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this, &MapCanvasEntities::onNodeChanged);
        connect(this->hydraulic_data, &HydraulicData::signalNodeLocateRequested, this, &MapCanvasEntities::onNodeLocateRequested);
        loadNetwork(this->hydraulic_data->networkHydraulic());
    }
}

void MapCanvasEntities::onNodeChanged(InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (!this->hydraulic_data || this->synchronizing_geometry)
        return;

    const std::optional<HydraulicNodeCommonData> node =
        this->hydraulic_data->nodeCommonData(entity_type, uuid);
    if (!node.has_value() || !this->point_markers->setCoordinate(uuid, node->coordinate_wgs84))
        return;

    recalculateWrapReferenceLongitude();
    positionMarkers();
    updateCanvas();
}

void MapCanvasEntities::onNodeLocateRequested(InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (!this->hydraulic_data || !this->map_canvas)
        return;

    const std::optional<HydraulicNodeCommonData> node =
        this->hydraulic_data->nodeCommonData(entity_type, uuid);
    if (!node.has_value())
        return;

    this->map_model->setCenter(node->coordinate_wgs84.longitude_deg,
                               node->coordinate_wgs84.latitude_deg,
                               this->map_canvas->size());
}

void MapCanvasEntities::loadNetwork(const NetworkHydraulic &network)
{
    this->setWrapReferenceLongitude(this->map_model->centerLon());
    this->device_links->clearPlacement();
    this->pipes->clearPlacement();
    this->pipes->cancelPipeVertexMove();
    this->point_markers->setMouseTransparency(false);
    this->placement->stop();
    clearMoveSnapshot();
    this->selection->clear();
    this->device_links->clear();
    this->pipes->clear();
    this->point_markers->clear();

    QHash<QUuid, MapEntityMarker> node_markers;

    const std::function<void(InfrastructureEntity, const QUuid &, const CoordinateWGS84 &)> add_node =
        [this, &node_markers](InfrastructureEntity type, const QUuid &uuid,
                              const CoordinateWGS84 &coordinate)
    {
        if (uuid.isNull() || node_markers.contains(uuid))
        {
            qWarning() << "Cannot load hydraulic node with invalid or duplicate UUID:" << uuid;
            return;
        }

        InfrastructureEntityReference reference;
        reference.type = type;
        reference.uuid = uuid;
        const MapEntityMarker marker = this->point_markers->addMarker(
            reference, coordinate, this->point_markers->pixmapPathForEntity(type),
            this->point_markers->entityWidth());
        node_markers.insert(uuid, marker);
    };

    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
        add_node(InfrastructureEntity::Junction, junction.uuid, junction.coordinate_wgs84);
    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
        add_node(InfrastructureEntity::Reservoir, reservoir.uuid, reservoir.coordinate_wgs84);
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
        add_node(InfrastructureEntity::Tank, tank.uuid, tank.coordinate_wgs84);

    recalculateWrapReferenceLongitude();

    const std::function<std::optional<MapEntityMarker>(const QUuid &)> node_marker =
        [&node_markers](const QUuid &uuid) -> std::optional<MapEntityMarker>
    {
        const QHash<QUuid, MapEntityMarker>::const_iterator iterator = node_markers.constFind(uuid);
        if (iterator == node_markers.constEnd())
            return std::nullopt;
        return iterator.value();
    };

    for (const HydraulicLinkPipe &pipe : network.links_pipes)
    {
        const std::optional<MapEntityMarker> start_marker = node_marker(pipe.node_uuid_from);
        const std::optional<MapEntityMarker> end_marker = node_marker(pipe.node_uuid_to);
        if (!start_marker.has_value() || !end_marker.has_value())
        {
            qWarning() << "Cannot load pipe with missing endpoint node:" << pipe.uuid;
            continue;
        }

        QList<CoordinateWGS84> vertices;
        vertices.reserve(pipe.vertices.size());
        for (const HydraulicLinkVertex &vertex : pipe.vertices)
            vertices.append(vertex.coordinate_wgs84);

        InfrastructureEntityReference reference;
        reference.type = InfrastructureEntity::Pipe;
        reference.uuid = pipe.uuid;
        if (!this->pipes->addPipe(reference, start_marker->entity, end_marker->entity,
                                  start_marker->label, end_marker->label, vertices))
        {
            qWarning() << "Cannot load pipe:" << pipe.uuid;
        }
    }

    const std::function<void(InfrastructureEntity, const QUuid &, const QUuid &,
                             const QUuid &, const QList<HydraulicLinkVertex> &)> load_device_link =
        [this, &node_marker](InfrastructureEntity type, const QUuid &uuid,
                             const QUuid &node_uuid_from, const QUuid &node_uuid_to,
                             const QList<HydraulicLinkVertex> &vertices)
    {
        const std::optional<MapEntityMarker> start_marker = node_marker(node_uuid_from);
        const std::optional<MapEntityMarker> end_marker = node_marker(node_uuid_to);
        if (!start_marker.has_value() || !end_marker.has_value())
        {
            qWarning() << "Cannot load device link with missing endpoint node:" << uuid;
            return;
        }

        DeviceLinkGeometry geometry;
        geometry.start_node = start_marker->entity;
        geometry.end_node = end_marker->entity;
        geometry.center_coordinate = vertices.isEmpty()
                                         ? midpoint(start_marker->coord_wgs84,
                                                    end_marker->coord_wgs84)
                                         : vertices.first().coordinate_wgs84;

        InfrastructureEntityReference reference;
        reference.type = type;
        reference.uuid = uuid;
        if (!this->device_links->addDeviceLink(
                reference, geometry, start_marker->label, end_marker->label,
                this->point_markers->pixmapPathForEntity(type),
                this->point_markers->entityWidth()))
        {
            qWarning() << "Cannot load device link:" << uuid;
        }
    };

    for (const HydraulicLinkPump &pump : network.links_pumps)
    {
        load_device_link(InfrastructureEntity::Pump, pump.uuid,
                         pump.node_uuid_from, pump.node_uuid_to, pump.vertices);
    }
    for (const HydraulicLinkValve &valve : network.links_valves)
    {
        load_device_link(InfrastructureEntity::Valve, valve.uuid,
                         valve.node_uuid_from, valve.node_uuid_to, valve.vertices);
    }

    positionMarkers();
    emit signalEntityMarkerSelected(false);
    updateCanvas();
}

void MapCanvasEntities::startEntityPositioning(InfrastructureEntity entity)
{
    stopEntityPositioning();
    
    if (isHydraulicCanvasLink(entity))
        this->point_markers->setMouseTransparency(true);
    
    const int width = isHydraulicCanvasLink(entity) ? this->point_markers->entityWidth() : 150;
    this->placement->startCreate(entity, this->point_markers->pixmapPathForEntity(entity), width);
}

void MapCanvasEntities::stopEntityPositioning()
{
    if (this->placement->movingSelected())
        this->selection->setMouseTransparency(false);

    restoreMoveSnapshot();
    
    this->device_links->clearPlacement();
    this->pipes->clearPlacement();
    this->pipes->cancelPipeVertexMove();
    this->point_markers->setMouseTransparency(false);
    this->placement->stop();
    clearMoveSnapshot();
    positionMarkers();
    updateCanvas();
}

bool MapCanvasEntities::cancelActiveMove()
{
    if (!this->placement->isMoving())
        return false;

    stopEntityPositioning();
    return true;
}

void MapCanvasEntities::floatEntity(QMouseEvent *event)
{
    if (this->placement->isMoving())
        this->placement->setMoveCursor(true);
    
    this->placement->updateMousePosition(event->position());
    
    if (this->placement->movingSelected())
    {
        this->selection->moveSelected(this->placement->previousMousePosition(),
                                      this->placement->mousePosition());
        if (!synchronizeSelectedGeometry())
        {
            stopEntityPositioning();
            event->accept();
            return;
        }

        positionMarkers();
        updateCanvas();
        event->accept();
        return;
    }
    
    updateConnectionTarget(event->position());
    
    if (this->placement->isMoving() && this->pipes->isPipeVertexMoveActive())
    {
        const std::optional<QUuid> pipe_uuid = this->pipes->activePipeVertexMoveUuid();
        const int vertex_index = this->pipes->activePipeVertexMoveIndex();
        const bool moved = this->pipes->updatePipeVertexMove(event->position());
        const std::optional<CoordinateWGS84> coordinate = pipe_uuid.has_value()
            ? this->pipes->pipeVertexCoordinate(pipe_uuid.value(), vertex_index)
            : std::nullopt;

        if (!moved || !pipe_uuid.has_value() || !coordinate.has_value() ||
            !this->hydraulic_data ||
            !this->hydraulic_data->setPipeVertexCoordinate(
                pipe_uuid.value(), vertex_index, coordinate.value()))
        {
            stopEntityPositioning();
        }

        event->accept();
        return;
    }
    
    MapEntityMarkerLabel *floating_label = this->placement->floatingLabel();
    if (!floating_label || !this->placement->revealFloatingLabelIfReady())
        return;
    
    bool device_link_positioned = false;
    if (this->placement->isMoving())
    {
        device_link_positioned = this->device_links->updateMove(floating_label, event->position());
    }
    else if (this->placement->isCreating() &&
             isHydraulicDeviceLink(this->placement->entity()))
    {
        device_link_positioned = this->device_links->positionFloatingLabel(
            floating_label, event->position(), this->placement->connectionTarget(),
            this->point_markers->markers());
    }
    
    if (!device_link_positioned)
        this->placement->moveFloatingLabelTopLeft(event->position());
    
    if (this->placement->isMoving())
    {
        bool synchronized = false;
        if (device_link_positioned)
        {
            synchronized = synchronizeMarkerCoordinate(floating_label);
        }
        else
        {
            const CoordinateWGS84 coordinate = this->map_model->wgs84FromScreen(
                event->position().toPoint(), this->map_canvas->size());
            synchronized = this->point_markers->setCoordinate(floating_label, coordinate) &&
                           synchronizeMarkerCoordinate(floating_label);
        }

        if (!synchronized)
        {
            stopEntityPositioning();
            event->accept();
            return;
        }
    }

    updateCanvas();
    event->accept();
}

bool MapCanvasEntities::anchorMarker(QMouseEvent *event)
{
    if (anchorPipeVertexMove(event))
        return true;
    
    MapEntityMarkerLabel *floating_label = this->placement->floatingLabel();
    if (!floating_label)
        return false;
    
    if (this->placement->movingSelected())
    {
        this->selection->moveSelected(this->placement->mousePosition(), event->position());
        const bool synchronized = synchronizeSelectedGeometry();
        if (!synchronized)
            restoreMoveSnapshot();
        recalculateWrapReferenceLongitude();
        this->selection->setMouseTransparency(false);
        this->placement->completeMove();
        clearMoveSnapshot();
        positionMarkers();
        updateCanvas();
        return true;
    }
    
    if (this->placement->isCreating())
    {
        if (isHydraulicDeviceLink(this->placement->entity()))
            return anchorDeviceLink(event);
        if (isHydraulicPipeGeometry(this->placement->entity()))
            return anchorPipe(event);
    }
    
    const CoordinateWGS84 coordinate = this->map_model->wgs84FromScreen(
        event->position().toPoint(), this->map_canvas->size());
    
    if (this->placement->isMoving())
    {
        const bool moved = this->point_markers->setCoordinate(floating_label, coordinate) ||
                           this->device_links->setCenterCoordinate(floating_label, coordinate);
        const bool synchronized = moved && synchronizeMarkerCoordinate(floating_label);
        if (!synchronized)
            restoreMoveSnapshot();
        recalculateWrapReferenceLongitude();
        this->placement->completeMove();
        clearMoveSnapshot();
        positionMarkers();
        updateCanvas();
        return synchronized;
    }
    
    if (!this->placement->isCreating())
        return false;
    
    MapEntityMarkerLabel *created_label = this->placement->takeCreatedLabel();
    if (!created_label)
        return false;

    InfrastructureEntityReference reference;
    reference.type = this->placement->entity();
    reference.uuid = createHydraulicNode(reference.type, coordinate);
    if (reference.uuid.isNull())
    {
        created_label->hide();
        created_label->deleteLater();
        this->placement->stop();
        return false;
    }
    
    this->point_markers->addMarker(
        reference, coordinate, this->point_markers->pixmapPathForEntity(reference.type),
        this->point_markers->entityWidth(), created_label);
    recalculateWrapReferenceLongitude();
    this->placement->setFloatingHiddenUntil(event->position().toPoint());
    positionMarkers();
    this->placement->rearmCreate(this->point_markers->pixmapPathForEntity(reference.type), 150);
    updateCanvas();
    return true;
}

QUuid MapCanvasEntities::createHydraulicNode(InfrastructureEntity entity, const CoordinateWGS84 &coordinate)
{
    if (!this->hydraulic_data)
        return QUuid();

    switch (entity)
    {
    case InfrastructureEntity::Junction:
        return this->hydraulic_data->addJunction(coordinate);
    case InfrastructureEntity::Reservoir:
        return this->hydraulic_data->addReservoir(coordinate);
    case InfrastructureEntity::Tank:
        return this->hydraulic_data->addTank(coordinate);
    default:
        return QUuid();
    }
}

bool MapCanvasEntities::anchorDeviceLink(QMouseEvent *event)
{
    const InfrastructureEntity entity = this->placement->entity();
    InfrastructureEntityReference reference;
    reference.type = entity;

    if (this->device_links->hasStartLabel())
    {
        const std::optional<DeviceLinkGeometry> geometry =
            this->device_links->completionGeometry(
                this->placement->connectionTarget(), this->point_markers->markers());
        if (geometry.has_value())
        {
            reference.uuid = createHydraulicDeviceLink(entity, geometry.value());
            if (reference.uuid.isNull())
                return true;
        }
    }

    const MapCanvasDeviceLinks::AnchorResult result = this->device_links->anchor(
        reference, this->placement->connectionTarget(), this->placement->floatingLabel(),
        this->point_markers->markers(), this->point_markers->pixmapPathForEntity(entity),
        this->point_markers->entityWidth());

    if (result.status == MapCanvasDeviceLinks::AnchorStatus::StartSet)
    {
        this->placement->clearConnectionTarget();
        updateConnectionTarget(event->position());
        updateCanvas();
        return true;
    }

    if (result.status != MapCanvasDeviceLinks::AnchorStatus::Completed)
    {
        if (!reference.uuid.isNull())
            deleteHydraulicLink(reference);
        return true;
    }

    this->placement->setFloatingHiddenUntil(event->position().toPoint());
    this->placement->takeCreatedLabel();
    this->placement->clearConnectionTarget();
    this->placement->rearmCreate(this->point_markers->pixmapPathForEntity(entity),
                                 this->point_markers->entityWidth());
    updateCanvas();
    return true;
}

QUuid MapCanvasEntities::createHydraulicDeviceLink(InfrastructureEntity entity, const DeviceLinkGeometry &geometry)
{
    if (!this->hydraulic_data)
        return QUuid();

    switch (entity)
    {
    case InfrastructureEntity::Pump:
        return this->hydraulic_data->addPump(
            geometry.start_node.uuid, geometry.end_node.uuid, geometry.center_coordinate);
    case InfrastructureEntity::Valve:
        return this->hydraulic_data->addValve(
            geometry.start_node.uuid, geometry.end_node.uuid, geometry.center_coordinate);
    default:
        return QUuid();
    }
}

bool MapCanvasEntities::synchronizeMarkerCoordinate(MapEntityMarkerLabel *label)
{
    if (!this->hydraulic_data || !label)
        return false;

    const QScopedValueRollback<bool> synchronization_guard(
        this->synchronizing_geometry, true);
    const MapEntityMarker marker = markerByLabel(label);
    switch (marker.entity.type)
    {
    case InfrastructureEntity::Junction:
    case InfrastructureEntity::Reservoir:
    case InfrastructureEntity::Tank:
        return this->hydraulic_data->setNodeCoordinate(marker.entity.uuid, marker.coord_wgs84);
    case InfrastructureEntity::Pump:
        return this->hydraulic_data->setPumpCenterCoordinate(marker.entity.uuid, marker.coord_wgs84);
    case InfrastructureEntity::Valve:
        return this->hydraulic_data->setValveCenterCoordinate(marker.entity.uuid, marker.coord_wgs84);
    default:
        return false;
    }
}

bool MapCanvasEntities::synchronizeSelectedGeometry()
{
    if (!this->hydraulic_data)
        return false;

    const QScopedValueRollback<bool> synchronization_guard(
        this->synchronizing_geometry, true);
    bool synchronized = true;

    const QList<MapEntityMarkerLabel *> selected_labels = this->selection->selectedLabels();
    for (MapEntityMarkerLabel *label : selected_labels)
        synchronized = synchronizeMarkerCoordinate(label) && synchronized;

    const QList<QUuid> selected_pipe_uuids = this->pipes->selectedPipeUuids();
    for (const QUuid &pipe_uuid : selected_pipe_uuids)
    {
        synchronized = this->hydraulic_data->setPipeVertices(
            pipe_uuid, this->pipes->intermediateVertices(pipe_uuid)) && synchronized;
    }

    return synchronized;
}

void MapCanvasEntities::captureMarkerMoveSnapshot(MapEntityMarkerLabel *label)
{
    clearMoveSnapshot();
    const MapEntityMarker marker = markerByLabel(label);
    if (marker.entity.type != InfrastructureEntity::Unknown)
        this->move_marker_snapshot.append(marker);
}

void MapCanvasEntities::captureSelectedMoveSnapshot()
{
    clearMoveSnapshot();

    const QList<MapEntityMarkerLabel *> selected_labels = this->selection->selectedLabels();
    for (MapEntityMarkerLabel *label : selected_labels)
    {
        const MapEntityMarker marker = markerByLabel(label);
        if (marker.entity.type != InfrastructureEntity::Unknown)
            this->move_marker_snapshot.append(marker);
    }

    const QList<QUuid> selected_pipe_uuids = this->pipes->selectedPipeUuids();
    for (const QUuid &pipe_uuid : selected_pipe_uuids)
    {
        this->move_pipe_vertices_snapshot.insert(
            pipe_uuid, this->pipes->intermediateVertices(pipe_uuid));
    }
}

void MapCanvasEntities::capturePipeMoveSnapshot(const QUuid &pipe_uuid)
{
    clearMoveSnapshot();
    this->move_pipe_vertices_snapshot.insert(
        pipe_uuid, this->pipes->intermediateVertices(pipe_uuid));
}

void MapCanvasEntities::restoreMoveSnapshot()
{
    for (const MapEntityMarker &marker : this->move_marker_snapshot)
    {
        if (!marker.label)
            continue;

        if (isHydraulicConnectionNode(marker.entity.type))
        {
            this->point_markers->setCoordinate(marker.label, marker.coord_wgs84);
            if (this->hydraulic_data)
                this->hydraulic_data->setNodeCoordinate(marker.entity.uuid, marker.coord_wgs84);
        }
        else if (isHydraulicDeviceLink(marker.entity.type))
        {
            this->device_links->setCenterCoordinate(marker.label, marker.coord_wgs84);
            if (this->hydraulic_data && marker.entity.type == InfrastructureEntity::Pump)
                this->hydraulic_data->setPumpCenterCoordinate(marker.entity.uuid, marker.coord_wgs84);
            else if (this->hydraulic_data && marker.entity.type == InfrastructureEntity::Valve)
                this->hydraulic_data->setValveCenterCoordinate(marker.entity.uuid, marker.coord_wgs84);
        }
    }

    QHash<QUuid, QList<CoordinateWGS84>>::const_iterator iterator =
        this->move_pipe_vertices_snapshot.constBegin();
    while (iterator != this->move_pipe_vertices_snapshot.constEnd())
    {
        this->pipes->setIntermediateVertices(iterator.key(), iterator.value());
        if (this->hydraulic_data)
            this->hydraulic_data->setPipeVertices(iterator.key(), iterator.value());
        ++iterator;
    }
}

void MapCanvasEntities::clearMoveSnapshot()
{
    this->move_marker_snapshot.clear();
    this->move_pipe_vertices_snapshot.clear();
}

bool MapCanvasEntities::deleteHydraulicLink(const InfrastructureEntityReference &reference)
{
    if (!this->hydraulic_data || reference.uuid.isNull())
        return false;

    switch (reference.type)
    {
    case InfrastructureEntity::Pipe:
        return this->hydraulic_data->deletePipe(reference.uuid);
    case InfrastructureEntity::Pump:
        return this->hydraulic_data->deletePump(reference.uuid);
    case InfrastructureEntity::Valve:
        return this->hydraulic_data->deleteValve(reference.uuid);
    default:
        return false;
    }
}

bool MapCanvasEntities::deleteHydraulicNode(const InfrastructureEntityReference &reference)
{
    if (!this->hydraulic_data || reference.uuid.isNull())
        return false;

    switch (reference.type)
    {
    case InfrastructureEntity::Junction:
        return this->hydraulic_data->deleteJunction(reference.uuid);
    case InfrastructureEntity::Reservoir:
        return this->hydraulic_data->deleteReservoir(reference.uuid);
    case InfrastructureEntity::Tank:
        return this->hydraulic_data->deleteTank(reference.uuid);
    default:
        return false;
    }
}

void MapCanvasEntities::addPipeVertex(const QUuid &pipe_uuid, int insert_index,
                                      const CoordinateWGS84 &coordinate)
{
    const QList<CoordinateWGS84> previous_vertices = this->pipes->intermediateVertices(pipe_uuid);
    if (!this->pipes->addPipeVertex(pipe_uuid, insert_index, coordinate))
        return;

    if (!this->hydraulic_data ||
        !this->hydraulic_data->setPipeVertices(pipe_uuid, this->pipes->intermediateVertices(pipe_uuid)))
    {
        this->pipes->setIntermediateVertices(pipe_uuid, previous_vertices);
    }
}

void MapCanvasEntities::deletePipeVertex(const QUuid &pipe_uuid, int vertex_index)
{
    const QList<CoordinateWGS84> previous_vertices = this->pipes->intermediateVertices(pipe_uuid);
    if (!this->pipes->deletePipeVertex(pipe_uuid, vertex_index))
        return;

    if (!this->hydraulic_data ||
        !this->hydraulic_data->setPipeVertices(pipe_uuid, this->pipes->intermediateVertices(pipe_uuid)))
    {
        this->pipes->setIntermediateVertices(pipe_uuid, previous_vertices);
    }
}

bool MapCanvasEntities::anchorPipe(QMouseEvent *event)
{
    MapEntityMarkerLabel *connection_target = this->placement->connectionTarget();
    if (!this->pipes->hasStartLabel())
    {
        if (!connection_target)
            return true;
        
        const MapEntityMarker start_marker = markerByLabel(connection_target);
        if (!isHydraulicConnectionNode(start_marker.entity.type))
            return true;
        
        this->pipes->startPipe(connection_target);
        this->placement->clearConnectionTarget();
        return true;
    }
    
    if (connection_target)
    {
        if (connection_target == this->pipes->startLabel())
            return true;
        
        const MapEntityMarker start_marker = markerByLabel(this->pipes->startLabel());
        const MapEntityMarker end_marker = markerByLabel(connection_target);
        if (!isHydraulicConnectionNode(start_marker.entity.type) ||
            !isHydraulicConnectionNode(end_marker.entity.type))
        {
            return true;
        }
        
        InfrastructureEntityReference pipe_reference;
        pipe_reference.type = InfrastructureEntity::Pipe;
        if (this->hydraulic_data)
        {
            pipe_reference.uuid = this->hydraulic_data->addPipe(
                start_marker.entity.uuid, end_marker.entity.uuid,
                this->pipes->intermediateVertices());
        }
        if (pipe_reference.uuid.isNull())
            return true;

        if (!this->pipes->completePipe(
                pipe_reference, start_marker.entity, end_marker.entity, connection_target))
        {
            deleteHydraulicLink(pipe_reference);
            return true;
        }
        
        this->placement->setFloatingHiddenUntil(event->position().toPoint());
        MapEntityMarkerLabel *placement_icon = this->placement->takeCreatedLabel();
        this->placement->clearConnectionTarget();
        if (placement_icon)
        {
            placement_icon->hide();
            placement_icon->deleteLater();
        }
        this->placement->rearmCreate(
            this->point_markers->pixmapPathForEntity(InfrastructureEntity::Pipe),
            this->point_markers->entityWidth());
        return true;
    }
    
    const CoordinateWGS84 intermediate_vertex = this->map_model->wgs84FromScreen(
        event->position().toPoint(), this->map_canvas->size());
    this->pipes->appendIntermediateVertex(intermediate_vertex);
    return true;
}

bool MapCanvasEntities::anchorPipeVertexMove(QMouseEvent *event)
{
    if (!this->placement->isMoving() || !this->pipes->isPipeVertexMoveActive())
        return false;

    const std::optional<QUuid> pipe_uuid = this->pipes->activePipeVertexMoveUuid();
    const int vertex_index = this->pipes->activePipeVertexMoveIndex();
    const bool moved = this->pipes->finishPipeVertexMove(event->position());
    bool synchronized = false;
    if (moved && pipe_uuid.has_value() && this->hydraulic_data)
    {
        const std::optional<CoordinateWGS84> coordinate = this->pipes->pipeVertexCoordinate(
            pipe_uuid.value(), vertex_index);
        synchronized = coordinate.has_value() &&
                       this->hydraulic_data->setPipeVertexCoordinate(
                           pipe_uuid.value(), vertex_index, coordinate.value());
    }

    if (!synchronized)
        restoreMoveSnapshot();

    this->placement->completeMove();
    clearMoveSnapshot();
    return true;
}

void MapCanvasEntities::setWrapReferenceLongitude(double longitude)
{
    const double normalized_longitude = GeoWebMercator::normalizeLongitude(longitude);
    this->point_markers->setWrapReferenceLongitude(normalized_longitude);
    this->device_links->setWrapReferenceLongitude(normalized_longitude);
    this->pipes->setWrapReferenceLongitude(normalized_longitude);
}

void MapCanvasEntities::recalculateWrapReferenceLongitude()
{
    const QList<MapEntityMarker> &markers = this->point_markers->markers();
    if (markers.isEmpty())
    {
        this->setWrapReferenceLongitude(this->map_model->centerLon());
        return;
    }

    double longitude_sin_sum = 0.0;
    double longitude_cos_sum = 0.0;
    for (const MapEntityMarker &marker : markers)
    {
        const double longitude_radians = qDegreesToRadians(
            GeoWebMercator::normalizeLongitude(marker.coord_wgs84.longitude_deg));
        longitude_sin_sum += std::sin(longitude_radians);
        longitude_cos_sum += std::cos(longitude_radians);
    }

    double reference_longitude = markers.first().coord_wgs84.longitude_deg;
    if (std::hypot(longitude_sin_sum, longitude_cos_sum) > 1e-9)
    {
        reference_longitude = qRadiansToDegrees(
            std::atan2(longitude_sin_sum, longitude_cos_sum));
    }

    this->setWrapReferenceLongitude(reference_longitude);
}

void MapCanvasEntities::scaleMarkers()
{
    const int width = this->point_markers->entityWidth();
    this->point_markers->scaleLabels(width);
    this->device_links->scaleLabels(width);
    
    if (this->placement->isCreating() &&
        isHydraulicCanvasLink(this->placement->entity()))
    {
        this->placement->scaleFloatingLabel(
            this->point_markers->pixmapPathForEntity(this->placement->entity()), width);
    }
    
    positionMarkers();
    updateCanvas();
}

void MapCanvasEntities::positionMarkers()
{
    MapEntityMarkerLabel *label_to_skip = nullptr;
    if (this->placement->isMoving() && !this->placement->movingSelected())
        label_to_skip = this->placement->floatingLabel();
    
    this->point_markers->positionLabels(label_to_skip);
    this->device_links->positionLabels();
}

void MapCanvasEntities::paintMarkers(QPainter &paint)
{
    this->pipes->paint(
        paint, this->point_markers->markers(),
        this->placement->isCreating() && isHydraulicPipeGeometry(this->placement->entity()),
        this->placement->mousePosition(), this->placement->connectionTarget());
    this->device_links->paint(
        paint, this->point_markers->markers(), this->selection->selectedMarkers(),
        this->placement->isCreating() && isHydraulicDeviceLink(this->placement->entity()),
        this->placement->mousePosition(), this->placement->connectionTarget());
    
    const bool draw_moving_label_at_mouse = this->placement->isMoving() &&
                                            this->placement->floatingLabel() &&
                                            !this->placement->movingSelected();
    this->point_markers->paintConnectionPoints(
        paint, this->placement->connectionTarget(), this->placement->floatingLabel(),
        draw_moving_label_at_mouse, this->placement->mousePosition());
}

void MapCanvasEntities::onMarkerMoveRequested(MapEntityMarkerLabel *label)
{
    if (!label)
        return;
    
    const MapEntityMarker marker = markerByLabel(label);
    if (marker.entity.type == InfrastructureEntity::Unknown)
        return;
    
    stopEntityPositioning();
    const QPointF mouse_position = this->map_canvas->mapFromGlobal(QCursor::pos());
    captureMarkerMoveSnapshot(label);
    if (!this->placement->startMove(marker.entity.type, label, mouse_position))
        clearMoveSnapshot();
    updateCanvas();
}

void MapCanvasEntities::onMarkerMoveSelectedRequested(MapEntityMarkerLabel *label)
{
    if (!label || !this->selection->isMarkerSelected(label) ||
        this->selection->selectedMarkerCount() < 2)
    {
        return;
    }
    
    onMarkerMoveRequested(label);
    if (!this->placement->isMoving() || this->placement->floatingLabel() != label)
        return;
    
    this->placement->setMovingSelected(true);
    this->pipes->selectPipesWithSelectedEndpoints(this->selection->selectedMarkers());
    captureSelectedMoveSnapshot();
    this->selection->setMouseTransparency(true);
    positionMarkers();
    updateCanvas();
}

void MapCanvasEntities::onMarkerDeleteRequested(MapEntityMarkerLabel *label)
{
    deleteMarker(label);
    updateCanvas();
}

void MapCanvasEntities::onMarkerSelectedDeleteRequested()
{
    const QList<MapEntityMarkerLabel *> labels_to_delete = this->selection->selectedLabels();
    const QList<QUuid> pipe_uuids_to_delete = this->pipes->selectedPipeUuids();
    for (MapEntityMarkerLabel *label : labels_to_delete)
        deleteMarker(label);

    for (const QUuid &pipe_uuid : pipe_uuids_to_delete)
    {
        if (this->hydraulic_data)
            this->hydraulic_data->deletePipe(pipe_uuid);
        this->pipes->removePipe(pipe_uuid);
    }

    this->selection->clear();
    updateCanvas();
    emit signalEntityMarkerSelected(false);
}

void MapCanvasEntities::deleteMarker(MapEntityMarkerLabel *label)
{
    if (!label)
        return;

    const MapEntityMarker marker = markerByLabel(label);
    if (marker.entity.type == InfrastructureEntity::Unknown)
        return;

    if (this->placement->floatingLabel() == label)
        stopEntityPositioning();

    if (isHydraulicConnectionNode(marker.entity.type) && !deleteHydraulicNode(marker.entity))
        return;
    if (isHydraulicDeviceLink(marker.entity.type) && !deleteHydraulicLink(marker.entity))
        return;

    this->selection->clear();

    if (this->placement->connectionTarget() == label)
        this->placement->clearConnectionTarget();
    if (this->pipes->startLabel() == label)
        this->pipes->clearPlacement();
    
    this->device_links->removeConnectedToLabel(label);
    this->pipes->removeConnectedToLabel(label);
    this->selection->removeMarker(label);
    
    const bool point_marker_removed = this->point_markers->removeMarker(label);
    if (!point_marker_removed)
    {
        label->hide();
        label->deleteLater();
    }
    else
    {
        recalculateWrapReferenceLongitude();
    }
}

void MapCanvasEntities::onMarkerClicked(MapEntityMarkerLabel *label)
{
    const MapEntityMarker marker = markerByLabel(label);
    if (marker.entity.type == InfrastructureEntity::Unknown)
        return;
    
    if (QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
    {
        this->selection->toggleMarker(marker);
        emit signalEntityMarkerSelected(this->selection->hasSelection());
        updateCanvas();
        return;
    }
    
    this->selection->replaceWithMarker(marker);
    emit signalEntityMarkerSelected(true);
    
    if (this->hydraulic_data)
        this->hydraulic_data->setSelectedUuid(marker.entity.type, marker.entity.uuid);
    updateCanvas();
}

void MapCanvasEntities::onMarkerContextMenuRequested(MapEntityMarkerLabel *label,
                                                     const QPoint &global_position)
{
    if (!label)
        return;
    
    const bool multiple_entities_selected = this->selection->isMarkerSelected(label) &&
                                            this->selection->selectedMarkerCount() > 1;
    label->showContextMenu(global_position, multiple_entities_selected);
}

void MapCanvasEntities::onRectangleSelect(const QRect &rect,
                                          RectangleSelectMode mode)
{
    this->selection->selectInRectangle(rect, this->point_markers->markers(),
                                       this->device_links->markers(),
                                       mode == RectangleSelectMode::Replace);
    emit signalEntityMarkerSelected(this->selection->hasSelection());
    updateCanvas();
}

void MapCanvasEntities::updateConnectionTarget(const QPointF &mouse_position)
{
    MapEntityMarkerLabel *nearest_label = nullptr;
    if (this->placement->isCreating() &&
        (isHydraulicDeviceLink(this->placement->entity()) ||
         isHydraulicPipeGeometry(this->placement->entity())))
    {
        MapEntityMarkerLabel *excluded_label = nullptr;
        if (isHydraulicDeviceLink(this->placement->entity()))
            excluded_label = this->device_links->startLabel();
        nearest_label = this->point_markers->nearestConnectionTarget(
            mouse_position, excluded_label);
    }
    
    if (this->placement->connectionTarget() == nearest_label)
        return;
    
    this->placement->setConnectionTarget(nearest_label);
    updateCanvas();
}

bool MapCanvasEntities::selectDeviceLinkAt(const QPointF &position)
{
    if (!this->placement->isIdle())
        return false;
    
    MapEntityMarkerLabel *device_label = this->device_links->labelAt(
        position, this->point_markers->markers());
    if (!device_label)
        return false;
    
    onMarkerClicked(device_label);
    return true;
}

bool MapCanvasEntities::isDeviceLinkAt(const QPointF &position)
{
    return this->placement->isIdle() &&
           this->device_links->labelAt(position, this->point_markers->markers()) != nullptr;
}

bool MapCanvasEntities::showPipeContextMenuAt(const QPointF &position,
                                              const QPoint &global_position)
{
    return this->placement->isIdle() &&
           this->pipes->showContextMenuAt(position, global_position,
                                          this->point_markers->markers());
}

void MapCanvasEntities::selectPipe(const QUuid &pipe_uuid)
{
    const std::optional<InfrastructureEntityReference> selected_pipe =
        this->selection->replaceWithPipe(pipe_uuid);
    if (!selected_pipe.has_value())
        return;
    
    emit signalEntityMarkerSelected(true);
    if (this->hydraulic_data)
        this->hydraulic_data->setSelectedUuid(InfrastructureEntity::Pipe, pipe_uuid);
    updateCanvas();
}

void MapCanvasEntities::startPipeVertexMove(const QUuid &pipe_uuid, int vertex_index)
{
    stopEntityPositioning();
    if (!this->pipes->startPipeVertexMove(pipe_uuid, vertex_index))
        return;
    
    capturePipeMoveSnapshot(pipe_uuid);
    selectPipe(pipe_uuid);
    const QPointF mouse_position = this->map_canvas->mapFromGlobal(QCursor::pos());
    this->placement->startVirtualMove(InfrastructureEntity::Pipe, mouse_position);
}

void MapCanvasEntities::convertPipeVertexToJunction(const QUuid &pipe_uuid, int vertex_index)
{
    const std::optional<CoordinateWGS84> vertex_coordinate =
        this->pipes->pipeVertexCoordinate(pipe_uuid, vertex_index);
    if (!vertex_coordinate.has_value())
        return;
    
    InfrastructureEntityReference junction_reference;
    junction_reference.type = InfrastructureEntity::Junction;
    junction_reference.uuid = createHydraulicNode(InfrastructureEntity::Junction, vertex_coordinate.value());
    if (junction_reference.uuid.isNull())
        return;

    const MapEntityMarker junction_marker = this->point_markers->addMarker(
        junction_reference, vertex_coordinate.value(),
        this->point_markers->pixmapPathForEntity(InfrastructureEntity::Junction),
        this->point_markers->entityWidth());
    
    InfrastructureEntityReference second_pipe_reference;
    second_pipe_reference.type = InfrastructureEntity::Pipe;
    second_pipe_reference.uuid = this->hydraulic_data->splitPipeAtVertex(
        pipe_uuid, vertex_index, junction_reference.uuid);
    if (second_pipe_reference.uuid.isNull())
    {
        this->point_markers->removeMarker(junction_marker.label);
        this->hydraulic_data->deleteJunction(junction_reference.uuid);
        return;
    }

    if (!this->pipes->splitPipeAtVertex(
            pipe_uuid, vertex_index, junction_reference,
            second_pipe_reference, junction_marker.label))
    {
        this->hydraulic_data->undoPipeSplit(
            pipe_uuid, second_pipe_reference.uuid, junction_reference.uuid);
        this->point_markers->removeMarker(junction_marker.label);
        this->hydraulic_data->deleteJunction(junction_reference.uuid);
        return;
    }
    
    this->selection->replaceWithMarker(junction_marker);
    emit signalEntityMarkerSelected(true);
    recalculateWrapReferenceLongitude();
    positionMarkers();
    updateCanvas();
}

bool MapCanvasEntities::selectPipeAt(const QPointF &position)
{
    if (!this->placement->isIdle())
        return false;
    
    const std::optional<InfrastructureEntityReference> pipe =
        this->pipes->pipeAt(position, this->point_markers->markers());
    if (!pipe.has_value())
        return false;
    
    selectPipe(pipe->uuid);
    return true;
}

bool MapCanvasEntities::isPipeAt(const QPointF &position)
{
    return this->placement->isIdle() &&
           this->pipes->pipeAt(position, this->point_markers->markers()).has_value();
}

MapEntityMarker MapCanvasEntities::markerByLabel(MapEntityMarkerLabel *label)
{
    const std::optional<MapEntityMarker> point_marker = this->point_markers->markerByLabel(label);
    if (point_marker.has_value())
        return point_marker.value();
    
    const std::optional<MapEntityMarker> device_link_marker =
        this->device_links->markerByLabel(label);
    if (device_link_marker.has_value())
        return device_link_marker.value();
    
    MapEntityMarker marker;
    marker.entity.type = InfrastructureEntity::Unknown;
    return marker;
}

void MapCanvasEntities::updateCanvas()
{
    if (this->map_canvas)
        this->map_canvas->update();
}
