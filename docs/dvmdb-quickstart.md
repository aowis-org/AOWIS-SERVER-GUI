# Connect the Projects workspace to DVMDB

This is the **implemented** GUI ↔ DVMDB setup, as of 2026-10-26.
It applies to the real data-source integration, not the older in-memory
preview. For future capabilities, see
[the integration roadmap](dvmdb-integration-roadmap.md).

## 1. Start a DVMDB HTTP server (for remote/REST access)

In the **AOWIS-SERVER-DVMDB checkout**, run:

```bash
./compile_linux.sh
```

The script configures/builds the Release server and **executes it in the
foreground** when the build succeeds. It does not require you to pass a
database directory for an existing development checkout. Default listener:
`127.0.0.1:8755`. To stop it, press Ctrl+C in that terminal. To compile only:

```bash
./compile_linux.sh --build-only
```

In a second terminal, check the *real* HTTP endpoint:

```bash
curl -fsS http://127.0.0.1:8755/dvmdb/v1/health
curl -fsS http://127.0.0.1:8755/dvmdb/v1/ready
curl -fsS http://127.0.0.1:8755/dvmdb/v1/projects
```

Expected shapes:

```json
{"status":"ok"}
```

```json
{"status":"ready"}
```

```json
{"projects":[]}
```

The third response is illustrative for a new, empty database; populated
databases return project objects in the array. `health` means a listener is
answering; `ready` checks the catalogue. `curl` failing with connection
refused means no listener is reachable at that address, regardless of whether
the build completed.

### Check which data directory the server opened

The HTTP server prints its actual storage root on startup. Without an
explicit root it chooses, in order:

1. An existing `./aowis-dvmdb-data` relative to the server process working
   directory (the build/start script runs from the DVMDB checkout).
2. Otherwise, `$XDG_DATA_HOME/aowis-dvmdb`, normally
   `~/.local/share/aowis-dvmdb`.

An explicit `--data-root` wins over `[storage] root=` in the INI, and both
win over automatic selection. The example `deploy/aowis-server-dvmdb.ini`
explicitly selects `/var/lib/aowis-dvmdb`: do not use it as if it were
the no-configuration development default.

An empty project list may mean an empty catalogue **or a different selected
root**. Compare the printed root with the expected files before making new
projects. Startup does not move or merge data roots. Never delete an active
`catalogue.lock`. For controlled relocation, follow DVMDB
`docs/operations.md` (offline whole-root backup/restore).

## 2. Configure the GUI

Open **Projects → Databases**. With no sources configured, the workspace is
empty and the inspector is closed. Click the small **+** beside *Databases*
(or **Add database** in the empty state).

For the HTTP daemon:

- Choose **Remote database (REST)**.
- Give it a recognizable name, e.g. `Development DVMDB`.
- Enter `http://127.0.0.1:8755` as the URL; the GUI also accepts an URL
  ending in `/dvmdb/v1` without appending the prefix twice.
- Set authentication mode to **None**.
- Select **Create**. **Cancel** or closing this new-source inspector creates
  no profile.

The profile is stored locally in the native GUI INI; browser builds use
browser `localStorage`. Profiles persist between restarts. For a
fresh desktop setup on Linux, the GUI config is usually at
`~/.local/share/aowis-server-gui/aowis-server-gui.ini`.

**There is no Connect/Disconnect button.** Each configured database starts
automatically. Select its database row to inspect settings, status and
**Refresh catalogue**. Remote health is checked about every **5 seconds**:
one failed probe reports degradation; two consecutive failures report
offline; recovery is automatic. The catalogue refreshes about every
**60 seconds** while healthy and reloads on recovery. A health request
checks `/dvmdb/v1/projects` and requires an actual DVMDB-shaped response,
not merely HTTP 200. The Map Server health monitor uses the same
bounded-check infrastructure, but it is a separate service.

If the server goes offline, previously loaded entries can remain visible as
**stale, read-only catalogue data**. The UI must not present these as an
available writable database. The row checkbox controls whether a source
appears in the explorer and graph; **unchecking does not disconnect** it.
Visibility preferences persist.

