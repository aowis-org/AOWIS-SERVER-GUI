# Version-control implementation verification

This development verification record is **not** the active starting point or
a release-readiness report. The authoritative product requirements and the
current source-level implementation/status matrix are in the
[GUI roadmap](dvmdb-integration-roadmap.md).

The current acceptance target is practical checkout into a private working
DVMDB, complete hydraulic-model hydration, logical edit capture, durable
queued autosave with atomic membership updates, crash/reconnect recovery,
reopen with exact model equality, and explicit first local Commit from an
unborn branch. Backend/GUI unit tests validate individual pieces but do not
replace this end-to-end scenario.

Testing and operational evidence belongs with the actual subsystems:
[working-copy Save](personal-working-copy-save.md),
[hydraulic logical edits](hydraulic-edit-transactions.md), and
[Projects workspace](projects-workspace.md).

Do not reintroduce backward compatibility or experimental-format migration
code as a prerequisite for the first AOWIS release. Security design is a
separate unscheduled effort.
