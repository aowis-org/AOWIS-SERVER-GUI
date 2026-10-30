# Hydraulic editor logical transactions

The shared canonical entity codec and `ModelEditTransactionCodec` live in
AOWIS-SERVER-MODEL. `HydraulicEditTransactionCapture` is the GUI adapter from
real hydraulic editor operations to the MODEL format. It watches only affected
UUIDs and keeps before/after canonical bytes. It does **not** snapshot the whole
network, and it does not write to a database.

## Current boundary

`HydraulicData::setLogicalEditCaptureEnabled()` defaults to **false** and resets
to false when the network is replaced. The caller must not enable it for general
editing until every mutation path has complete coverage, a durable local queue,
and a working-copy recovery procedure. Until then it is a regression/testing
hook, not proof that the editor is connected to DVMDB.

Entity creation (junction/reservoir/tank, demand point, pipe/pump/valve),
property changes on hydraulic nodes/links/demand points, pump controls, demand
allocations, geometry changes, pipe split/rejoin, and entity deletion now emit
reversible events. A scoped capture mechanism watches the affected entity before
each property mutation and compares its canonical bytes afterward. Node deletion
also captures the cascading removal of connected links and cleared demand-point
attachments in **one** event. Pipe split/rejoin and geometry batches keep their
explicit multi-entity capture boundaries. No-op and failed edits are not journaled. A successful operation emits `signalLogicalEditRecorded` with exactly
one encoded reversible MODEL transaction; a no-op or failed operation emits
nothing. A capture failure emits `signalLogicalEditCaptureFailed` and disables
capture. Signals themselves are **not** a persistence acknowledgment.

This remains **capture-only by default**: no draft is bound to the live editor.
The durable pending queue and atomic DVMDB event-save contract now exist, but
neither is automatically connected to general GUI editing. Network-wide hydraulic options (including headloss
formula), bulk imports, external mutation paths, and other persistent model
objects still require explicit journal coverage. Do not silently treat an
uncovered mutation as saved, or enable capture as a general production autosave
feature until those boundaries are closed.

## Next integration

1. Bind a personal working-copy UUID and pin its base revision.
2. Complete transaction capture for every mutation path and support deliberate
   group boundaries for dragging and multi-entity commands.
3. Persist the transaction journal and materialized candidate atomically through
   DVMDB. The local pending queue must survive a GUI crash, preserving the same
   UUIDv7 transaction IDs across retries.
4. Use generation checks, fencing, and precise acknowledgment/reopen recovery.
5. Add durable undo/redo cursor and replay, then expose the controls.
6. Commit only after the complete pending queue is durably acknowledged.

Commit is not Save, and Save is not Publish. No backward-format compatibility or
security system is part of this roadmap.

## Local pending-event durability

`PersonalEditQueue` now provides a crash-recoverable per-repository/per-draft
**pending-event spool** (under the GUI application's local data directory).
Each complete, validated MODEL transaction is atomically committed as its own
binary file with sequence ordering and SHA-256 verification; no ZIP or JSON is
used for entity/event data. A process lock prevents two GUI instances from
writing the same draft queue concurrently. After a restart, `pending()` returns
the original UUIDv7 transaction IDs and exact bytes in application order.
Corrupt events block opening the queue rather than disappearing silently.
Duplicate transaction IDs with identical bytes are accepted idempotently;
conflicting IDs fail. The queue is bounded (128 MiB / 100,000 pending
transactions); on exhaustion editing must pause rather than silently discard
operations. The oldest pending transaction must be acknowledged first. Distinct repository/draft UUIDs have independent queues.

The spool is intentionally **not** the server's undo history. Acknowledging an
item deletes its local pending file and must happen **only after the personal
DVMDB atomically commits both the event and its candidate-model changes**.
The old DVMDB `saveChangesRecovering()` overlay-only acknowledgement does
not justify dropping a transaction. The new `save-events` endpoint stores
the MODEL journal and candidate overlay atomically; the GUI now supports
`saveEventsRecovering()` and `flushOldestQueuedEvent()` with an exact journal
ID/byte comparison, generation check and candidate-record comparison. Events
are removed from the spool only after that contract is verified. A dropped
HTTP response, or a GUI restart before local queue removal, is resolved by
reading the authoritative journal rather than blindly replaying the event. Do not enable capture globally yet: network-wide settings,
bulk imports, and external model mutation paths remain uncovered. The default
is still disabled, and no currently shipped UI action binds the spool to a
working copy. The remaining editor integration must bind a verified private draft, hydrate
its pinned base plus candidate overlay into HydraulicData, and convert the
affected MODEL entity images to *complete typed model records* (including
canonical kinds, dependency references and network-membership records).
`flushOldestQueuedEvent()` deliberately takes that model-record batch from
the caller; it checks that every edited entity's after-image matches but
cannot infer DVMDB's internal membership chunks. It is not a general-purpose
model serializer. Do not enable general editing merely because this transport
passes its regression tests. If spool persistence fails, pause the editor
and report an unsaved error; never silently resume normal editing.

The spool is for pending changes only. Confirmed undo/redo history will live
in the personal DVMDB's durable journal with its history cursor. Transport
batch compression can be introduced later if measurements support it.

## Canonical entity-event to DVMDB record mapping (VC-04)

`HydraulicDraftEventAdapter::prepare()` now accepts one MODEL transaction and
an explicit **staged** hydraulic network at the state immediately before that
event. It verifies the exact canonical before-image, decodes/re-encodes each
after-image, verifies the resulting network's typed dependencies using MODEL's
validator, and produces DVMDB `PersonalWorkingCopyChange` records with the
current numeric storage kinds and sorted/deduplicated member references.
Only after complete validation does it advance the staged network and replace
the caller's proposed record batch. A failed or out-of-order edit cannot
partially advance the staged model. This preserves original event identities
and prepares real typed payloads for `flushOldestQueuedEvent()`.

**Do not pass the editor's latest in-memory network to replay an older queued
event.** Replay starts from the pinned private committed base plus its already
acknowledged draft overlay, in event order. Each successful event advances a
separate staged state; server acknowledgements still decide whether queued
files can be deleted. The adapter does not itself acquire a lease or perform
HTTP operations.

The adapter now has an explicit `prepareWithMembership()` API. It validates
entity creation and deletion against the staged network, emits canonical typed
insert/update/delete records, and separately reports each membership insertion
or removal with the owning network UUID and the canonical collection ordinal.
Cascading deletion groups its member tombstones and affected dependent records
as one reversible logical operation. All outputs and the staged network advance
only after full MODEL dependency validation.

**DVMDB remains the owner of hydraulic manifest and membership-chunk encoding.**
The GUI must not invent its own physical chunk format. These membership intents
are not yet accepted by `/save-events`; server-side materialization is the next
backend task. The record-only `prepare()` deliberately continues to reject any
membership mutation and leaves its outputs unchanged. Never send entity insert
or delete records alone and mistake them for a complete candidate network.
The overall editor autosave switch remains off until the native DVMDB operation
can atomically update root, chunks, candidate entities and event journal.
Existing-member property and relationship changes are supported. Network-wide
metadata edits are not captured. The current full-network validation/copy is a
correctness-first bridge; profile and replace it with indexed validation for
large models before enabling unrestricted autosave. The UI's global capture
switch deliberately remains off. No Commit is performed.
