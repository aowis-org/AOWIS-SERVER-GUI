# Hydraulic edit transactions

## Product contract

One user operation in `HydraulicData` creates a **logical, reversible** MODEL
`ModelEditTransaction`. An operation may affect multiple typed entities, for
example deleting a junction together with its connected pipes and dependent
attachments. Its before/after images and UUIDv7 transaction identity are
preserved in the durable journal. The event groups are designed for future
undo/redo, but **a complete undo/redo UI and history engine are not yet
implemented**.

AOWIS-SERVER-MODEL owns the canonical binary codec, schema and validation.
DVMDB owns immutable repository storage and the native hydraulic membership
manifest/chunk implementation; the GUI owns interactive edit capture,
`HydraulicData`, offline event queue, and communication to the private DVMDB.
Do not add JSON snapshots or a duplicate GUI serializer.

## Current implementation

- `HydraulicData` captures supported entity edits as MODEL transaction frames.
  Capture is deliberately not enabled globally while network-wide settings,
  bulk import and all mutation paths lack equivalent support.
- `PersonalEditQueue` durably spools transactions by private repository and
  draft. Queue removal is conditional on exact DVMDB acknowledgement.
- `HydraulicDraftEventAdapter::prepareWithMembership` checks canonical
  before/after state, builds kind-typed model records, validates the staged
  network, derives sorted UUID references, and emits membership intents for
  creation/deletion. A failed event leaves caller data unchanged.
- DVMDB's `/save-events` can atomically materialize those membership intents
  in existing hydraulic networks with the model changes and event journal.
  The GUI's event-save client does **not yet forward** the membership array.
- The existing record-only `prepare()` rejects creation/deletion so that an
  incomplete candidate cannot be presented as a successful saved model.

## Required editor synchronization

1. Checkout a known immutable branch base (or unborn branch) into a personal
   working repository; create/reopen the precise draft and acquire its lease.
2. Hydrate `HydraulicData` from the pinned base plus the draft's candidate
   overlay. Verify network UUID, canonical records, dependencies and generation.
3. Enable capture only for supported edit paths. Immediately persist complete
   logical transactions to the local queue before treating them as recoverable.
4. Replay oldest queued events in order against a **staged** network, never the
   latest editor state. Materialize records and membership intents atomically.
5. Submit using expected generation and lease fencing. Accept success only
   after authoritative acknowledgement/reopen; otherwise retain the queue.
6. On restart/offline recovery, reconcile already-accepted journal frames and
   reconstruct the same staged model without duplicates or lost edits.
7. Expose saved/unsaved/conflicted state in Projects. Validate the complete
   candidate before separate **Commit locally**; Save must never commit.

## Acceptance evidence still required

Prove create/update/cascading delete, multiple queued operations, reconnect,
dropped acknowledgement, process crash, stale lease, wrong draft target, and
reopen of a complete hydraulic network. Test actual GUI wiring to a real
private DVMDB, not only an in-process codec or mocked HTTP endpoint. Preserve
transaction grouping for eventual undo/redo and do not enable unsupported
mutation paths through a misleading generic autosave switch.

Related documents: [Save and recovery](personal-working-copy-save.md),
[Projects workspace](projects-workspace.md), and the
[implementation roadmap](dvmdb-integration-roadmap.md).
