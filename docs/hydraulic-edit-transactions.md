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

This is still **capture only**, not durability: no draft has been bound, no durable
pending queue exists, and DVMDB cannot yet atomically persist the event journal
with the candidate overlay. Network-wide hydraulic options (including headloss
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
The existing DVMDB `saveChangesRecovering()` overlay-only acknowledgement
cannot justify dropping a transaction, because it does not yet persist the
reversible transaction journal. Keep events locally until that server-side
contract exists. Do not enable capture globally yet: network-wide settings,
bulk imports, and external model mutation paths remain uncovered. The default
is still disabled, and no currently shipped UI action binds the spool to a
working copy. The next integration will bind a verified local draft, restore
its event spool, and add the durable DVMDB journal endpoint and acknowledgements
before enabling general editing. If spool persistence fails, pause the editor
and report an unsaved error; never silently resume normal editing.

The spool is for pending changes only. Confirmed undo/redo history will live
in the personal DVMDB's durable journal with its history cursor. Transport
batch compression can be introduced later if measurements support it.
