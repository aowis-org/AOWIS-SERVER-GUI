# Working with DVMDB in the Projects workspace

This page separates **what today's components support** from the intended
full editing workflow. It is not a promise that local Commit or hydraulic
autosave is already exposed in the GUI.

## 1. Start DVMDB

In the DVMDB repository, run `./compile_linux.sh` to build and start the
HTTP server after a successful build. The default address is
`127.0.0.1:8755`; test it from a second terminal:

```bash
curl -fsS http://127.0.0.1:8755/dvmdb/v1/ready
curl -fsS http://127.0.0.1:8755/dvmdb/v1/repository
curl -fsS http://127.0.0.1:8755/dvmdb/v1/projects
```

The repository endpoint identifies the server repository's UUID and functional
role. A shared browsing source and a **personal working repository** are
separate destinations even if their project/branch UUIDs match.

## 2. Browse a source

Open the GUI **Projects** tab and configure a DVMDB source in the Servers
section/Inspector. Source visibility controls which project and branch
histories appear in the navigator and graph; it does not make a source the
private editor target. Refresh after external changes. Project creation
produces an unborn `main` with zero revisions—no synthetic first commit.
The branch inspector can identify the exact committed fork revision and
source branch; those are provenance, not additional graph ancestry edges.

## 3. Configure the personal repository

Standalone chooses a separate durable per-user embedded DVMDB root where
supported. Ordinary desktop and WebAssembly GUI require an explicit private
HTTP DVMDB endpoint and exact repository UUIDv7 in the personal-repository
setting. The profile is independent of selected shared sources.
See [working repository configuration](working-repository-configuration.md).
If that endpoint is missing, unavailable or mismatched, editing must be
disabled; there is no silent shared-source or RAM-only fallback.

The GUI contains a committed-branch checkout client for REST sources and a
durable working-copy HTTP transport. These are **not** the same as a complete
checkout into an active `HydraulicData` editor. Merely selecting a revision
or showing a working-copy dialog must not imply the model has been hydrated
from its pinned base and draft candidate.

## 4. Editing lifecycle to finish

1. Explicitly check out an exact committed revision or unborn `main` into a
   personal working repository; then create/reopen the private working-copy
   draft and acquire its writer lease.
2. Load the exact base plus saved candidate into `HydraulicData`, capture
   supported logical MODEL edits and durably queue them locally.
3. Save the canonical event and typed entity changes (and DVMDB-owned
   membership intents for creation/deletion) using expected generation and
   fencing token. Never clear the queue before authoritative confirmation.
4. Reopen after crash/disconnect and verify the full hydraulic network equals
   the saved candidate. A Save must not move any committed HEAD.
5. **Commit locally** from the validated saved draft to the personal branch
   with exact expected local HEAD/null. This creates one immutable revision;
   the shared branch remains unchanged until explicit Publish.
6. Later Fetch, Publish and manual reconcile/merge will move independent
   shared branch refs under expected-HEAD checks. No force-publish or silent
   conflict overwrite.

**Current delivery focus:** steps 1–4 end to end, followed immediately by
local Commit. Refer to the [roadmap](dvmdb-integration-roadmap.md) for tested
subcomponents and still-open integration gates. Do not use this checklist as
an assertion that the workflow is already implemented.

## 5. Troubleshooting and safety

- Check the HTTP repository UUID and functional role, not only its URL.
- Check the selected draft, branch UUID, pinned base and saved generation.
- A lost Save response can mean the write succeeded: reopen and compare exact
  event bytes and candidate records before deleting a queued event.
- A stale lease or generation is a conflict; never silently take over another
  session's draft or switch to a different database.
- The HTTP server currently has no enforced authentication. Keep writable
  development endpoints isolated; security design is separate from this work.
