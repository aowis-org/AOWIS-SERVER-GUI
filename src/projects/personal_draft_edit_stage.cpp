#include "projects/personal_draft_edit_stage.h"

#include <aowis/model/edit/model_edit_transaction.h>

#include <utility>

namespace
{
bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}
}

bool PersonalDraftEditStage::open(
    const QString &queue_root, const QUuid &repository_id, const QUuid &draft_id,
    const NetworkHydraulic &verified_candidate,
    const QByteArray &last_journal_event, QString *error)
{
    close();
    if (error)
        error->clear();
    if (verified_candidate.uuid.isNull())
        return fail(error, QStringLiteral("A verified hydraulic network UUID is required"));
    if (!this->queue_.open(queue_root, repository_id, draft_id, error))
        return false;

    this->persisted_ = verified_candidate;
    this->visible_ = verified_candidate;

    // Exactly the FIRST local queued event may have reached the authoritative
    // journal before the process crashed. The caller supplies the journal tail
    // from the same validated draft/network, not from a client-side guess.
    if (!last_journal_event.isEmpty() && !this->queue_.pending().isEmpty() &&
        this->queue_.pending().first().encoded_transaction == last_journal_event)
    {
        const QUuid id = this->queue_.pending().first().transaction_id;
        if (!this->queue_.acknowledgePersisted(id, error))
        {
            close();
            return false;
        }
    }

    for (const PersonalEditQueue::Entry &entry : this->queue_.pending())
    {
        QList<PersonalWorkingCopyChange> records;
        QList<HydraulicDraftMembershipEdit> membership;
        QString replay_error;
        if (!HydraulicDraftEventAdapter::prepareWithMembership(
                entry.encoded_transaction, this->visible_, records,
                membership, &replay_error))
        {
            // Never discard an unmatched pending event. Keep the queue files
            // on disk and reject the session until it is reconciled.
            close();
            return fail(error, QStringLiteral("Cannot replay queued transaction %1: %2")
                .arg(entry.transaction_id.toString(QUuid::WithoutBraces), replay_error));
        }
    }
    return true;
}

void PersonalDraftEditStage::close()
{
    this->queue_.close();
    this->persisted_ = NetworkHydraulic{};
    this->visible_ = NetworkHydraulic{};
    this->blocked_ = false;
}

bool PersonalDraftEditStage::append(const QByteArray &encoded_event, QString *error)
{
    if (error)
        error->clear();
    if (!isOpen() || this->blocked_)
        return fail(error, QStringLiteral("Personal draft edit stage is not writable"));
    NetworkHydraulic next = this->visible_;
    QList<PersonalWorkingCopyChange> records;
    QList<HydraulicDraftMembershipEdit> membership;
    if (!HydraulicDraftEventAdapter::prepareWithMembership(
            encoded_event, next, records, membership, error))
        return false;
    if (!this->queue_.append(encoded_event, error))
    {
        // An I/O failure may have happened after the transaction was written
        // to disk. We cannot safely accept further edits in that situation.
        this->blocked_ = true;
        return false;
    }
    this->visible_ = std::move(next);
    return true;
}

bool PersonalDraftEditStage::prepareOldest(
    NetworkHydraulic &next_candidate,
    QList<PersonalWorkingCopyChange> &records,
    QList<HydraulicDraftMembershipEdit> &membership,
    QString *error) const
{
    if (error)
        error->clear();
    if (!isOpen() || this->blocked_ || this->queue_.pending().isEmpty())
        return fail(error, QStringLiteral("No writable personal draft with a queued transaction"));
    NetworkHydraulic next = this->persisted_;
    QList<PersonalWorkingCopyChange> proposed;
    QList<HydraulicDraftMembershipEdit> intents;
    if (!HydraulicDraftEventAdapter::prepareWithMembership(
            this->queue_.pending().first().encoded_transaction,
            next, proposed, intents, error))
        return false;
    next_candidate = std::move(next);
    records = std::move(proposed);
    membership = std::move(intents);
    return true;
}

bool PersonalDraftEditStage::acknowledgeOldest(const QByteArray &confirmed_event, QString *error)
{
    if (error)
        error->clear();
    if (!isOpen() || this->blocked_ || this->queue_.pending().isEmpty())
        return fail(error, QStringLiteral("No queued event to acknowledge"));
    const PersonalEditQueue::Entry first = this->queue_.pending().first();
    if (first.encoded_transaction != confirmed_event)
        return fail(error, QStringLiteral("Server confirmation does not match oldest local event"));
    NetworkHydraulic next;
    QList<PersonalWorkingCopyChange> records;
    QList<HydraulicDraftMembershipEdit> membership;
    if (!prepareOldest(next, records, membership, error))
    {
        this->blocked_ = true;
        return false;
    }
    if (!this->queue_.acknowledgePersisted(first.transaction_id, error))
    {
        this->blocked_ = true;
        return false;
    }
    this->persisted_ = std::move(next);
    return true;
}

bool PersonalDraftEditStage::isOpen() const
{
    return this->queue_.isOpen();
}

bool PersonalDraftEditStage::isBlocked() const
{
    return this->blocked_;
}

int PersonalDraftEditStage::pendingCount() const
{
    return this->queue_.pending().size();
}

const NetworkHydraulic &PersonalDraftEditStage::visibleNetwork() const
{
    return this->visible_;
}

const NetworkHydraulic &PersonalDraftEditStage::persistedNetwork() const
{
    return this->persisted_;
}

const PersonalEditQueue &PersonalDraftEditStage::queue() const
{
    return this->queue_;
}
