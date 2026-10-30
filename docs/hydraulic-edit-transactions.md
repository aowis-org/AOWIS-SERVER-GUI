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
