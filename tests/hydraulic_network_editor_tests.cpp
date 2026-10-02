#include "network/hydraulic_network_editor.h"
#include "network/network_render_snapshot_builder.h"

#include <QDate>

#include <cmath>
#include <cstdio>

namespace
{
int failure_count = 0;

void expectTrue(bool condition, const char *message)
{
    if (condition)
        return;

    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failure_count;
}

void expectNear(double actual, double expected, double tolerance, const char *message)
{
    if (std::isfinite(actual) && std::abs(actual - expected) <= tolerance)
        return;

    std::fprintf(stderr,
                 "FAIL: %s (actual=%.12f expected=%.12f tolerance=%.12f)\n",
                 message, actual, expected, tolerance);
    ++failure_count;
}

void testDemandPointLifecycle()
{
    NetworkHydraulic network;
    HydraulicNetworkEditor editor(network);

    CoordinateWGS84 first_coordinate;
    first_coordinate.latitude_deg = 11.95;
    first_coordinate.longitude_deg = 18.18;

    const QUuid first_uuid = editor.addDemandPoint(first_coordinate);
    expectTrue(!first_uuid.isNull(), "demand point receives a UUID");
    expectTrue(network.demand_points.size() == 1, "demand point is appended to the network");
    expectTrue(network.demand_points.first().id == QStringLiteral("DP1"),
               "first demand point receives DP1 ID");
    expectTrue(network.demand_points.first().metadata.date_added.has_value(),
               "demand point receives date-added metadata");
    if (network.demand_points.first().metadata.date_added.has_value())
    {
        expectTrue(network.demand_points.first().metadata.date_added.value() == QDate::currentDate(),
                   "demand point date-added is today");
    }

    CoordinateWGS84 second_coordinate;
    second_coordinate.latitude_deg = 11.96;
    second_coordinate.longitude_deg = 18.19;
    const QUuid second_uuid = editor.addDemandPoint(second_coordinate);
    expectTrue(!second_uuid.isNull(), "second demand point receives a UUID");
    expectTrue(network.demand_points.size() == 2, "second demand point is appended");
    expectTrue(network.demand_points.at(1).id == QStringLiteral("DP2"),
               "second demand point receives DP2 ID");

    expectTrue(editor.setDemandPointId(second_uuid, QStringLiteral("School-01")),
               "demand point ID can be edited");
    expectTrue(editor.setDemandPointModelRole(second_uuid, EntityModelRole::ExistingAsset),
               "demand point model role can be edited");
    expectTrue(editor.setDemandPointDescription(
                   second_uuid, QStringLiteral("Primary school consumption")),
               "demand point description can be edited");
    expectTrue(editor.setDemandPointComment(second_uuid, QStringLiteral("Field note")),
               "demand point comment can be edited");
    expectTrue(editor.setDemandPointTags(
                   second_uuid, QStringList{QStringLiteral("school"), QStringLiteral("public")}),
               "demand point tags can be edited");
    expectTrue(editor.setDemandPointEnabled(second_uuid, false),
               "demand point can be disabled");
    expectTrue(editor.setDemandPointEnabled(second_uuid, true),
               "demand point can be re-enabled");

    HydraulicDemand demand;
    demand.category_name = QStringLiteral("students");
    demand.base_demand_m3_per_h = 1.25;
    expectTrue(editor.addDemandPointDemand(second_uuid, demand),
               "demand can be added to a demand point");
    expectTrue(editor.setDemandPointDemandBaseDemandM3PerH(second_uuid, 0, 1.5),
               "demand-point base demand can be edited");
    expectTrue(editor.setDemandPointDemandNote(second_uuid, 0, QStringLiteral("Measured estimate")),
               "demand-point demand note can be edited");

    const std::optional<HydraulicDemandPoint> edited_second = editor.demandPoint(second_uuid);
    expectTrue(edited_second.has_value(), "edited demand point remains available");
    if (edited_second.has_value())
    {
        expectTrue(edited_second->id == QStringLiteral("School-01"),
                   "edited demand point ID is stored");
        expectTrue(edited_second->metadata.description ==
                       QStringLiteral("Primary school consumption"),
                   "edited demand point description is stored");
        expectTrue(edited_second->metadata.tags ==
                       QStringList{QStringLiteral("school"), QStringLiteral("public")},
                   "edited demand point tags are stored");
        expectTrue(edited_second->demands.size() == 1,
                   "demand point stores the added demand");
        if (edited_second->demands.size() == 1)
        {
            expectNear(edited_second->demands.first().base_demand_m3_per_h, 1.5, 1e-12,
                       "edited demand-point base demand is stored");
        }
    }
    expectTrue(editor.removeDemandPointDemand(second_uuid, 0),
               "demand can be removed from a demand point");

    expectTrue(editor.addDemandPointMeter(second_uuid),
               "a primary meter can be added to a demand point");
    expectTrue(!editor.addDemandPointMeter(second_uuid),
               "a demand point cannot receive a second primary meter");

    std::optional<HydraulicDemandPoint> metered_second = editor.demandPoint(second_uuid);
    expectTrue(metered_second.has_value() && metered_second->meter.has_value(),
               "added demand-point meter is stored");
    if (metered_second.has_value() && metered_second->meter.has_value())
    {
        expectTrue(!metered_second->meter->uuid.isNull(),
                   "demand-point meter receives a UUID");
        expectTrue(metered_second->meter->id == QStringLiteral("M1"),
                   "first demand-point meter receives M1 ID");
        expectTrue(metered_second->meter->metadata.date_added.has_value(),
                   "demand-point meter receives date-added metadata");

        WaterMeter edited_meter = metered_second->meter.value();
        edited_meter.id = QStringLiteral("School-Meter");
        edited_meter.manufacturer = QStringLiteral("Acme");
        edited_meter.model = QStringLiteral("WM-100");
        edited_meter.serial_number = QStringLiteral("SN-123");
        edited_meter.metadata.enabled = false;
        edited_meter.metadata.description = QStringLiteral("Primary school meter");
        edited_meter.metadata.comment = QStringLiteral("Installed at entrance");
        edited_meter.metadata.tags =
            QStringList{QStringLiteral("billing"), QStringLiteral("lorawan-ready")};
        edited_meter.metadata.model_role = EntityModelRole::ExistingAsset;
        edited_meter.metadata.date_installed = QDate(2026, 9, 1);

        expectTrue(editor.setDemandPointMeter(second_uuid, edited_meter),
                   "demand-point meter can be edited atomically");

        const std::optional<HydraulicDemandPoint> edited_meter_point =
            editor.demandPoint(second_uuid);
        expectTrue(edited_meter_point.has_value() && edited_meter_point->meter.has_value(),
                   "edited demand-point meter remains available");
        if (edited_meter_point.has_value() && edited_meter_point->meter.has_value())
        {
            expectTrue(edited_meter_point->meter->id == QStringLiteral("School-Meter"),
                       "edited meter ID is stored");
            expectTrue(edited_meter_point->meter->manufacturer == QStringLiteral("Acme"),
                       "edited meter manufacturer is stored");
            expectTrue(edited_meter_point->meter->serial_number == QStringLiteral("SN-123"),
                       "edited meter serial number is stored");
            expectTrue(!edited_meter_point->meter->metadata.enabled,
                       "edited meter enabled state is stored");
            expectTrue(edited_meter_point->meter->metadata.tags ==
                           QStringList{QStringLiteral("billing"),
                                       QStringLiteral("lorawan-ready")},
                       "edited meter tags are stored");
        }
    }

    expectTrue(editor.removeDemandPointMeter(second_uuid),
               "primary meter can be removed from a demand point");
    const std::optional<HydraulicDemandPoint> unmetered_second =
        editor.demandPoint(second_uuid);
    expectTrue(unmetered_second.has_value() && !unmetered_second->meter.has_value(),
               "removed demand-point meter is cleared");
    expectTrue(!editor.removeDemandPointMeter(second_uuid),
               "removing a missing demand-point meter fails cleanly");

    const NetworkRenderSnapshot render_snapshot = buildNetworkRenderSnapshot(network, 1, 1);
    bool first_demand_point_rendered = false;
    bool second_demand_point_rendered = false;
    for (const NetworkRenderNode &node : render_snapshot.nodes)
    {
        if (node.uuid == first_uuid)
        {
            first_demand_point_rendered =
                node.entity_type == InfrastructureEntity::DemandPoint &&
                std::isnan(node.elevation_m);
        }
        if (node.uuid == second_uuid)
        {
            second_demand_point_rendered =
                node.entity_type == InfrastructureEntity::DemandPoint &&
                std::isnan(node.elevation_m);
        }
    }
    expectTrue(first_demand_point_rendered,
               "first demand point is present in the render snapshot");
    expectTrue(second_demand_point_rendered,
               "second demand point is present in the render snapshot");

    const std::optional<HydraulicDemandPoint> first = editor.demandPoint(first_uuid);
    expectTrue(first.has_value(), "demand point can be looked up by UUID");
    if (first.has_value())
    {
        expectNear(first->coordinate_wgs84.latitude_deg, first_coordinate.latitude_deg, 1e-12,
                   "lookup preserves demand point latitude");
        expectNear(first->coordinate_wgs84.longitude_deg, first_coordinate.longitude_deg, 1e-12,
                   "lookup preserves demand point longitude");
    }

    CoordinateWGS84 moved_coordinate;
    moved_coordinate.latitude_deg = 12.01;
    moved_coordinate.longitude_deg = 18.25;
    expectTrue(editor.setDemandPointCoordinate(first_uuid, moved_coordinate),
               "demand point coordinate can be changed directly");

    const std::optional<HydraulicDemandPoint> moved = editor.demandPoint(first_uuid);
    expectTrue(moved.has_value(), "moved demand point remains available");
    if (moved.has_value())
    {
        expectNear(moved->coordinate_wgs84.latitude_deg, moved_coordinate.latitude_deg, 1e-12,
                   "direct move updates demand point latitude");
        expectNear(moved->coordinate_wgs84.longitude_deg, moved_coordinate.longitude_deg, 1e-12,
                   "direct move updates demand point longitude");
    }

    CoordinateWGS84 batch_coordinate;
    batch_coordinate.latitude_deg = 12.02;
    batch_coordinate.longitude_deg = 18.26;
    HydraulicGeometryBatch batch;
    batch.demand_point_coordinates.insert(first_uuid, batch_coordinate);
    const HydraulicGeometryBatchResult batch_result = editor.applyGeometryBatch(batch);
    expectTrue(batch_result.successful, "geometry batch accepts an existing demand point");

    const std::optional<HydraulicDemandPoint> batch_moved = editor.demandPoint(first_uuid);
    expectTrue(batch_moved.has_value(), "batch-moved demand point remains available");
    if (batch_moved.has_value())
    {
        expectNear(batch_moved->coordinate_wgs84.latitude_deg, batch_coordinate.latitude_deg, 1e-12,
                   "geometry batch updates demand point latitude");
        expectNear(batch_moved->coordinate_wgs84.longitude_deg, batch_coordinate.longitude_deg, 1e-12,
                   "geometry batch updates demand point longitude");
    }

    HydraulicGeometryBatch invalid_batch;
    CoordinateWGS84 invalid_coordinate;
    invalid_coordinate.latitude_deg = 1.0;
    invalid_coordinate.longitude_deg = 2.0;
    invalid_batch.demand_point_coordinates.insert(QUuid::createUuid(), invalid_coordinate);
    expectTrue(!editor.applyGeometryBatch(invalid_batch).successful,
               "geometry batch rejects an unknown demand point UUID");

    expectTrue(editor.deleteDemandPoint(first_uuid), "demand point can be deleted");
    expectTrue(!editor.demandPoint(first_uuid).has_value(),
               "deleted demand point cannot be looked up");
    expectTrue(network.demand_points.size() == 1,
               "deleting one demand point preserves the others");
    expectTrue(network.demand_points.first().uuid == second_uuid,
               "the remaining demand point is unchanged");

    CoordinateWGS84 junction_a_coordinate;
    junction_a_coordinate.latitude_deg = 12.0;
    junction_a_coordinate.longitude_deg = 18.2;
    const QUuid junction_a_uuid = editor.addJunction(junction_a_coordinate);
    expectTrue(!junction_a_uuid.isNull(), "first attachment junction receives a UUID");

    CoordinateWGS84 junction_b_coordinate;
    junction_b_coordinate.latitude_deg = 12.0;
    junction_b_coordinate.longitude_deg = 18.3;
    const QUuid junction_b_uuid = editor.addJunction(junction_b_coordinate);
    expectTrue(!junction_b_uuid.isNull(), "second attachment junction receives a UUID");

    const QUuid pipe_uuid = editor.addPipe(junction_a_uuid, junction_b_uuid, {});
    expectTrue(!pipe_uuid.isNull(), "attachment pipe receives a UUID");

    expectTrue(editor.attachDemandPointToPipe(second_uuid, pipe_uuid, 0.25),
               "demand point can attach to a normalized position on a pipe");
    std::optional<HydraulicDemandPoint> attached = editor.demandPoint(second_uuid);
    expectTrue(attached.has_value(), "pipe-attached demand point remains available");
    if (attached.has_value())
    {
        expectTrue(attached->attachment.type == HydraulicDemandPointAttachmentType::Pipe,
                   "pipe attachment records pipe attachment type");
        expectTrue(attached->attachment.pipe_uuid == pipe_uuid,
                   "pipe attachment records supplying pipe UUID");
        expectNear(attached->attachment.pipe_position, 0.25, 1e-12,
                   "pipe attachment records normalized pipe position");
        expectTrue(attached->attachment.junction_uuid.isNull(),
                   "pipe attachment does not retain a stale junction UUID");
    }

    expectTrue(!editor.attachDemandPointToPipe(second_uuid, pipe_uuid, -0.1),
               "negative pipe attachment position is rejected");
    expectTrue(!editor.attachDemandPointToPipe(second_uuid, pipe_uuid, 1.1),
               "pipe attachment position above one is rejected");

    const NetworkRenderSnapshot pipe_attachment_snapshot =
        buildNetworkRenderSnapshot(network, 2, 2);
    expectTrue(pipe_attachment_snapshot.demand_point_attachments.size() == 1,
               "pipe-attached demand point is present in the render snapshot");
    if (pipe_attachment_snapshot.demand_point_attachments.size() == 1)
    {
        const NetworkRenderDemandPointAttachment &render_attachment =
            pipe_attachment_snapshot.demand_point_attachments.first();
        expectTrue(render_attachment.demand_point_uuid == second_uuid,
                   "render attachment retains demand-point UUID");
        expectTrue(render_attachment.demand_point_render_id != 0,
                   "render attachment uses the demand-point render ID");
        expectNear(render_attachment.attachment_coordinate_wgs84.latitude_deg,
                   junction_a_coordinate.latitude_deg, 1e-9,
                   "pipe attachment render coordinate follows pipe latitude");
        expectNear(render_attachment.attachment_coordinate_wgs84.longitude_deg,
                   18.225, 1e-9,
                   "pipe attachment render coordinate follows normalized pipe position");
    }

    expectTrue(editor.attachDemandPointToJunction(second_uuid, junction_a_uuid),
               "demand point can attach directly to a junction");
    attached = editor.demandPoint(second_uuid);
    expectTrue(attached.has_value(), "junction-attached demand point remains available");
    if (attached.has_value())
    {
        expectTrue(attached->attachment.type == HydraulicDemandPointAttachmentType::Junction,
                   "junction attachment records junction attachment type");
        expectTrue(attached->attachment.junction_uuid == junction_a_uuid,
                   "junction attachment records supplying junction UUID");
        expectTrue(attached->attachment.pipe_uuid.isNull(),
                   "junction attachment does not retain a stale pipe UUID");
    }

    const NetworkRenderSnapshot junction_attachment_snapshot =
        buildNetworkRenderSnapshot(network, 3, 3);
    expectTrue(junction_attachment_snapshot.demand_point_attachments.size() == 1,
               "junction-attached demand point is present in the render snapshot");
    if (junction_attachment_snapshot.demand_point_attachments.size() == 1)
    {
        const NetworkRenderDemandPointAttachment &render_attachment =
            junction_attachment_snapshot.demand_point_attachments.first();
        expectNear(render_attachment.attachment_coordinate_wgs84.latitude_deg,
                   junction_a_coordinate.latitude_deg, 1e-12,
                   "junction attachment render coordinate follows junction latitude");
        expectNear(render_attachment.attachment_coordinate_wgs84.longitude_deg,
                   junction_a_coordinate.longitude_deg, 1e-12,
                   "junction attachment render coordinate follows junction longitude");
    }

    expectTrue(editor.deleteJunction(junction_a_uuid),
               "junction with an attached demand point can be deleted");
    attached = editor.demandPoint(second_uuid);
    expectTrue(attached.has_value(), "demand point survives deletion of its attached junction");
    if (attached.has_value())
    {
        expectTrue(attached->attachment.type == HydraulicDemandPointAttachmentType::None,
                   "deleting an attached junction detaches the demand point");
    }

    CoordinateWGS84 junction_c_coordinate;
    junction_c_coordinate.latitude_deg = 12.1;
    junction_c_coordinate.longitude_deg = 18.2;
    const QUuid junction_c_uuid = editor.addJunction(junction_c_coordinate);
    CoordinateWGS84 junction_d_coordinate;
    junction_d_coordinate.latitude_deg = 12.1;
    junction_d_coordinate.longitude_deg = 18.3;
    const QUuid junction_d_uuid = editor.addJunction(junction_d_coordinate);
    const QUuid second_pipe_uuid = editor.addPipe(junction_c_uuid, junction_d_uuid, {});
    expectTrue(editor.attachDemandPointToPipe(second_uuid, second_pipe_uuid, 0.60),
               "demand point can reattach to another pipe");
    expectTrue(editor.deletePipe(second_pipe_uuid),
               "pipe with an attached demand point can be deleted");
    attached = editor.demandPoint(second_uuid);
    expectTrue(attached.has_value(), "demand point survives deletion of its attached pipe");
    if (attached.has_value())
    {
        expectTrue(attached->attachment.type == HydraulicDemandPointAttachmentType::None,
                   "deleting an attached pipe detaches the demand point");
    }
}

void testDemandPointAttachmentRemappingOnPipeSplit()
{
    NetworkHydraulic network;
    HydraulicNetworkEditor editor(network);

    CoordinateWGS84 from_coordinate;
    from_coordinate.latitude_deg = 0.0;
    from_coordinate.longitude_deg = 0.0;
    const QUuid from_uuid = editor.addJunction(from_coordinate);

    CoordinateWGS84 to_coordinate;
    to_coordinate.latitude_deg = 0.0;
    to_coordinate.longitude_deg = 2.0;
    const QUuid to_uuid = editor.addJunction(to_coordinate);

    CoordinateWGS84 midpoint_coordinate;
    midpoint_coordinate.latitude_deg = 0.0;
    midpoint_coordinate.longitude_deg = 1.0;
    const QUuid pipe_uuid = editor.addPipe(from_uuid, to_uuid, {midpoint_coordinate});
    expectTrue(!pipe_uuid.isNull(), "split-remapping pipe receives a UUID");

    CoordinateWGS84 demand_coordinate;
    demand_coordinate.latitude_deg = 0.2;
    demand_coordinate.longitude_deg = 1.0;
    const QUuid before_uuid = editor.addDemandPoint(demand_coordinate);
    const QUuid at_uuid = editor.addDemandPoint(demand_coordinate);
    const QUuid after_uuid = editor.addDemandPoint(demand_coordinate);

    expectTrue(editor.attachDemandPointToPipe(before_uuid, pipe_uuid, 0.25),
               "pre-split demand point attaches before split position");
    expectTrue(editor.attachDemandPointToPipe(at_uuid, pipe_uuid, 0.50),
               "pre-split demand point attaches at split position");
    expectTrue(editor.attachDemandPointToPipe(after_uuid, pipe_uuid, 0.75),
               "pre-split demand point attaches after split position");

    const QUuid split_junction_uuid = editor.addJunction(midpoint_coordinate);
    const QUuid second_pipe_uuid = editor.splitPipeAtVertex(pipe_uuid, 0, split_junction_uuid);
    expectTrue(!second_pipe_uuid.isNull(), "pipe can be split while demand points are attached");

    const std::optional<HydraulicDemandPoint> before_split = editor.demandPoint(before_uuid);
    const std::optional<HydraulicDemandPoint> at_split = editor.demandPoint(at_uuid);
    const std::optional<HydraulicDemandPoint> after_split = editor.demandPoint(after_uuid);
    expectTrue(before_split.has_value() && at_split.has_value() && after_split.has_value(),
               "all demand points survive pipe split");
    if (before_split.has_value())
    {
        expectTrue(before_split->attachment.type == HydraulicDemandPointAttachmentType::Pipe,
                   "attachment before split remains on a pipe");
        expectTrue(before_split->attachment.pipe_uuid == pipe_uuid,
                   "attachment before split remains on first pipe");
        expectNear(before_split->attachment.pipe_position, 0.50, 1e-9,
                   "attachment before split is renormalized on first pipe");
    }
    if (at_split.has_value())
    {
        expectTrue(at_split->attachment.type == HydraulicDemandPointAttachmentType::Junction,
                   "attachment exactly at split becomes a junction attachment");
        expectTrue(at_split->attachment.junction_uuid == split_junction_uuid,
                   "attachment exactly at split targets the new junction");
    }
    if (after_split.has_value())
    {
        expectTrue(after_split->attachment.type == HydraulicDemandPointAttachmentType::Pipe,
                   "attachment after split remains on a pipe");
        expectTrue(after_split->attachment.pipe_uuid == second_pipe_uuid,
                   "attachment after split moves to second pipe");
        expectNear(after_split->attachment.pipe_position, 0.50, 1e-9,
                   "attachment after split is renormalized on second pipe");
    }

    expectTrue(editor.undoPipeSplit(pipe_uuid, second_pipe_uuid, split_junction_uuid),
               "pipe split can be undone with demand-point attachments");
    const std::optional<HydraulicDemandPoint> before_undo = editor.demandPoint(before_uuid);
    const std::optional<HydraulicDemandPoint> at_undo = editor.demandPoint(at_uuid);
    const std::optional<HydraulicDemandPoint> after_undo = editor.demandPoint(after_uuid);
    if (before_undo.has_value())
    {
        expectTrue(before_undo->attachment.pipe_uuid == pipe_uuid,
                   "undo restores pre-split attachment to original pipe");
        expectNear(before_undo->attachment.pipe_position, 0.25, 1e-9,
                   "undo restores pre-split normalized position");
    }
    if (at_undo.has_value())
    {
        expectTrue(at_undo->attachment.type == HydraulicDemandPointAttachmentType::Pipe,
                   "undo converts split-junction attachment back to pipe attachment");
        expectTrue(at_undo->attachment.pipe_uuid == pipe_uuid,
                   "undo restores split-point attachment to original pipe");
        expectNear(at_undo->attachment.pipe_position, 0.50, 1e-9,
                   "undo restores split-point normalized position");
    }
    if (after_undo.has_value())
    {
        expectTrue(after_undo->attachment.pipe_uuid == pipe_uuid,
                   "undo restores post-split attachment to original pipe");
        expectNear(after_undo->attachment.pipe_position, 0.75, 1e-9,
                   "undo restores post-split normalized position");
    }
}

}

int main()
{
    testDemandPointLifecycle();
    testDemandPointAttachmentRemappingOnPipeSplit();

    if (failure_count != 0)
    {
        std::fprintf(stderr, "%d test(s) failed\n", failure_count);
        return 1;
    }

    std::printf("All hydraulic network editor tests passed\n");
    return 0;
}
