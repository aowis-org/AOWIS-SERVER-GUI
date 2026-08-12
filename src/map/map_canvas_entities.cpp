#include "map_canvas_entities.h"
#include "map_canvas_devicelinks.h"
#include "map_canvas_markers.h"
#include "map_canvas_pipes.h"
#include "map_canvas_placement.h"
#include "map_canvas_selection.h"
#include "map_canvas_widget.h"

#include "../geo_web_mercator.h"
#include "../infrastructure_entity_traits.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QMenu>
#include <QMessageBox>
#include <QScopedValueRollback>

#include <cmath>
#include <functional>

#include <QtMath>

namespace
{
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
    this->point_markers = new MapCanvasMarkers(
        this->map_model, this->map_canvas, &this->pixmap_renderer, this);
    this->device_links = new MapCanvasDeviceLinks(
        this->map_model, this->map_canvas, &this->pixmap_renderer, this);
    this->pipes = new MapCanvasPipes(this->map_model, this->map_canvas, this);
    this->selection = new MapCanvasSelection(
        this->map_model, this->map_canvas, this->point_markers,
        this->device_links, this->pipes, this);
    this->placement = new MapCanvasPlacement(this->map_canvas, this);

    setWrapReferenceLongitude(this->map_model->centerLon());

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
    connect(this->pipes, &MapCanvasPipes::signalCanvasUpdateRequested,
            this, &MapCanvasEntities::repaintCanvas);
    connect(this->device_links, &MapCanvasDeviceLinks::signalCanvasUpdateRequested,
            this, &MapCanvasEntities::repaintCanvas);

    connect(this->map_model, &MapModel::zoomChanged,
            this, &MapCanvasEntities::scaleMarkers);
    connect(this->map_model, &MapModel::centerChangedWGS84, this,
            [this](const CoordinateWGS84 &)
    {
        positionMarkers();
    });

    if (this->hydraulic_data)
    {
        connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, [this]()
        {
            loadNetwork(this->hydraulic_data->networkHydraulic());
        });
        connect(this->hydraulic_data, &HydraulicData::signalNodeChanged,
                this, &MapCanvasEntities::onNodeChanged);
        connect(this->hydraulic_data, &HydraulicData::signalLinkChanged,
                this, &MapCanvasEntities::onLinkChanged);
        connect(this->hydraulic_data, &HydraulicData::signalSimulationResultTimelineChanged,
                this, [this](bool)
        {
            updateCanvas();
        });
        connect(this->hydraulic_data, &HydraulicData::signalNodeLocateRequested,
                this, &MapCanvasEntities::onNodeLocateRequested);
        connect(this->hydraulic_data, &HydraulicData::signalSelectedTank, this,
            [this](const HydraulicNodeTank &tank)
        {
            applyExternalSelection(InfrastructureEntity::Tank, tank.uuid);
        });
        connect(this->hydraulic_data, &HydraulicData::signalSelectedReservoir, this,
            [this](const HydraulicNodeReservoir &reservoir)
        {
            applyExternalSelection(InfrastructureEntity::Reservoir, reservoir.uuid);
        });
        connect(this->hydraulic_data, &HydraulicData::signalSelectedJunction, this,
            [this](const HydraulicNodeJunction &junction)
        {
            applyExternalSelection(InfrastructureEntity::Junction, junction.uuid);
        });
        connect(this->hydraulic_data, &HydraulicData::signalSelectedPipe, this,
            [this](const HydraulicLinkPipe &pipe)
        {
            applyExternalSelection(InfrastructureEntity::Pipe, pipe.uuid);
        });
        connect(this->hydraulic_data, &HydraulicData::signalSelectedPump, this,
            [this](const HydraulicLinkPump &pump)
        {
            applyExternalSelection(InfrastructureEntity::Pump, pump.uuid);
        });
        connect(this->hydraulic_data, &HydraulicData::signalSelectedValve, this,
            [this](const HydraulicLinkValve &valve)
        {
            applyExternalSelection(InfrastructureEntity::Valve, valve.uuid);
        });
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
    updateCanvas();
}

