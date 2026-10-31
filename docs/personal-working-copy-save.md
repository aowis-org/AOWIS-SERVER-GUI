# Private draft Save, MODEL journal and recovery

This document describes the **implemented transport components** and the
**remaining editor integration**. A durable Save is not a Commit or Publish.
The relevant code is `src/projects/personal_working_copy_client.*`,
`personal_edit_queue.*`, `personal_draft_edit_stage.*`,
`personal_draft_edit_session.*` and `hydraulic_draft_event_adapter.*`.

## Ownership and targets

The GUI must target the explicitly selected **personal DVMDB**, identified by
repository UUID and functional role `private`; it never writes a draft to the
selected shared browsing server as a fallback. Standalone ultimately uses its
persistent embedded private DVMDB, while ordinary desktop/web GUI uses a
configured private HTTP DVMDB. One draft has an exact project, branch,
revision base (or unborn null), draft UUID, writer session and generation.

DVMDB Save requires the active session, exclusive lease/fencing token and
expected generation. Saving candidate changes or an event batch advances the
draft generation after durable replacement; it does **not** move committed
HEAD. Stale generations and expired fencing tokens fail rather than overwrite
another session's state.

## Canonical bytes and event preparation

**AOWIS-SERVER-MODEL owns the canonical serializer and the reversible
`ModelEditTransaction` codec.** The GUI captures logical `HydraulicData`
operations, stores encoded MODEL events in a durable local queue, and uses
`HydraulicDraftEventAdapter::prepareWithMembership` against a staged network
at the state *before* each event. This produces typed DVMDB model-record
changes and, for creations/deletions, logical membership intents. MODEL's
validation and reference rules remain authoritative.

The GUI must not invent its own manifest/chunk binary format. DVMDB's
`/dvmdb/v1/working-copies/{draftId}/save-events` accepts an optional
`membership` array (`networkId`, `entityId`, `kind`, `collectionIndex`,
`inserted`) and materializes matching server-owned hydraulic membership
chunks atomically with the candidate entity records and original MODEL event
frames. `saveEventsRecovering` and `flushOldestQueuedEvent` take the
adapter's intents and send them as `membership` (omitted for update-only
batches, since DVMDB accepts only a non-empty array). Before any request the
client checks that each created/removed entity has exactly one intent of the
same direction, that each intent describes such an edit, and that the typed
record matches (inserted: same kind and the event's after-image; removed:
tombstone). DVMDB applies the same rule and rejects a creation or removal
without membership with 400. The record-only adapter deliberately refuses
membership mutations.

DVMDB error bodies are `{"error":{"code","message"}}`; the client reports
`message (code)` instead of Qt's generic transport text.

## Durable acknowledgement and retries

`saveEventsRecovering` verifies accepted event IDs, generation and count.
If the HTTP reply is lost, it reopens the authoritative draft and confirms
persisted encoded event bytes and matching candidate records before reporting
success. The queue drains **only after** authoritative confirmation. A draft
reopen also resolves a GUI restart after remote persistence but before local
queue deletion. An unknown/conflicting result preserves the local queue and
must not cause a blind retry with another event identity.

Queued events are processed serially per draft, in original transaction order.
The bridge must retain a correctly staged model from the pinned base plus
previously acknowledged draft changes. Never build an old queued event's
references from today's potentially newer GUI network. `PersonalEditQueue`
instance lifetime must cover asynchronous callbacks; wrong draft/repository
targets are rejected.

## Fenced HTTP editing controller (source-level implementation)

`PersonalDraftEditSession` now provides the long-lived **HTTP-only** writer
boundary. On start it reopens the authoritative draft, loads the canonical
hydraulic candidate, obtains a fenced 90-second lease, checks the generation
and opens the per-repository/per-draft local event queue. Replayed pending
events are staged against that exact verified candidate. The controller then
sends precisely the oldest event and its typed records plus membership intents,
never rebuilding the request from the current GUI model.

The controller renews the lease periodically, **serially** with Saves because
DVMDB rotates fencing tokens on reacquisition; renewal while a Save is in
flight is deferred. A new generation is accepted and the local event retired
only after `saveEventsRecovering` has verified DVMDB's journal and records.
Unconfirmed or conflicting responses block the session, disable further
appends, and preserve local queue files until a new authoritative open. The
caller must disable GUI capture immediately on a blocked signal or failed
append, and must not discard a dirty live model. Explicit stop releases the
current fenced lease; the server expires it if the process crashes.

The HTTP fixture regression covers a lost Save acknowledgement recovered
without a duplicate POST, rejected Save retaining the local queue, reopening
and replaying its event, and eventual lease release. The controller does not
claim **user-facing autosave**: it has not yet been bound to live
`HydraulicData`, and its staged visible model must be compared with the editor
model before enabling capture. The embedded Standalone writer remains a
separate integration step.

## Remaining user-facing integration

The fenced transport controller and corresponding targeted regressions exist, but
**the current Projects-tab hydraulic editor is not yet bound to an end-to-end
autosaved working-copy session**. Remaining work is:
load the pinned base and candidate overlay into `HydraulicData`; attach
edit capture and queue to the actual checked-out session; replay and recover
after restart; verify persisted full-network equality; cover network-wide
edits/bulk imports before allowing them through autosave. Then implement
explicit local Commit with its own expected-HEAD and idempotency contract.

See [hydraulic edit transactions](hydraulic-edit-transactions.md),
[working repository configuration](working-repository-configuration.md), and
[GUI integration roadmap](dvmdb-integration-roadmap.md).
