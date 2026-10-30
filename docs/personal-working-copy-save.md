# Personal working-copy Save transport

This is the HTTP transport contract for VC-04 draft editing. It is **not yet**
editor hydration or semantic validation, and it does not create commits.

`src/projects/personal_working_copy_client.h` exposes `acquireLease`,
`saveChanges`, `reopen` and `releaseLease`. Every operation verifies the
configured private DVMDB repository ID and functional role and never falls
back to the selected shared project source.

The editor supplies the UUIDv7 draft ID, an explicit writer-session UUIDv7,
lease duration (1–120 seconds), and a batch of 1–128 typed `ModelRecord`
changes. Model keys and references preserve any canonical non-null UUID
(including legacy model UUIDs); repository, project, branch, draft and
session identities remain UUIDv7. Changes contain a model kind (`uint16`),
references and exact payload bytes, or a tombstone/delete operation. They are
sent using DVMDB's existing `{uuid, kind, references, payloadBase64}` or
`{uuid, delete: true}` model-record wire shape. Model payloads must come from
the **real AOWIS Model codec**, not arbitrary JSON or GUI render snapshots.

A lease returns an opaque fencing token and decimal-string generation.
Every Save supplies the exact session, token and expected generation. On
success the response must identify the same draft and next generation; only
then may the editor mark that generation saved. Duplicate keys, invalid
references and malformed candidate overlays are rejected before presenting
an acknowledgement. Release uses the exact token and session. A failed or
uncertain HTTP Save must **not** be replayed blindly. The new
`saveChangesRecovering()` issues the Save once, then (if the result is not
confirmed) reopens the draft to verify the exact **next generation** and all
proposed candidate records byte-for-byte, including ordered references. Only
this exact match counts as recovered success; a stale/advanced generation,
different payload, missing entry or failed reopen leaves the editor's local
changes unsaved and requires explicit conflict handling. A recovery read does
not acquire or extend a lease, and it never sends another write. Lease expiry and stale generations
are not silently overridden. The server stores candidate changes separately
from immutable HEADs and committed revisions.

## Next GUI connection (still VC-04)

The map/network editor currently edits `NetworkHydraulic` in memory. The
remaining bridge must load the pinned committed base **plus** saved candidate
overlay into that model, convert actual model-entity mutations into canonical
typed `ModelRecord` changes using the shared AOWIS Model codec, and bind
Save/autosave and recovery to this transport. Until that bridge is proven,
there must be no generic “Save” button claiming to persist a hydraulic model.
A local Commit is a distinct VC-05 operation.

## Codec integration boundary

`NetworkHydraulic` is the editor's authoritative in-memory representation.
The canonical `ModelCodec<NetworkHydraulic>` and typed per-entity encodings
currently live in AOWIS-SERVER-DVMDB (`model_codec.cpp` and
`hydraulic_network_persistence.cpp`), **not** in the supplied GUI or its
Model headers. The GUI's personal-client transport accepts canonical typed
records but cannot itself convert `NetworkHydraulic` without sharing this
codec implementation. Do not reconstruct network data using ad-hoc JSON or
blindly treat the currently open example network as a checked-out revision.

Next: expose/reuse the existing canonical codec as a shared build dependency
for the GUI's HTTP/Standalone targets, then hydrate a pinned private revision
plus its draft overlay into `HydraulicData`. Only after that can Save/autosave
be hooked to real editor changes. The model network UUID and draft base must
be verified rather than guessed. This is a VC-04 integration prerequisite,
not a security or separate roadmap milestone.

## Atomic MODEL event journal and local queue synchronization (VC-04)

DVMDB's `/save-events` operation atomically publishes a batch of MODEL event
frames and candidate typed model records. The GUI `saveEventsRecovering()`
checks exact IDs, generation and journal count in its small acknowledgement.
On an unknown outcome it reopens the authoritative draft and compares the
ordered journal tail and exact changed candidate records. It never retries
an uncertain write automatically. `flushOldestQueuedEvent()` sends the
oldest `PersonalEditQueue` event together with a caller-provided typed model
batch, verifies the event's after-images against those records, and deletes
the local queued event only after that atomic confirmation. A GUI restart
between DVMDB acceptance and local deletion is reconciled against the last
persisted journal event. Failed or ambiguous confirmation preserves the
queue. This workflow must remain serialized per draft, and the open queue
must outlive the callback.

The next integration is NOT another networking transport. Implement a
canonical editor-to-model-record adapter that builds correct kind IDs,
references and network membership changes for every logical edit, hydrate
the actual checked-out revision with its draft candidate, and bind the GUI
editor session to the durable queue. Global capture remains disabled because
network-wide settings and bulk imports need equivalent coverage. No draft
Save is a Commit or a Publish.