void MapCanvasEntities::onLinkChanged(InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (!this->hydraulic_data || this->synchronizing_geometry)
        return;

    if (entity_type == InfrastructureEntity::Pipe)
    {
        const std::optional<HydraulicLinkPipe> pipe = this->hydraulic_data->pipe(uuid);
        if (!pipe.has_value())
            return;

        QList<CoordinateWGS84> vertices;
        vertices.reserve(pipe->vertices.size());
        for (const HydraulicLinkVertex &vertex : pipe->vertices)
            vertices.append(vertex.coordinate_wgs84);
        this->pipes->setIntermediateVertices(uuid, vertices);
        return;
    }

    QUuid start_node_uuid;
    QUuid end_node_uuid;
    QList<HydraulicLinkVertex> vertices;
    if (entity_type == InfrastructureEntity::Pump)
    {
        const std::optional<HydraulicLinkPump> pump = this->hydraulic_data->pump(uuid);
        if (!pump.has_value())
            return;
        start_node_uuid = pump->node_uuid_from;
        end_node_uuid = pump->node_uuid_to;
        vertices = pump->vertices;
    }
    else if (entity_type == InfrastructureEntity::Valve)
    {
        const std::optional<HydraulicLinkValve> valve = this->hydraulic_data->valve(uuid);
        if (!valve.has_value())
            return;
        start_node_uuid = valve->node_uuid_from;
        end_node_uuid = valve->node_uuid_to;
        vertices = valve->vertices;
    }
    else
    {
        return;
    }

    CoordinateWGS84 center_coordinate;
    if (!vertices.isEmpty())
    {
        center_coordinate = vertices.first().coordinate_wgs84;
    }
    else
    {
        const std::optional<MapEntityMarker> start_marker = markerByUuid(start_node_uuid);
        const std::optional<MapEntityMarker> end_marker = markerByUuid(end_node_uuid);
        if (!start_marker.has_value() || !end_marker.has_value())
            return;
        center_coordinate = midpoint(start_marker->coord_wgs84, end_marker->coord_wgs84);
    }

    this->device_links->setCenterCoordinate(uuid, center_coordinate);
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
    setWrapReferenceLongitude(this->map_model->centerLon());
    this->device_links->clearPlacement();
    this->pipes->clearPlacement();
    this->pipes->cancelPipeVertexMove();
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
        if (!this->pipes->addPipe(reference, start_marker->entity,
                                  end_marker->entity, vertices))
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
            ? midpoint(start_marker->coord_wgs84, end_marker->coord_wgs84)
            : vertices.first().coordinate_wgs84;

        InfrastructureEntityReference reference;
        reference.type = type;
        reference.uuid = uuid;
        if (!this->device_links->addDeviceLink(
                reference, geometry, this->point_markers->pixmapPathForEntity(type),
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

    emit signalEntityMarkerSelected(false);
    updateCanvas();
}

void MapCanvasEntities::startEntityPositioning(InfrastructureEntity entity)
{
    stopEntityPositioning();
    const int width = InfrastructureEntityTraits::isHydraulicNetworkLink(entity)
        ? this->point_markers->entityWidth()
        : qMax(1, qRound(150.0 * this->point_markers->iconSizePercent() / 100.0));
    this->placement->startCreate(
        entity, this->point_markers->pixmapPathForEntity(entity), width);
}

void MapCanvasEntities::stopEntityPositioning()
{
    restoreMoveSnapshot();
    this->device_links->clearPlacement();
    this->pipes->clearPlacement();
    this->pipes->cancelPipeVertexMove();
    this->placement->stop();
    clearMoveSnapshot();
    updateCanvas();
}

bool MapCanvasEntities::cancelActiveMove()
{
    if (!this->placement->isMoving())
        return false;
    stopEntityPositioning();
    return true;
}

bool MapCanvasEntities::positioningActive() const
{
    return !this->placement->isIdle();
}

bool MapCanvasEntities::floatEntity(const QPointF &position)
{
    if (this->placement->isMoving())
        this->placement->setMoveCursor(true);

    const CoordinateWGS84 mouse_coordinate = this->map_model->wgs84FromScreen(
        position.toPoint(), this->map_canvas->size());
    this->placement->updateMousePosition(position, mouse_coordinate);

    if (this->placement->movingSelected())
    {
        this->selection->moveSelected(this->placement->previousMouseCoordinate(),
                                      this->placement->mouseCoordinate(),
                                      this->move_translated_pipe_uuids);
        updateCanvas();
        return true;
    }

    updateConnectionTarget(position);

    if (this->placement->isMoving() && this->pipes->isPipeVertexMoveActive())
    {
        if (!this->pipes->updatePipeVertexMove(position))
            stopEntityPositioning();
        else
            updateCanvas();
        return true;
    }

    if (!this->placement->hasFloatingMarker() ||
        !this->placement->revealFloatingMarkerIfReady())
    {
        return false;
    }

    if (this->placement->isMoving())
    {
        const QUuid moving_uuid = this->placement->floatingUuid();
        bool moved = this->device_links->updateMove(moving_uuid, position);
        if (!moved)
        {
            const CoordinateWGS84 coordinate = this->map_model->wgs84FromScreen(
                position.toPoint(), this->map_canvas->size());
            moved = this->point_markers->setCoordinate(moving_uuid, coordinate);
        }

        if (!moved)
        {
            stopEntityPositioning();
            return true;
        }
    }

    updateCanvas();
    return true;
}

bool MapCanvasEntities::anchorMarker(const QPointF &position)
{
    if (anchorPipeVertexMove(position))
        return true;

    if (!this->placement->hasFloatingMarker())
        return false;

    if (this->placement->movingSelected())
    {
        const CoordinateWGS84 coordinate = this->map_model->wgs84FromScreen(
            position.toPoint(), this->map_canvas->size());
        const CoordinateWGS84 from_coordinate = this->placement->mouseCoordinateValid()
            ? this->placement->mouseCoordinate() : coordinate;
        this->selection->moveSelected(from_coordinate, coordinate,
                                      this->move_translated_pipe_uuids);
        const bool synchronized = synchronizeSelectedGeometry();
        if (!synchronized)
            restoreMoveSnapshot();
        recalculateWrapReferenceLongitude();
        this->placement->completeMove();
        clearMoveSnapshot();
        updateCanvas();
        return true;
    }

    if (this->placement->isCreating())
    {
        if (InfrastructureEntityTraits::isHydraulicDeviceLink(this->placement->entity()))
            return anchorDeviceLink(position);
        if (this->placement->entity() == InfrastructureEntity::Pipe)
            return anchorPipe(position);
    }

    const CoordinateWGS84 coordinate = this->map_model->wgs84FromScreen(
        position.toPoint(), this->map_canvas->size());

    if (this->placement->isMoving())
    {
        const QUuid moving_uuid = this->placement->floatingUuid();
        const bool moved = this->point_markers->setCoordinate(moving_uuid, coordinate) ||
                           this->device_links->setCenterCoordinate(moving_uuid, coordinate);
        const bool synchronized = moved && synchronizeMarkerCoordinate(moving_uuid);
        if (!synchronized)
            restoreMoveSnapshot();
        recalculateWrapReferenceLongitude();
        this->placement->completeMove();
        clearMoveSnapshot();
        updateCanvas();
        return synchronized;
    }

    if (!this->placement->isCreating())
        return false;

    InfrastructureEntityReference reference;
    reference.type = this->placement->entity();
    reference.uuid = createHydraulicNode(reference.type, coordinate);
    if (reference.uuid.isNull())
    {
        this->placement->stop();
        return false;
    }

    this->point_markers->addMarker(
        reference, coordinate, this->point_markers->pixmapPathForEntity(reference.type),
        this->point_markers->entityWidth());
    recalculateWrapReferenceLongitude();
    this->placement->setFloatingHiddenUntil(position.toPoint());
    this->placement->consumeCreatedMarker();
    this->placement->rearmCreate(
        this->point_markers->pixmapPathForEntity(reference.type),
        qMax(1, qRound(150.0 * this->point_markers->iconSizePercent() / 100.0)));
    updateCanvas();
    return true;
}

QUuid MapCanvasEntities::createHydraulicNode(InfrastructureEntity entity,
                                             const CoordinateWGS84 &coordinate)
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

bool MapCanvasEntities::anchorDeviceLink(const QPointF &position)
{
    const InfrastructureEntity entity = this->placement->entity();
    InfrastructureEntityReference reference;
    reference.type = entity;

    if (this->device_links->hasStartNode())
    {
        const std::optional<DeviceLinkGeometry> geometry =
            this->device_links->completionGeometry(
                this->placement->connectionTargetUuid(), this->point_markers->markers());
        if (geometry.has_value())
        {
            reference.uuid = createHydraulicDeviceLink(entity, geometry.value());
            if (reference.uuid.isNull())
                return true;
        }
    }

    const MapCanvasDeviceLinks::AnchorResult result = this->device_links->anchor(
        reference, this->placement->connectionTargetUuid(), this->point_markers->markers(),
        this->point_markers->pixmapPathForEntity(entity),
        this->point_markers->entityWidth());

    if (result.status == MapCanvasDeviceLinks::AnchorStatus::StartSet)
    {
        this->placement->clearConnectionTarget();
        updateConnectionTarget(position);
        updateCanvas();
        return true;
    }

    if (result.status != MapCanvasDeviceLinks::AnchorStatus::Completed)
    {
        if (!reference.uuid.isNull())
            deleteHydraulicLink(reference);
        return true;
    }

    this->placement->setFloatingHiddenUntil(position.toPoint());
    this->placement->consumeCreatedMarker();
    this->placement->clearConnectionTarget();
    this->placement->rearmCreate(
        this->point_markers->pixmapPathForEntity(entity),
        this->point_markers->entityWidth());
    updateCanvas();
    return true;
}

QUuid MapCanvasEntities::createHydraulicDeviceLink(
    InfrastructureEntity entity, const DeviceLinkGeometry &geometry)
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

bool MapCanvasEntities::synchronizeMarkerCoordinate(const QUuid &uuid)
{
    if (!this->hydraulic_data || uuid.isNull())
        return false;

    const std::optional<MapEntityMarker> marker = markerByUuid(uuid);
    if (!marker.has_value())
        return false;

    HydraulicGeometryBatch batch;
    switch (marker->entity.type)
    {
    case InfrastructureEntity::Junction:
    case InfrastructureEntity::Reservoir:
    case InfrastructureEntity::Tank:
        batch.node_coordinates.insert(marker->entity.uuid, marker->coord_wgs84);
        break;
    case InfrastructureEntity::Pump:
        batch.pump_center_coordinates.insert(marker->entity.uuid, marker->coord_wgs84);
        break;
    case InfrastructureEntity::Valve:
        batch.valve_center_coordinates.insert(marker->entity.uuid, marker->coord_wgs84);
        break;
    default:
        return false;
    }

    const QScopedValueRollback<bool> synchronization_guard(
        this->synchronizing_geometry, true);
    return this->hydraulic_data->applyGeometryBatch(batch);
}

bool MapCanvasEntities::synchronizeSelectedGeometry()
{
    if (!this->hydraulic_data)
        return false;

    HydraulicGeometryBatch batch;
    for (const MapEntityMarker &original_marker : this->move_marker_snapshot)
    {
        std::optional<MapEntityMarker> marker = this->point_markers->markerByUuid(
            original_marker.entity.uuid);
        if (!marker.has_value())
            marker = this->device_links->markerByUuid(original_marker.entity.uuid);
        if (!marker.has_value())
            return false;

        if (InfrastructureEntityTraits::isHydraulicConnectionNode(marker->entity.type))
            batch.node_coordinates.insert(marker->entity.uuid, marker->coord_wgs84);
        else if (marker->entity.type == InfrastructureEntity::Pump)
            batch.pump_center_coordinates.insert(marker->entity.uuid, marker->coord_wgs84);
        else if (marker->entity.type == InfrastructureEntity::Valve)
            batch.valve_center_coordinates.insert(marker->entity.uuid, marker->coord_wgs84);
    }

    for (const QUuid &pipe_uuid : this->move_translated_pipe_uuids)
    {
        batch.pipe_vertices.insert(
            pipe_uuid, this->pipes->intermediateVertices(pipe_uuid));
    }

    const QScopedValueRollback<bool> synchronization_guard(
        this->synchronizing_geometry, true);
    return this->hydraulic_data->applyGeometryBatch(batch);
}

void MapCanvasEntities::captureMarkerMoveSnapshot(const QUuid &uuid)
{
    clearMoveSnapshot();
    const std::optional<MapEntityMarker> marker = markerByUuid(uuid);
    if (marker.has_value())
        this->move_marker_snapshot.append(marker.value());
}

void MapCanvasEntities::captureSelectedMoveSnapshot()
{
    clearMoveSnapshot();
    for (const QUuid &uuid : this->selection->selectedMarkerUuids())
    {
        const std::optional<MapEntityMarker> marker = markerByUuid(uuid);
        if (marker.has_value())
            this->move_marker_snapshot.append(marker.value());
    }

    for (const QUuid &pipe_uuid : this->pipes->selectedPipeUuids())
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

void MapCanvasEntities::prepareMoveVisualState()
{
    this->move_dynamic_pipe_uuids.clear();
    this->move_dynamic_device_link_uuids.clear();
    this->move_translated_pipe_uuids.clear();

    QSet<QUuid> moved_node_uuids;
    for (const MapEntityMarker &marker : this->move_marker_snapshot)
    {
        if (InfrastructureEntityTraits::isHydraulicConnectionNode(marker.entity.type))
            moved_node_uuids.insert(marker.entity.uuid);
        else if (InfrastructureEntityTraits::isHydraulicDeviceLink(marker.entity.type))
            this->move_dynamic_device_link_uuids.append(marker.entity.uuid);
    }

    if (!moved_node_uuids.isEmpty())
    {
        this->move_dynamic_pipe_uuids = this->pipes->connectedPipeUuids(moved_node_uuids);
        const QList<QUuid> connected_device_links =
            this->device_links->connectedLinkUuids(moved_node_uuids);
        for (const QUuid &uuid : connected_device_links)
        {
            if (!this->move_dynamic_device_link_uuids.contains(uuid))
                this->move_dynamic_device_link_uuids.append(uuid);
        }
    }

    if (this->pipes->isPipeVertexMoveActive())
    {
        const std::optional<QUuid> pipe_uuid = this->pipes->activePipeVertexMoveUuid();
        if (pipe_uuid.has_value() && !this->move_dynamic_pipe_uuids.contains(pipe_uuid.value()))
            this->move_dynamic_pipe_uuids.append(pipe_uuid.value());
    }

    if (this->placement->movingSelected())
        this->move_translated_pipe_uuids = this->pipes->selectedPipeUuids();

    ++this->move_session_id;
}

void MapCanvasEntities::restoreMoveSnapshot()
{
    for (const MapEntityMarker &marker : this->move_marker_snapshot)
    {
        if (InfrastructureEntityTraits::isHydraulicConnectionNode(marker.entity.type))
            this->point_markers->setCoordinate(marker.entity.uuid, marker.coord_wgs84);
        else if (InfrastructureEntityTraits::isHydraulicDeviceLink(marker.entity.type))
            this->device_links->setCenterCoordinate(marker.entity.uuid, marker.coord_wgs84);
    }

    auto iterator = this->move_pipe_vertices_snapshot.constBegin();
    while (iterator != this->move_pipe_vertices_snapshot.constEnd())
    {
        this->pipes->setIntermediateVertices(iterator.key(), iterator.value());
        ++iterator;
    }
}

void MapCanvasEntities::clearMoveSnapshot()
{
    this->move_marker_snapshot.clear();
    this->move_pipe_vertices_snapshot.clear();
    this->move_dynamic_pipe_uuids.clear();
    this->move_dynamic_device_link_uuids.clear();
    this->move_translated_pipe_uuids.clear();
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
    const QList<CoordinateWGS84> previous_vertices =
        this->pipes->intermediateVertices(pipe_uuid);
    if (!this->pipes->addPipeVertex(pipe_uuid, insert_index, coordinate))
        return;

    if (!this->hydraulic_data ||
        !this->hydraulic_data->setPipeVertices(
            pipe_uuid, this->pipes->intermediateVertices(pipe_uuid)))
    {
        this->pipes->setIntermediateVertices(pipe_uuid, previous_vertices);
    }
}

void MapCanvasEntities::deletePipeVertex(const QUuid &pipe_uuid, int vertex_index)
{
    const QList<CoordinateWGS84> previous_vertices =
        this->pipes->intermediateVertices(pipe_uuid);
    if (!this->pipes->deletePipeVertex(pipe_uuid, vertex_index))
        return;

    if (!this->hydraulic_data ||
        !this->hydraulic_data->setPipeVertices(
            pipe_uuid, this->pipes->intermediateVertices(pipe_uuid)))
    {
        this->pipes->setIntermediateVertices(pipe_uuid, previous_vertices);
    }
}

bool MapCanvasEntities::anchorPipe(const QPointF &position)
{
    const QUuid connection_target_uuid = this->placement->connectionTargetUuid();
    if (!this->pipes->hasStartNode())
    {
        if (connection_target_uuid.isNull())
            return true;

        const std::optional<MapEntityMarker> start_marker = markerByUuid(connection_target_uuid);
        if (!start_marker.has_value() ||
            !InfrastructureEntityTraits::isHydraulicConnectionNode(start_marker->entity.type))
            return true;

        this->pipes->startPipe(connection_target_uuid);
        this->placement->clearConnectionTarget();
        updateCanvas();
        return true;
    }

    if (!connection_target_uuid.isNull())
    {
        if (connection_target_uuid == this->pipes->startNodeUuid())
            return true;

        const std::optional<MapEntityMarker> start_marker = markerByUuid(
            this->pipes->startNodeUuid());
        const std::optional<MapEntityMarker> end_marker = markerByUuid(connection_target_uuid);
        if (!start_marker.has_value() || !end_marker.has_value() ||
            !InfrastructureEntityTraits::isHydraulicConnectionNode(start_marker->entity.type) ||
            !InfrastructureEntityTraits::isHydraulicConnectionNode(end_marker->entity.type))
        {
            return true;
        }

        InfrastructureEntityReference pipe_reference;
        pipe_reference.type = InfrastructureEntity::Pipe;
        if (this->hydraulic_data)
        {
            pipe_reference.uuid = this->hydraulic_data->addPipe(
                start_marker->entity.uuid, end_marker->entity.uuid,
                this->pipes->intermediateVertices());
        }
        if (pipe_reference.uuid.isNull())
            return true;

        if (!this->pipes->completePipe(
                pipe_reference, start_marker->entity, end_marker->entity))
        {
            deleteHydraulicLink(pipe_reference);
            return true;
        }

        this->placement->setFloatingHiddenUntil(position.toPoint());
        this->placement->consumeCreatedMarker();
        this->placement->clearConnectionTarget();
        this->placement->rearmCreate(
            this->point_markers->pixmapPathForEntity(InfrastructureEntity::Pipe),
            this->point_markers->entityWidth());
        updateCanvas();
        return true;
    }

    const CoordinateWGS84 intermediate_vertex = this->map_model->wgs84FromScreen(
        position.toPoint(), this->map_canvas->size());
    this->pipes->appendIntermediateVertex(intermediate_vertex);
    updateCanvas();
    return true;
}

bool MapCanvasEntities::anchorPipeVertexMove(const QPointF &position)
{
    if (!this->placement->isMoving() || !this->pipes->isPipeVertexMoveActive())
        return false;

    const std::optional<QUuid> pipe_uuid = this->pipes->activePipeVertexMoveUuid();
    const bool moved = this->pipes->finishPipeVertexMove(position);
    bool synchronized = false;
    if (moved && pipe_uuid.has_value() && this->hydraulic_data)
    {
        HydraulicGeometryBatch batch;
        batch.pipe_vertices.insert(
            pipe_uuid.value(), this->pipes->intermediateVertices(pipe_uuid.value()));
        const QScopedValueRollback<bool> synchronization_guard(
            this->synchronizing_geometry, true);
        synchronized = this->hydraulic_data->applyGeometryBatch(batch);
    }

    if (!synchronized)
        restoreMoveSnapshot();

    this->placement->completeMove();
    clearMoveSnapshot();
    updateCanvas();
    return true;
}

void MapCanvasEntities::setWrapReferenceLongitude(double longitude)
{
    const double normalized_longitude = GeoWebMercator::normalizeLongitude(longitude);
    this->wrap_reference_longitude = normalized_longitude;
    this->point_markers->setWrapReferenceLongitude(normalized_longitude);
    this->device_links->setWrapReferenceLongitude(normalized_longitude);
    this->pipes->setWrapReferenceLongitude(normalized_longitude);
}

void MapCanvasEntities::recalculateWrapReferenceLongitude()
{
    const QList<MapEntityMarker> &markers = this->point_markers->markers();
    if (markers.isEmpty())
    {
        setWrapReferenceLongitude(this->map_model->centerLon());
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
    setWrapReferenceLongitude(reference_longitude);
}

void MapCanvasEntities::setIconSizePercent(int size_percent)
{
    this->point_markers->setIconSizePercent(size_percent);
    scaleMarkers();
}

void MapCanvasEntities::scaleMarkers()
{
    const int width = this->point_markers->entityWidth();
    this->point_markers->scaleMarkers(width);
    this->device_links->scaleMarkers(width);

    if (this->placement->hasFloatingMarker())
    {
        int floating_width = width;
        if (this->placement->isCreating() &&
            !InfrastructureEntityTraits::isHydraulicNetworkLink(this->placement->entity()))
            floating_width = qMax(1, qRound(150.0 * this->point_markers->iconSizePercent() / 100.0));
        this->placement->scaleFloatingMarker(
            this->point_markers->pixmapPathForEntity(this->placement->entity()), floating_width);
    }
    updateCanvas();
}

void MapCanvasEntities::positionMarkers()
{
    repaintCanvas();
}

MapEditorVisualState MapCanvasEntities::visualState() const
{
    MapEditorVisualState state;
    state.revision = this->visual_state_revision;
    state.selected_marker_uuids = this->selection->selectedMarkerUuids();
    state.selected_pipe_uuids = this->pipes->selectedPipeUuids();
    if (this->hydraulic_data)
    {
        state.simulation_error_entities = this->hydraulic_data->simulationErrorEntities();
        state.simulation_stale_diagnostic_entity_uuids =
            this->hydraulic_data->simulationStaleDiagnosticEntityUuids();
    }
    state.wrap_reference_longitude = this->wrap_reference_longitude;
    state.entity_width = this->point_markers->entityWidth();

    state.placement.creating = this->placement->isCreating();
    state.placement.floating_marker_visible =
        this->placement->isCreating() && this->placement->floatingMarkerVisible();
    state.placement.entity = this->placement->entity();
    state.placement.connection_target_uuid = this->placement->connectionTargetUuid();
    state.placement.pipe_start_node_uuid = this->pipes->startNodeUuid();
    state.placement.pipe_intermediate_vertices = this->pipes->intermediateVertices();
    state.placement.device_link_start_node_uuid = this->device_links->startNodeUuid();
    state.placement.floating_width = this->placement->floatingWidth();
    state.placement.mouse_position = this->placement->mousePosition();
    if (this->map_model && this->map_canvas && !this->map_canvas->size().isEmpty())
    {
        state.placement.mouse_coordinate_wgs84 = this->map_model->wgs84FromScreen(
            this->placement->mousePosition().toPoint(), this->map_canvas->size());
        state.placement.mouse_coordinate_wgs84_valid = true;
    }

    state.move.active = this->placement->isMoving();
    state.move.session_id = this->move_session_id;
    if (state.move.active)
    {
        state.move.markers.reserve(this->move_marker_snapshot.size());
        for (const MapEntityMarker &original_marker : this->move_marker_snapshot)
        {
            std::optional<MapEntityMarker> marker = this->point_markers->markerByUuid(
                original_marker.entity.uuid);
            if (!marker.has_value())
                marker = this->device_links->markerByUuid(original_marker.entity.uuid);
            if (!marker.has_value())
                continue;

            MapEditorDynamicMarkerVisualState dynamic_marker;
            dynamic_marker.entity = marker->entity.type;
            dynamic_marker.uuid = marker->entity.uuid;
            dynamic_marker.coordinate_wgs84 = marker->coord_wgs84;
            dynamic_marker.pixmap_path = marker->path_pixmap;
            state.move.markers.append(dynamic_marker);
        }

        state.move.links.reserve(this->move_dynamic_pipe_uuids.size() +
                                 this->move_dynamic_device_link_uuids.size());
        for (const QUuid &pipe_uuid : this->move_dynamic_pipe_uuids)
        {
            const std::optional<PipeGeometry> geometry = this->pipes->geometryByUuid(pipe_uuid);
            if (!geometry.has_value())
                continue;
            const std::optional<MapEntityMarker> start_marker =
                this->point_markers->markerByUuid(geometry->start_node.uuid);
            const std::optional<MapEntityMarker> end_marker =
                this->point_markers->markerByUuid(geometry->end_node.uuid);
            if (!start_marker.has_value() || !end_marker.has_value())
                continue;

            MapEditorDynamicLinkVisualState dynamic_link;
            dynamic_link.entity = InfrastructureEntity::Pipe;
            dynamic_link.uuid = pipe_uuid;
            dynamic_link.vertices_wgs84.reserve(geometry->intermediate_vertices.size() + 2);
            dynamic_link.vertices_wgs84.append(start_marker->coord_wgs84);
            dynamic_link.vertices_wgs84.append(geometry->intermediate_vertices);
            dynamic_link.vertices_wgs84.append(end_marker->coord_wgs84);
            state.move.links.append(dynamic_link);
        }

        for (const QUuid &link_uuid : this->move_dynamic_device_link_uuids)
        {
            const std::optional<DeviceLinkGeometry> geometry =
                this->device_links->geometryByUuid(link_uuid);
            const std::optional<MapEntityMarker> marker =
                this->device_links->markerByUuid(link_uuid);
            if (!geometry.has_value() || !marker.has_value())
                continue;
            const std::optional<MapEntityMarker> start_marker =
                this->point_markers->markerByUuid(geometry->start_node.uuid);
            const std::optional<MapEntityMarker> end_marker =
                this->point_markers->markerByUuid(geometry->end_node.uuid);
            if (!start_marker.has_value() || !end_marker.has_value())
                continue;

            MapEditorDynamicLinkVisualState dynamic_link;
            dynamic_link.entity = marker->entity.type;
            dynamic_link.uuid = link_uuid;
            dynamic_link.vertices_wgs84 = {
                start_marker->coord_wgs84, geometry->center_coordinate, end_marker->coord_wgs84};
            state.move.links.append(dynamic_link);
        }
    }

    return state;
}

bool MapCanvasEntities::selectMarkerAt(const QPointF &position)
{
    if (!this->placement->isIdle())
        return false;

    std::optional<InfrastructureEntityReference> entity = this->point_markers->markerAt(position);
    if (!entity.has_value())
        entity = this->device_links->markerAt(position);
    if (!entity.has_value())
        return false;

    selectMarker(entity->uuid);
    return true;
}

bool MapCanvasEntities::isMarkerAt(const QPointF &position) const
{
    if (!this->placement->isIdle())
        return false;
    return this->point_markers->isMarkerAt(position) ||
           this->device_links->markerAt(position).has_value();
}

bool MapCanvasEntities::showMarkerContextMenuAt(const QPointF &position,
                                                const QPoint &global_position)
{
    if (!this->placement->isIdle())
        return false;

    std::optional<InfrastructureEntityReference> entity = this->point_markers->markerAt(position);
    if (!entity.has_value())
        entity = this->device_links->markerAt(position);
    if (!entity.has_value())
        return false;

    showMarkerContextMenu(entity->uuid, global_position);
    return true;
}

bool MapCanvasEntities::selectDeviceLinkAt(const QPointF &position)
{
    if (!this->placement->isIdle())
        return false;

    const std::optional<InfrastructureEntityReference> device_link =
        this->device_links->linkAt(position, this->point_markers->markers());
    if (!device_link.has_value())
        return false;

    selectMarker(device_link->uuid);
    return true;
}

bool MapCanvasEntities::isDeviceLinkAt(const QPointF &position) const
{
    return this->placement->isIdle() &&
           this->device_links->linkAt(position, this->point_markers->markers()).has_value();
}

bool MapCanvasEntities::showPipeContextMenuAt(const QPointF &position,
                                              const QPoint &global_position)
{
    return this->placement->isIdle() &&
           this->pipes->showContextMenuAt(
               position, global_position, this->point_markers->markers());
}

void MapCanvasEntities::selectMarker(const QUuid &uuid)
{
    const std::optional<MapEntityMarker> marker = markerByUuid(uuid);
    if (!marker.has_value())
        return;

    if (QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
    {
        this->selection->toggleMarker(marker.value());
        emit signalEntityMarkerSelected(this->selection->hasSelection());
        updateCanvas();
        return;
    }

    this->selection->replaceWithMarker(marker.value());
    emit signalEntityMarkerSelected(true);
    if (this->hydraulic_data)
        this->hydraulic_data->setSelectedUuid(marker->entity.type, marker->entity.uuid);
    updateCanvas();
}

void MapCanvasEntities::applyExternalSelection(
    InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (uuid.isNull())
        return;

    if (entity_type == InfrastructureEntity::Pipe)
    {
        const std::optional<InfrastructureEntityReference> selected_pipe =
            this->selection->replaceWithPipe(uuid);
        if (!selected_pipe.has_value())
            return;
    }
    else
    {
        const std::optional<MapEntityMarker> marker = markerByUuid(uuid);
        if (!marker.has_value() || marker->entity.type != entity_type)
            return;
        this->selection->replaceWithMarker(marker.value());
    }

    emit signalEntityMarkerSelected(true);
    updateCanvas();
}

void MapCanvasEntities::startMarkerMove(const QUuid &uuid)
{
    const std::optional<MapEntityMarker> marker = markerByUuid(uuid);
    if (!marker.has_value())
        return;

    stopEntityPositioning();
    const QPointF mouse_position = this->map_canvas->mapFromGlobal(QCursor::pos());
    captureMarkerMoveSnapshot(uuid);
    const CoordinateWGS84 mouse_coordinate = this->map_model->wgs84FromScreen(
        mouse_position.toPoint(), this->map_canvas->size());
    if (!this->placement->startMove(
            marker->entity.type, uuid, marker->path_pixmap,
            this->point_markers->entityWidth(), mouse_position, mouse_coordinate))
    {
        clearMoveSnapshot();
    }
    else
    {
        prepareMoveVisualState();
    }
    updateCanvas();
}

void MapCanvasEntities::startSelectedMarkerMove(const QUuid &uuid)
{
    if (!this->selection->isMarkerSelected(uuid) ||
        this->selection->selectedMarkerCount() < 2)
    {
        return;
    }

    startMarkerMove(uuid);
    if (!this->placement->isMoving() || this->placement->floatingUuid() != uuid)
        return;

    this->placement->setMovingSelected(true);
    this->pipes->selectPipesWithSelectedEndpoints(
        this->selection->selectedMarkerUuids());
    captureSelectedMoveSnapshot();
    prepareMoveVisualState();
    updateCanvas();
}

void MapCanvasEntities::onMarkerSelectedDeleteRequested()
{
    const QList<QUuid> marker_uuids_to_delete = this->selection->selectedMarkerUuids();
    const QList<QUuid> pipe_uuids_to_delete = this->pipes->selectedPipeUuids();
    for (const QUuid &uuid : marker_uuids_to_delete)
        deleteMarker(uuid);

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

void MapCanvasEntities::deleteMarker(const QUuid &uuid)
{
    const std::optional<MapEntityMarker> marker = markerByUuid(uuid);
    if (!marker.has_value())
        return;

    if (this->placement->floatingUuid() == uuid)
        stopEntityPositioning();

    if (InfrastructureEntityTraits::isHydraulicConnectionNode(marker->entity.type) &&
        !deleteHydraulicNode(marker->entity))
        return;
    if (InfrastructureEntityTraits::isHydraulicDeviceLink(marker->entity.type) &&
        !deleteHydraulicLink(marker->entity))
        return;

    this->selection->clear();
    if (this->placement->connectionTargetUuid() == uuid)
        this->placement->clearConnectionTarget();
    if (this->pipes->startNodeUuid() == uuid)
        this->pipes->clearPlacement();

    this->device_links->removeConnectedToUuid(uuid);
    this->pipes->removeConnectedToUuid(uuid);
    this->selection->removeMarker(uuid);

    const bool point_marker_removed = this->point_markers->removeMarker(uuid);
    if (point_marker_removed)
        recalculateWrapReferenceLongitude();
}

void MapCanvasEntities::showMarkerContextMenu(const QUuid &uuid,
                                              const QPoint &global_position)
{
    if (!markerByUuid(uuid).has_value())
        return;

    const bool multiple_entities_selected = this->selection->isMarkerSelected(uuid) &&
                                            this->selection->selectedMarkerCount() > 1;
    QMenu *menu = new QMenu(this->map_canvas);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    if (multiple_entities_selected)
    {
        QAction *action_move_this = menu->addAction("Move this entity");
        QAction *action_move_selected = menu->addAction("Move selected entities");
        connect(action_move_this, &QAction::triggered, this, [this, uuid]()
        {
            startMarkerMove(uuid);
        });
        connect(action_move_selected, &QAction::triggered, this, [this, uuid]()
        {
            startSelectedMarkerMove(uuid);
        });
    }
    else
    {
        QAction *action_move = menu->addAction("Move");
        connect(action_move, &QAction::triggered, this, [this, uuid]()
        {
            startMarkerMove(uuid);
        });
    }

    menu->addSeparator();

    const QString delete_this_text = multiple_entities_selected
                                         ? QStringLiteral("Delete this entity")
                                         : QStringLiteral("Delete");
    QAction *action_delete_this = menu->addAction(delete_this_text);
    connect(action_delete_this, &QAction::triggered, this, [this, uuid]()
    {
        QMessageBox *box = new QMessageBox(this->map_canvas);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setIcon(QMessageBox::Question);
        box->setWindowTitle("Delete entity");
        box->setText("Do you really want to delete this entity?");
        box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box->setDefaultButton(QMessageBox::No);
        connect(box, &QMessageBox::buttonClicked, this, [this, box, uuid](QAbstractButton *button)
        {
            if (box->standardButton(button) == QMessageBox::Yes)
            {
                deleteMarker(uuid);
                updateCanvas();
            }
        });
        box->open();
    });

    if (multiple_entities_selected)
    {
        QAction *action_delete_selected = menu->addAction("Delete selected entities");
        connect(action_delete_selected, &QAction::triggered, this, [this]()
        {
            QMessageBox *box = new QMessageBox(this->map_canvas);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setIcon(QMessageBox::Question);
            box->setWindowTitle("Delete selected entities");
            box->setText("Do you really want to delete all selected entities?");
            box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            box->setDefaultButton(QMessageBox::No);
            connect(box, &QMessageBox::buttonClicked, this,
                    [this, box](QAbstractButton *button)
            {
                if (box->standardButton(button) == QMessageBox::Yes)
                    onMarkerSelectedDeleteRequested();
            });
            box->open();
        });
    }

    menu->popup(global_position);
}

void MapCanvasEntities::onRectangleSelect(const QRect &rect, RectangleSelectMode mode)
{
    const QList<QUuid> previous_marker_uuids = this->selection->selectedMarkerUuids();
    const QList<QUuid> previous_pipe_uuids = this->pipes->selectedPipeUuids();
    this->selection->selectInRectangle(
        rect, this->point_markers->markers(), this->device_links->markers(),
        mode == RectangleSelectMode::Replace);

    if (previous_marker_uuids == this->selection->selectedMarkerUuids() &&
        previous_pipe_uuids == this->pipes->selectedPipeUuids())
    {
        return;
    }

    emit signalEntityMarkerSelected(this->selection->hasSelection());
    updateCanvas();
}

void MapCanvasEntities::updateConnectionTarget(const QPointF &mouse_position)
{
    std::optional<InfrastructureEntityReference> nearest_entity;
    if (this->placement->isCreating() &&
        InfrastructureEntityTraits::isHydraulicNetworkLink(this->placement->entity()))
    {
        QUuid excluded_uuid;
        if (InfrastructureEntityTraits::isHydraulicDeviceLink(this->placement->entity()))
            excluded_uuid = this->device_links->startNodeUuid();
        nearest_entity = this->point_markers->nearestConnectionTarget(
            mouse_position, excluded_uuid);
    }

    const QUuid nearest_uuid = nearest_entity.has_value() ? nearest_entity->uuid : QUuid();
    if (this->placement->connectionTargetUuid() == nearest_uuid)
        return;

    this->placement->setConnectionTarget(nearest_uuid);
    updateCanvas();
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
    prepareMoveVisualState();
    const QPointF mouse_position = this->map_canvas->mapFromGlobal(QCursor::pos());
    const CoordinateWGS84 mouse_coordinate = this->map_model->wgs84FromScreen(
        mouse_position.toPoint(), this->map_canvas->size());
    this->placement->startVirtualMove(
        InfrastructureEntity::Pipe, mouse_position, mouse_coordinate);
}

void MapCanvasEntities::convertPipeVertexToJunction(const QUuid &pipe_uuid,
                                                    int vertex_index)
{
    const std::optional<CoordinateWGS84> vertex_coordinate =
        this->pipes->pipeVertexCoordinate(pipe_uuid, vertex_index);
    if (!vertex_coordinate.has_value())
        return;

    InfrastructureEntityReference junction_reference;
    junction_reference.type = InfrastructureEntity::Junction;
    junction_reference.uuid = createHydraulicNode(
        InfrastructureEntity::Junction, vertex_coordinate.value());
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
        this->point_markers->removeMarker(junction_marker.entity.uuid);
        this->hydraulic_data->deleteJunction(junction_reference.uuid);
        return;
    }

    if (!this->pipes->splitPipeAtVertex(
            pipe_uuid, vertex_index, junction_reference, second_pipe_reference))
    {
        this->hydraulic_data->undoPipeSplit(
            pipe_uuid, second_pipe_reference.uuid, junction_reference.uuid);
        this->point_markers->removeMarker(junction_marker.entity.uuid);
        this->hydraulic_data->deleteJunction(junction_reference.uuid);
        return;
    }

    this->selection->replaceWithMarker(junction_marker);
    emit signalEntityMarkerSelected(true);
    recalculateWrapReferenceLongitude();
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

bool MapCanvasEntities::isPipeAt(const QPointF &position) const
{
    return this->placement->isIdle() &&
           this->pipes->pipeAt(position, this->point_markers->markers()).has_value();
}

std::optional<MapEntityMarker> MapCanvasEntities::markerByUuid(const QUuid &uuid) const
{
    std::optional<MapEntityMarker> marker = this->point_markers->markerByUuid(uuid);
    if (marker.has_value())
        return marker;
    return this->device_links->markerByUuid(uuid);
}

void MapCanvasEntities::repaintCanvas()
{
    if (this->map_canvas)
        this->map_canvas->requestRenderUpdate();
}

void MapCanvasEntities::updateCanvas()
{
    ++this->visual_state_revision;
    emit signalVisualStateChanged(this->visual_state_revision);
    repaintCanvas();
}
