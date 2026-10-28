# Per-user DVMDB working repository configuration

**Normative GUI integration design**, coordinated with the
[DVMDB personal repository contract](../../AOWIS-SERVER-DVMDB/docs/private-working-repositories.md),
[Projects workspace](projects-workspace.md), and
[security backlog](../../AOWIS-SERVER-DVMDB/docs/security-permissions-design.md).
The [GUI implementation roadmap](dvmdb-integration-roadmap.md) lists the
backend prerequisites, per-user configuration work, decision gates and tests.
Actual configuration names and widgets are to be implemented; example field
names below are proposed logical keys, not a claim that these keys work today.

## Explicit roles, not interchangeable source profiles

The GUI has one configured **personal working repository** for the active
user. It is the durable owner of their private drafts, local commits,
local branches and remote-tracking data. The navigator may also contain
one or more **shared project sources**: servers/repositories that hold
published project histories. A checked source is not automatically selected
as the personal working repo. Users must be able to inspect a shared source
without giving it custody of their private draft data.

| Mode | Working DVMDB configuration | Behavior |
|---|---|---|
| Standalone | Built-in/native database with configured persistent working repo path | Edit and local commit without HTTP; publish to selected shared source when connected |
| Desktop/basic GUI | **Required** private HTTP DVMDB endpoint and repository identity | Draft saving, checkout and local commit go to that private service |
| Browser/web GUI | **Required** private HTTP DVMDB endpoint and repository identity | No editing with browser memory alone; server-side durable draft recovery |

One configured working repo belongs to one intended user and can serve
multiple of their sessions, projects and devices (subject to later identity
and authorization rules). Each session normally creates or reopens its
**own working-copy UUID**, not the shared committed HEAD or another user's
copy. The GUI must show which working repository currently holds the draft.

## Proposed configuration fields

```ini
# Illustrative schema; NOT recognized configuration keys yet.
[WorkingRepository]
mode=embedded|http
# embedded (Standalone only):
path=/absolute/persistent/user-working-dvmdb
# http (desktop/basic/web):
endpoint=https://private-working-dvmdb.example.invalid
repositoryId=<user-private-repository-uuidv7>
```

Shared project connections remain separate GUI database-source profiles.
Do not put access tokens or passwords in this file. Store credential handles
in an approved secure provider once authentication is implemented.

## Startup and connectivity behavior

1. Resolve the active user's working-repository selection *before* starting
   any editable session. Embedded path must be durable, writable and not a
   transient cache. HTTP endpoint/repository must be explicitly configured,
   reachable and identified as the expected personal repository.
2. A missing/unavailable private repo makes editing and Commit unavailable;
   published history may remain readable from configured shared sources.
   Never silently fall back to shared DVMDB, a different user repo or RAM.
3. On reconnect, offer recovery of saved working-copy UUIDs, pinned bases,
   local unpublished commit histories and pending publications. Preserve
   draft dirty state across navigation and connection changes.
4. Distinguish **Save draft**, **Commit locally** and **Publish to shared**.
   Show local HEAD, remote-tracking HEAD and current shared HEAD explicitly.
5. Publication failure retains committed local revisions and the editable
   draft. Fetch/reconcile operations need user confirmation before changing
   local branch history or working-copy bases.

The private HTTP service being configured does **not** prove that only the
intended user can access it. Until the security phase, this must be enforced
by deployment isolation. Do not expose a shared unauthenticated editing
service and call it a personal working repository.

## GUI acceptance

- Standalone edits and commits against its own embedded personal repo;
  shared database selection does not redirect local drafts.
- Basic and web modes cannot edit until their personal HTTP repo is valid;
  an unavailable repo does not discard draft metadata or silently fallback.
- Two users viewing the same shared R7 have separate working repos and can
  save drafts and locally commit without changing shared R7.
- `Publish` checks the shared HEAD and rejects stale history, leaving the
  private commit and working copy recoverable.
- Working-copy IDs and local/remote tracking refs are never conflated with
  shared branch UUIDs or displayed as ancestry vertices.
