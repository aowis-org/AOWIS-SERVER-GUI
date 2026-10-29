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
uncertain HTTP Save must **not** be replayed blindly: reopen the draft, read
its persisted generation and candidate overlay, compare the proposed changes,
and explicitly recover the editing session. Lease expiry and stale generations
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
