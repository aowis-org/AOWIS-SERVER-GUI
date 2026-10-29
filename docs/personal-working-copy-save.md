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
