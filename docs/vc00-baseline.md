# VC-00 — GUI integration baseline (2026-10-28)

Source inspection of supplied snapshots only. The DVMDB backend uses **native storage and native transactions**, not a relational database. See the DVMDB repository's `docs/vc00-baseline.md` for the API/format and acceptance matrix.

| GUI component | Current assumption / integration requirement |
|---|---|
| `src/projects/project_source_reader.h` | `SourceBranch::head` is a string; its empty/null semantics are not yet a validated contract |
| `src/projects/project_source_reader.cpp` | `validateCompleteGraph` rejects branch HEADs without UUIDv7 and matching immutable revision hashes; unborn projects cannot pass |
| `src/projects/project_source_writer.*` | Must be rechecked against repository-private draft/Commit/Publish APIs before enabling writes |
| `src/tabs/project_revision_graph.*` | Preserve revision-only graph nodes; show unborn branches without invented commit nodes |
| `src/tabs/tab_projects_widget.*` | Branch controls and empty graph state need acceptance coverage |
| `src/graph/layered_dag_layout.h` | Empty DAG and shared revision ref decorations need regression tests |
| `src/config/project_server_profile_storage.*` | Distinguish native/Standalone and REST transport/capability boundaries |

**Pinned integration contract to establish:** DVMDB VC-01 must expose a genuine UUIDv7 branch ID with `headRevisionId: null` (or an explicitly documented equivalent), empty revision lists, and matching native and REST result semantics. Never map null to a zero UUID or synthetic revision. Branch provenance, local/shared HEADs and private drafts remain later milestones.

**Tests to extend after backend freeze:** CMake targets `aowis-project-source-reader-tests`, `aowis-project-source-writer-tests`, `aowis-project-revision-graph-tests`, and `aowis-layered-dag-layout-tests`. Acceptance cases: unborn project, first commit, shared HEAD decoration, multi-session draft boundaries, reload/restore and cross-repository sync. No tests or builds are claimed as executed by this document.

**Read-only gap inspection:** from the DVMDB checkout run `python3 tools/check_vc00_contract.py --gui-root ../AOWIS-SERVER-GUI`.