## 3. Embedded native DVMDB (Standalone)

A supported Linux **AOWIS-SERVER-Standalone** build with native DVMDB enabled
automatically provisions a protected **Local database** profile. It uses
`$XDG_DATA_HOME/aowis-standalone-dvmdb` (normally
`~/.local/share/aowis-standalone-dvmdb`) and reads it through the C++ library.
There is **no HTTP daemon, URL or liveness ping** involved. The root is
created/opened when the native adapter starts; first-use provisioning of the
profile itself is not a claim that it contains any projects.

The standalone root is **not the same default root** as the HTTP daemon's
`aowis-dvmdb`. Keep those stores separate. Do not point a standalone
embedded source at a directory simultaneously opened by the daemon.
GUI-only and WebAssembly builds without the native backend cannot open a
local embedded source.

## 4. Browse and create

The **Databases** checkboxes control which sources contribute to the
**Projects & Branches** tree and the shared revision graph. Multiple real
databases can be displayed together, even if they have projects with
identical names or revision hashes; the GUI qualifies their internal IDs
by source. Select the target database/project before creating anything:

- **+ Project** creates a *real* DVMDB project in the selected available source.
- **+ Branch** creates a real branch in the selected project; a selected
  historical revision can serve as its fork point.
- Writes are followed by a new catalogue read. They are not optimistic
  fake entries. An ambiguous POST timeout does not automatically trigger a
  second write: inspect the refreshed catalogue before retrying.
- **Refresh catalogue** is on the selected database's inspector.

Currently the GUI reads **up to 100 first-parent revisions per branch**.
This is *not* a full or fully paginated merge DAG. DVMDB project/branch
rename/delete, network listing/checkout, edited-model Save, and revision
annotation writes are **not** available in this GUI milestone. The engine
already has lower-level hydraulic read/write APIs; they are **not yet wired
to a safe GUI working-copy/expected-HEAD session**. In-memory demo
objects and the Example Library remain clearly separate from real DVMDB
persistence. See [Projects workspace](projects-workspace.md) for details.

## 5. Troubleshooting

| Observation | Check |
|---|---|
| DVMDB build succeeded, but GUI shows *Unavailable* | Is `./compile_linux.sh` still running? Does `curl -fsS http://127.0.0.1:8755/dvmdb/v1/ready` work in a separate terminal? |
| Server starts, but old projects are missing | Check the printed storage root. CLI/INI override, the checkout-local legacy root and the per-user root may contain different catalogues. |
| GUI shows *Checking…* or *Connection unstable* | Give the asynchronous checks time to complete; confirm URL, port, and `/dvmdb/v1/projects` JSON rather than relying on a browser landing page. |
| REST profile rejected or never becomes available | Use `http://` or `https://` with a host, no embedded credentials/query/fragment, and **None** authentication. The current server itself only serves plain HTTP. |
| Project list or graph looks incomplete | Check the database visibility checkbox, offline state, configured source, and the 100-first-parent-revision display limit. |
| No *Local database* option | Embedded DVMDB is available only when the Standalone application is compiled with native DVMDB support. |
| An existing catalogue is locked | Stop the other process using that root. Do not delete an active lock or make a concurrent filesystem copy. |
| `+ Project` or `+ Branch` unavailable | Select an available, writable database/project, not a stale offline snapshot; check if another write is in progress. |

## 6. Security and implementation boundaries

The **current DVMDB HTTP server has no authentication, authorization or TLS**.
A configured GUI authentication *label* is not authentication, and entering
credentials does not secure the server. Keep the HTTP listener on trusted
development/loopback networks; do not publish it to the Internet. If a reverse
proxy terminates TLS, that does not magically add DVMDB authorization.

The GUI's automatic source monitoring does not synchronize independent
databases. Visibility does not transfer branches or models. Full history
paging, compare, checkout/save with expected-HEAD protection, lifecycle
rename/delete and bundle push/pull remain future integration work. The actual
HTTP operations are defined by the DVMDB repo's
`docs/dvmdb-http-api.yaml`; do not infer endpoints from planned UI features.
