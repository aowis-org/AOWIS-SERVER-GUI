# DVMDB integration — implementation status and next steps

**Updated 2026-10-27.** This document describes code in this GUI checkout.
Do not confuse an implemented backend API, a wired GUI feature, and a
proposed feature: they are separate milestones. See the
[practical quickstart](dvmdb-quickstart.md) and
[Projects workspace guide](projects-workspace.md).

## Current capabilities

| Capability | GUI state | Important boundary |
|---|---|---|
| Database profile CRUD | **Implemented** | Stored locally in GUI configuration; profile deletion does **not** delete database data. The default embedded profile is protected. |
| Embedded source in Standalone | **Implemented on supported native builds** | Uses DVMDB `ProjectService`, not a second database implementation or an HTTP server. |
| REST catalogue reader | **Implemented** | HTTP `/dvmdb/v1` project and paginated full-DAG reads with pinned branch HEAD sets, validation, deadlines and cancellation. |
| Automatic remote health and refresh | **Implemented** | ~5 s health probes, degraded/offline after failures, ~60 s catalogue refresh while healthy; offline caches read-only. |
| Multi-source explorer | **Implemented** | Persistent visibility checkboxes; source-qualified project/revision IDs; same-named projects on different databases remain distinguishable. |
| Revision history graph | **P0 integrated** | All-parent graph pages for the entire project, with snapshot-consistent HEADs, duplicate/closure checks and explicit resource limits; QWidget/QPainter, not a second bgfx device. |
| Project creation | **Implemented, native + REST** | Real server-side create, followed by catalogue refresh. |
| Branch creation / fork from revision | **Implemented, native + REST** | Creates a new branch from the chosen supported base. No rename/delete. |
| Network enumeration and checkout | **Not implemented in GUI** | Underlying DVMDB read API for known network IDs exists, but no server network-discovery endpoint. |
| Edited-model Save as a new revision | **Not implemented in GUI** | Need pinned working-copy session and exact expected-HEAD conflict handling. |
| Project/branch rename and delete | **Not implemented by DVMDB service** | Preview-only edits are not remote data writes. |
| REST auth, ACL enforcement, TLS | **Not implemented by DVMDB HTTP server** | Descriptive GUI auth modes cannot authenticate. |
| Pull/push/compare/review/annotations | **Not implemented in GUI** | Requires explicit designs/contracts, not graph gestures with implicit writes. |

## Completed foundations

### Stage 1 — Data-source UX

- `Databases` section with compact add action and useful empty states;
  inspector closed when nothing is selected.
- An unsaved inspector draft for **Create/Cancel**, without a throwaway
  name-only dialog or orphan profiles.
- Local/remote profile types, compatibility for legacy endpoint-only
  profiles, and a protected default local profile in supported Standalone
  builds (separate from the daemon's default root).

### Stage 2 — Shared read-only catalogue

- Native worker and asynchronous REST readers normalize projects, branches,
  pinned branch HEADs and the full revision DAG into the same GUI model.
- Preview content remains distinct from real DVMDB content; real revision
  inspector data is not editable through the old preview controls.
- HTTP responses have size/time/decode checks. Graph pagination and
  uncaptured ancestors must not masquerade as a complete DAG.

### Stage 3a — Real project and branch creation

- Native and REST writers create projects and fork branches, including from
  a selected historical revision.
- Catalogue refresh after completion, no speculative local insertion.
- An uncertain HTTP POST outcome triggers a read instead of a blind retry.
  Conflicts and source-switch races are handled without overriding data.

### Stage 3c — Context-sensitive inspector workflows

- Object identity remains visible above inspector tabs; contextual tabs
  navigate connection properties, projects, branches, reachable revisions,
  and revision ancestry without duplicating a graph or offering fake writes.
- The three-button navigator action row is removed. Project/branch creation
  uses inline forms in the appropriate inspector with explicit base revision,
  duplicate-name checks and cancellation. Native/REST creation semantics and
  authoritative refresh behavior remain unchanged.
- The Network import action is located on the project inspector; import does
  not imply a persisted model revision until P1 checkout/save is implemented.

### Stage 3b — Always-active sources and multi-source explorer

- Connect/Disconnect controls removed: configured sources load automatically.
  Remote health uses the bounded shared Map Server HTTP monitor with response
  validation and degraded/recovered states.
- Independent per-source catalogue readers, stale offline cache handling and
  persistent source visibility checkboxes.
- Aggregated navigator and graph with source-qualified IDs. Embedded local
  DVMDB has no HTTP polling. Explicit Refresh remains in the inspector.

## Next implementation milestones

1. **Network discovery contract.** Define and implement list-networks for an
   immutable project revision (IDs, descriptive metadata, pagination and
   empty-project rules) in both the native service and REST. Extend OpenAPI
   only after the route exists. Avoid choosing an arbitrary network.
2. **Pinned editor session / checkout.** Load one network snapshot at a
   specific revision, retain immutable base and mutable working copy
   separately, track dirty state and prompt on context changes. Tests must
   cover source switching and temporary network loss.
3. **Save with optimistic HEAD.** Use the existing DVMDB network-write
   contract once a safe session exists. Preserve local changes on stale
   expected-HEAD / HTTP 409; never overwrite another client's branch
   update or retry an ambiguous POST automatically.
4. **Lifecycle contracts.** Project/branch rename, deletion/archival, metadata
   changes, validation and concurrency must be implemented by DVMDB first,
   then surfaced through the same inspector.
5. **History/sync workflows.** Build comparison and merge-preview UX on the
   now-integrated full DAG, plus explicit bundle fetch/push/pull decisions,
   optional revision annotations, audit and permission-aware capabilities.

Keep the native and REST backends behaviorally equivalent at the GUI
boundary. The DVMDB backlog is in its `docs/gui-integration-requirements.md`
and the implemented wire contract is `docs/dvmdb-http-api.yaml`. Introducing
SQL metadata, if eventually required, must include equivalent supported
SQLite **and** PostgreSQL queries/migrations; these GUI profiles currently
use Qt settings/JSON, not SQL.

## Tests / acceptance

Run the GUI test suite from the GUI checkout:

```bash
./compile_linux_tests.sh
```

For actual integration, also run the DVMDB server in its own checkout
(`./compile_linux.sh`) and verify its `/dvmdb/v1/ready` and
`/dvmdb/v1/projects` endpoints before assessing GUI failures.

The repository contains GUI regression tests for project/branch REST
creation, HTTP contract/error behavior, multi-source ID collisions and
visibility persistence. Executing them in a build environment is still
required; documentation itself is not proof of successful integration.
