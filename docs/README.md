# GUI documentation

AOWIS-SERVER-GUI is the Qt controller and Projects workspace for AOWIS.

## Projects and DVMDB

| Task | Reference |
|---|---|
| Understand project/branch/commit semantics and the inspector | [Projects workspace](projects-workspace.md), [Git-style graph](project-graph-layout.md) |
| Understand the per-user working repository | [Configuration](working-repository-configuration.md), [DVMDB setup](dvmdb-quickstart.md) |
| Implement real editing and persistence | [Hydraulic logical edits](hydraulic-edit-transactions.md), [Save/recovery contract](personal-working-copy-save.md) |
| Continue implementation in order | [Integration roadmap and current status](dvmdb-integration-roadmap.md) |
| Understand the revision/native/REST representation | [UUIDv7 integration contract](dvmdb-uuidv7-gui-readiness.md) |

The GUI can browse real DVMDB sources, render revision history, capture
supported MODEL entity edits and maintain a durable local pending-event
queue. **The complete Projects-tab autosave/Commit experience is still under
implementation.** No document should imply that choosing a source and
editing an example automatically persists a model.

## Other GUI subsystems

- [Map workspace architecture](map-workspace-architecture.md) covers maps,
  rendering ownership and map-client interaction.
- [Numeric editor transactions](numeric-editor-transactions.md),
  [numeric formatting](numeric-formatting.md) and
  [unit presentation coverage](unit-gui-coverage-matrix.md) document
  measurement input and unit conversions.
- [Apply patches](apply-patches.md) documents patch/source ZIP tooling.

Security is not part of the Projects/DVMDB implementation roadmap; see
[deferred security considerations](deferred-security-considerations.md) for
unscheduled design ideas. AOWIS is pre-first-release: unsupported historical
database layouts may be rejected cleanly, without compatibility or migration
code.
