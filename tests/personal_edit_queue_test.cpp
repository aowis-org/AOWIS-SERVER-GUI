#include "projects/personal_edit_queue.h"
#include "test_harness.h"

#include <aowis/model/edit/model_edit_transaction.h>
#include <aowis/model/uuid.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace
{
AowisTestHarness harness;

QByteArray transaction(const QUuid &id, const QByteArray &payload)
{
    aowis::model::ModelEditTransaction edit;
    edit.transactionId = id;
    edit.operation = QStringLiteral("hydraulic.junction.rename");
    aowis::model::ModelEntityEdit entity;
    entity.entityId = createUuidV7();
    entity.entityType = QStringLiteral("HydraulicNodeJunction");
    entity.before = QByteArray("previous");
    entity.after = payload;
    edit.edits.append(entity);
    return aowis::model::ModelEditTransactionCodec::encode(edit);
}

void testRoundTripAndIdempotency()
{
    QTemporaryDir root;
    harness.expectTrue(root.isValid(), "temporary local queue storage exists");
    const QUuid repository = createUuidV7();
    const QUuid draft = createUuidV7();
    const QUuid first = createUuidV7();
    const QUuid second = createUuidV7();
    const QByteArray first_bytes = transaction(first, QByteArray("changed-one"));
    const QByteArray second_bytes = transaction(second, QByteArray("changed-two"));
    PersonalEditQueue queue;
    QString error;
    harness.expectTrue(queue.open(root.path(), repository, draft, &error),
                       "open personal draft queue");
    harness.expectTrue(queue.append(first_bytes, &error), "first edit written durably");
    harness.expectTrue(queue.append(first_bytes, &error), "duplicate UUID and identical bytes are idempotent");
    harness.expectTrue(queue.pending().size() == 1, "duplicate does not create second queue item");
    harness.expectTrue(queue.append(second_bytes, &error), "second edit written durably");
    harness.expectTrue(queue.pending().size() == 2 &&
                       queue.pending().first().sequence < queue.pending().last().sequence,
                       "editing order is preserved");
    const QByteArray conflict = transaction(first, QByteArray("different"));
    harness.expectTrue(!queue.append(conflict, &error) && !error.isEmpty(),
                       "conflicting reuse of transaction UUID is refused");
    PersonalEditQueue concurrent;
    harness.expectTrue(!concurrent.open(root.path(), repository, draft, &error),
                       "second writer cannot open same draft queue concurrently");
    queue.close();

    PersonalEditQueue recovered;
    harness.expectTrue(recovered.open(root.path(), repository, draft, &error),
                       "reopen pending events after simulated GUI restart");
    const QList<PersonalEditQueue::Entry> pending = recovered.pending();
    harness.expectTrue(pending.size() == 2 && pending.at(0).transaction_id == first &&
                       pending.at(1).transaction_id == second &&
                       pending.at(0).encoded_transaction == first_bytes &&
                       pending.at(1).encoded_transaction == second_bytes,
                       "restart preserves exact logical events and their sequence");
    harness.expectTrue(!recovered.acknowledgePersisted(second, &error),
                       "out-of-order server acknowledgements cannot discard causal predecessors");
    harness.expectTrue(recovered.acknowledgePersisted(first, &error),
                       "explicit durable server-journal acknowledgement removes first event");
    harness.expectTrue(recovered.pending().size() == 1 &&
                       recovered.pending().first().transaction_id == second,
                       "acknowledgment never drops other pending events");
    recovered.close();
    PersonalEditQueue reopened;
    harness.expectTrue(reopened.open(root.path(), repository, draft, &error) &&
                       reopened.pending().size() == 1 &&
                       reopened.pending().first().transaction_id == second,
                       "pending edit remains after acknowledged edit was removed");
}

void testDraftIsolationAndInvalidData()
{
    QTemporaryDir root;
    const QUuid repository = createUuidV7();
    const QUuid draft_a = createUuidV7();
    const QUuid draft_b = createUuidV7();
    const QUuid edit_id = createUuidV7();
    const QByteArray encoded = transaction(edit_id, QByteArray("meaningful-change"));
    QString error;
    PersonalEditQueue first;
    PersonalEditQueue second;
    harness.expectTrue(first.open(root.path(), repository, draft_a, &error), "first draft opened");
    harness.expectTrue(second.open(root.path(), repository, draft_b, &error), "different draft opened");
    harness.expectTrue(first.append(encoded, &error), "first draft has an edit");
    harness.expectTrue(second.pending().isEmpty(), "another draft never sees first draft's edits");
    harness.expectTrue(!second.append(QByteArray("invalid"), &error),
                       "malformed MODEL event is rejected before writing");
    harness.expectTrue(second.pending().isEmpty(), "invalid event did not modify the queue");
    const QString directory = first.directoryPath();
    first.close();
    second.close();

    const QStringList names = QDir(directory).entryList(QStringList{QStringLiteral("*.txn")}, QDir::Files);
    harness.expectTrue(names.size() == 1, "one framed event exists on disk");
    if (names.size() != 1)
        return;
    const QString unfinished_path = QDir(directory).filePath(
        QStringLiteral(".") + names.first() + QStringLiteral(".ABC123"));
    QFile unfinished(unfinished_path);
    harness.expectTrue(unfinished.open(QIODevice::WriteOnly), "create abandoned atomic-save temporary file");
    if (unfinished.isOpen())
    {
        unfinished.write("partial", 7);
        unfinished.close();
    }
    PersonalEditQueue after_crash;
    harness.expectTrue(after_crash.open(root.path(), repository, draft_a, &error) &&
                       after_crash.pending().size() == 1,
                       "abandoned uncommitted temporary file cannot hide the durable event");
    after_crash.close();
    QFile file(QDir(directory).filePath(names.first()));
    harness.expectTrue(file.open(QIODevice::ReadWrite), "open event for corruption test");
    if (!file.isOpen())
        return;
    QByteArray bytes = file.readAll();
    bytes[bytes.size() - 1] = static_cast<char>(bytes.at(bytes.size() - 1) ^ 0x1);
    file.resize(0);
    file.write(bytes);
    file.close();

    PersonalEditQueue corrupted;
    harness.expectTrue(!corrupted.open(root.path(), repository, draft_a, &error) &&
                       !error.isEmpty(),
                       "damaged pending event blocks recovery rather than disappearing silently");
    harness.expectTrue(QFile::exists(file.fileName()),
                       "corrupt event retained for diagnosis/recovery");
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    harness.runCase("durable-idempotent-pending-events", testRoundTripAndIdempotency);
    harness.runCase("draft-isolation-and-corruption", testDraftIsolationAndInvalidData);
    return harness.finish();
}
