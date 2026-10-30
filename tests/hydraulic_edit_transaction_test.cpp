#include "network/hydraulic_data.h"
#include "network/hydraulic_edit_transaction_capture.h"
#include "network/hydraulic_network_editor.h"
#include "test_harness.h"

#include <aowis/model/edit/model_edit_transaction.h>
#include <aowis/model/serialization/model_codec.h>

#include <QCoreApplication>
#include <QObject>

namespace
{
AowisTestHarness test_harness;

CoordinateWGS84 coordinate(double latitude, double longitude)
{
    CoordinateWGS84 result;
    result.latitude_deg = latitude;
    result.longitude_deg = longitude;
    return result;
}

std::optional<aowis::model::ModelEditTransaction> decoded(const QByteArray &bytes)
{
    QString error;
    const std::optional<aowis::model::ModelEditTransaction> transaction =
        aowis::model::ModelEditTransactionCodec::decode(bytes, &error);
    test_harness.expectTrue(transaction.has_value() && error.isEmpty(),
                            "transaction decodes with the canonical MODEL codec");
    return transaction;
}

void testCaptureOnlyTouchedEntities()
{
    NetworkHydraulic network;
    HydraulicNetworkEditor editor(network);
    HydraulicEditTransactionCapture create(QStringLiteral("hydraulic.junction.create"));
    const QUuid junction = editor.addJunction(coordinate(50.1, 8.4));
    test_harness.expectTrue(!junction.isNull(), "creates a junction");
    test_harness.expectTrue(create.watchCreated(junction), "captures entity creation");
    const std::optional<QByteArray> created = create.finish(network);
    test_harness.expectTrue(created.has_value(), "creation emits a reversible event");
    if (!created.has_value())
        return;
    const std::optional<QByteArray> repeated = create.finish(network);
    test_harness.expectTrue(repeated.has_value() && repeated.value() == created.value(),
                            "re-encoding same captured operation keeps UUID and exact bytes");
    const std::optional<aowis::model::ModelEditTransaction> transaction = decoded(created.value());
    if (!transaction.has_value() || transaction->edits.size() != 1)
    {
        test_harness.expectTrue(false, "creation is one edit");
        return;
    }
    const aowis::model::ModelEntityEdit &edit = transaction->edits.first();
    test_harness.expectTrue(edit.entityId == junction && !edit.before.has_value() &&
                            edit.after.has_value(), "creation has only after image");
    if (edit.after.has_value())
    {
        const std::optional<HydraulicNodeJunction> decoded_entity =
            aowis::model::ModelCodec<HydraulicNodeJunction>::decode(edit.after.value());
        test_harness.expectTrue(decoded_entity.has_value() && decoded_entity->uuid == junction,
                                "creation contains typed canonical junction bytes");
    }

    HydraulicEditTransactionCapture no_change(QStringLiteral("hydraulic.junction.rename"));
    test_harness.expectTrue(no_change.watch(network, junction), "watch existing junction");
    test_harness.expectTrue(!no_change.finish(network).has_value(),
                            "unchanged entity emits no edit");

    HydraulicEditTransactionCapture update(QStringLiteral("hydraulic.junction.rename"));
    update.watch(network, junction);
    test_harness.expectTrue(editor.setNodeId(junction, QStringLiteral("J-17")), "rename succeeds");
    const std::optional<QByteArray> updated = update.finish(network);
    test_harness.expectTrue(updated.has_value(), "mutation emits an event");
    if (updated.has_value())
    {
        const std::optional<aowis::model::ModelEditTransaction> update_tx = decoded(updated.value());
        test_harness.expectTrue(update_tx.has_value() && update_tx->edits.size() == 1 &&
                                update_tx->edits.first().before.has_value() &&
                                update_tx->edits.first().after.has_value(),
                                "mutation has both before and after images");
    }

    HydraulicEditTransactionCapture duplicate(QStringLiteral("hydraulic.junction.create"));
    duplicate.watch(network, junction);
    test_harness.expectTrue(!duplicate.watchCreated(junction), "duplicate UUID watch is rejected");
    QString error;
    test_harness.expectTrue(!duplicate.finish(network, &error).has_value() && !error.isEmpty(),
                            "invalid capture fails explicitly");
}

void testHydraulicDataLogicalOperations()
{
    HydraulicData data;
    QList<QByteArray> events;
    QString capture_error;
    QObject::connect(&data, &HydraulicData::signalLogicalEditRecorded,
                     &data, [&events](const QByteArray &bytes) { events.append(bytes); });
    QObject::connect(&data, &HydraulicData::signalLogicalEditCaptureFailed,
                     &data, [&capture_error](const QString &error) { capture_error = error; });

    test_harness.expectTrue(!data.logicalEditCaptureEnabled(),
                            "example browsing does not record history by default");
    data.setLogicalEditCaptureEnabled(true);
    const QUuid first = data.addJunction(coordinate(50.1, 8.4));
    const QUuid second = data.addJunction(coordinate(50.2, 8.5));
    test_harness.expectTrue(!first.isNull() && !second.isNull() && events.size() == 2,
                            "junction creation produces two separate user actions");

    QList<CoordinateWGS84> vertices;
    vertices.append(coordinate(50.15, 8.45));
    const QUuid pipe_uuid = data.addPipe(first, second, vertices);
    test_harness.expectTrue(!pipe_uuid.isNull() && events.size() == 3,
                            "pipe creation produces one event");
    test_harness.expectTrue(data.setPipeDiameterMm(pipe_uuid, 220.0),
                            "pipe property mutation succeeds");
    test_harness.expectTrue(events.size() == 4, "property edit produces one event");

    HydraulicGeometryBatch batch;
    batch.node_coordinates.insert(first, coordinate(50.12, 8.42));
    batch.node_coordinates.insert(second, coordinate(50.22, 8.52));
    test_harness.expectTrue(data.applyGeometryBatch(batch), "geometry batch succeeds");
    test_harness.expectTrue(events.size() == 5, "multi-node geometry batch produces one undo step");
    if (events.size() == 5)
    {
        const std::optional<aowis::model::ModelEditTransaction> batch_tx = decoded(events.last());
        test_harness.expectTrue(batch_tx.has_value() && batch_tx->edits.size() >= 2,
                                "geometry batch groups both node changes");
    }

    const QUuid inserted = data.addJunction(coordinate(50.15, 8.45));
    const int before_split = events.size();
    const QUuid second_pipe = data.splitPipeAtVertex(pipe_uuid, 0, inserted);
    test_harness.expectTrue(!second_pipe.isNull() && events.size() == before_split + 1,
                            "pipe split produces exactly one logical event");
    if (events.size() == before_split + 1)
    {
        const std::optional<aowis::model::ModelEditTransaction> split_tx = decoded(events.last());
        test_harness.expectTrue(split_tx.has_value() && split_tx->edits.size() == 2,
                                "pipe split contains original and newly created pipe");
    }
    const int before_invalid = events.size();
    test_harness.expectTrue(!data.setPipeDiameterMm(QUuid::createUuid(), 150.0),
                            "invalid pipe edit is rejected");
    test_harness.expectTrue(events.size() == before_invalid,
                            "failed operation is not journaled");
    test_harness.expectTrue(data.deletePipe(second_pipe), "deleting a pipe succeeds");
    test_harness.expectTrue(events.size() == before_invalid + 1,
                            "deletion produces one logical event");
    if (events.size() == before_invalid + 1)
    {
        const std::optional<aowis::model::ModelEditTransaction> deletion = decoded(events.last());
        test_harness.expectTrue(deletion.has_value() && deletion->edits.size() == 1 &&
                                deletion->edits.first().before.has_value() &&
                                !deletion->edits.first().after.has_value(),
                                "deletion contains a reversible before image");
    }
    test_harness.expectTrue(capture_error.isEmpty(), "no encoding errors in covered operations");
    data.replaceNetworkHydraulic(NetworkHydraulic{}, {});
    test_harness.expectTrue(!data.logicalEditCaptureEnabled(),
                            "switching model disables history capture until new working copy is bound");
}
void testPropertyCaptureAndNoop()
{
    HydraulicData data;
    QList<QByteArray> events;
    QObject::connect(&data, &HydraulicData::signalLogicalEditRecorded,
                     &data, [&events](const QByteArray &event) { events.append(event); });
    data.setLogicalEditCaptureEnabled(true);
    const QUuid first = data.addJunction(coordinate(50.0, 8.0));
    const QUuid second = data.addJunction(coordinate(50.1, 8.1));
    const QUuid pipe = data.addPipe(first, second, {});
    const QUuid demand_point = data.addDemandPoint(coordinate(50.05, 8.05));
    test_harness.expectTrue(!first.isNull() && !second.isNull() &&
                            !pipe.isNull() && !demand_point.isNull(),
                            "entities for property capture created");
    if (pipe.isNull() || demand_point.isNull())
        return;

    const int baseline = events.size();
    test_harness.expectTrue(data.setNodeId(first, QStringLiteral("A-17")),
                            "node ID change succeeds");
    test_harness.expectTrue(events.size() == baseline + 1,
                            "node ID produces one reversible transaction");
    if (events.size() > baseline)
    {
        const std::optional<aowis::model::ModelEditTransaction> tx = decoded(events.last());
        test_harness.expectTrue(tx.has_value() && tx->edits.size() == 1 &&
                                tx->edits.first().entityId == first &&
                                tx->edits.first().before.has_value() &&
                                tx->edits.first().after.has_value(),
                                "node ID records only changed node");
    }
    const int after_rename = events.size();
    test_harness.expectTrue(data.setNodeId(first, QStringLiteral("A-17")),
                            "same value accepted by editor");
    test_harness.expectTrue(events.size() == after_rename,
                            "no-op property change creates no undo step");

    test_harness.expectTrue(data.setDemandPointDescription(demand_point,
                                                           QStringLiteral("Street demand")),
                            "demand point description succeeds");
    test_harness.expectTrue(events.size() == after_rename + 1,
                            "demand point property captured");
    if (events.size() > after_rename)
    {
        const std::optional<aowis::model::ModelEditTransaction> tx = decoded(events.last());
        test_harness.expectTrue(tx.has_value() && tx->edits.size() == 1 &&
                                tx->edits.first().entityId == demand_point,
                                "demand point property journaled under its own UUID");
    }

    const int before_pipe = events.size();
    test_harness.expectTrue(data.setPipeMinorLoss(pipe, 0.25),
                            "pipe minor loss succeeds");
    test_harness.expectTrue(events.size() == before_pipe + 1,
                            "previously uncovered pipe property captured");
    const int before_failed = events.size();
    test_harness.expectTrue(!data.setNodeId(QUuid::createUuid(), QStringLiteral("missing")),
                            "invalid node change fails");
    test_harness.expectTrue(events.size() == before_failed && data.logicalEditCaptureEnabled(),
                            "failed mutation is not journaled and capture remains enabled");
}

void testCascadingNodeDeletionIsOneTransaction()
{
    HydraulicData data;
    QList<QByteArray> events;
    QObject::connect(&data, &HydraulicData::signalLogicalEditRecorded,
                     &data, [&events](const QByteArray &event) { events.append(event); });
    data.setLogicalEditCaptureEnabled(true);
    const QUuid first = data.addJunction(coordinate(50.0, 8.0));
    const QUuid second = data.addJunction(coordinate(50.1, 8.1));
    const QUuid pipe = data.addPipe(first, second, {});
    const QUuid point = data.addDemandPoint(coordinate(50.02, 8.02));
    test_harness.expectTrue(data.attachDemandPointToPipe(point, pipe, 0.5),
                            "demand point attaches to soon-deleted pipe");
    const int before_delete = events.size();
    test_harness.expectTrue(data.deleteJunction(first), "delete junction succeeds");
    test_harness.expectTrue(events.size() == before_delete + 1,
                            "cascading node deletion creates one logical undo step");
    if (events.size() != before_delete + 1)
        return;
    const std::optional<aowis::model::ModelEditTransaction> tx = decoded(events.last());
    if (!tx.has_value())
        return;
    test_harness.expectTrue(tx->edits.size() == 3,
                            "node, connected pipe and attachment are one atomic event");
    bool found_node = false;
    bool found_pipe = false;
    bool found_point = false;
    for (const aowis::model::ModelEntityEdit &edit : tx->edits)
    {
        if (edit.entityId == first)
            found_node = edit.before.has_value() && !edit.after.has_value();
        if (edit.entityId == pipe)
            found_pipe = edit.before.has_value() && !edit.after.has_value();
        if (edit.entityId == point)
            found_point = edit.before.has_value() && edit.after.has_value();
    }
    test_harness.expectTrue(found_node && found_pipe && found_point,
                            "delete event preserves all reversible before/after images");
}

}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    test_harness.runCase("Only watched entities", testCaptureOnlyTouchedEntities);
    test_harness.runCase("HydraulicData logical operations", testHydraulicDataLogicalOperations);
    test_harness.runCase("Hydraulic property capture and no-op", testPropertyCaptureAndNoop);
    test_harness.runCase("Cascading node deletion", testCascadingNodeDeletionIsOneTransaction);
    return test_harness.finish();
}
