# AOWIS-SERVER-GUI (*AOWIS Controller*)

[EPANET](https://github.com/OpenWaterAnalytics/EPANET) based Controller for Water Networks.

This is the GUI for the AOWIS-SERVER infrastucture.

This software is written in C++ with Qt and can be compiled for all major platforms, including WebAssembly.

## Git submodules

Clone the repository with its dependencies, or initialize them after cloning:

```bash
git clone https://github.com/aowis-org/AOWIS-SERVER-GUI.git
cd AOWIS-SERVER-GUI
./git_submodule_init.sh
```

`git_submodule_init.sh` initializes **all** submodules recursively, including
AOWIS-SERVER-EPANET, AOWIS-SERVER-MODEL, GeographicLib, and the bgfx rendering
libraries (`bgfx`, `bx`, and `bimg`) plus the upstream `bgfx.cmake` build scripts. For normal clones it checks out the exact
revisions recorded by the AOWIS-SERVER-GUI repository; it does not update them
to their latest upstream commits.

To **update** submodules to their remote-tracking branches, use:

```bash
./git_submodule_update.sh          # AOWIS submodules and GeographicLib only
./git_submodule_update.sh --bgfx   # bgfx, bx, bimg and bgfx.cmake only
./git_submodule_update.sh --all    # Both groups
./git_submodule_update.sh --help   # Usage
```

**First-time setup of the bgfx patch:** ZIP files cannot contain Git submodule
gitlinks. If the four bgfx-group entries exist in `.gitmodules` but have not yet
been registered in Git, either `git_submodule_init.sh` or
`git_submodule_update.sh --bgfx` will register them automatically. Review and
commit the new `external/bgfx`, `external/bx`, `external/bimg`, and `external/bgfx.cmake` gitlinks
alongside `.gitmodules`, so subsequent clones can initialize all dependencies
normally. This initial registration fetches the bgfx repositories; future
`git_submodule_init.sh` runs use the committed revisions. The scripts deliberately
skip the nested `bgfx.cmake` submodules (otherwise bgfx, bx and bimg would be
cloned twice).

The bgfx submodules are only built when the experimental bgfx probe is enabled.
The main application still uses its existing QRhi, browser WebGL, or CPU backend.

### bgfx surface and stacking proof of concept

An isolated probe, **not yet a map backend**, tests native bgfx surface
integration and the browser rule that a bgfx canvas must be above the normal
Qt canvas but below Qt top-level dialogs. No QWidget is painted above bgfx.
The probe uses bgfx's GPU debug-text HUD, rather than a QWidget overlay.

```bash
# Native: configure your usual GUI build with this additional flag:
cmake -S . -B build-linux -DAOWIS_BUILD_BGFX_PROBE=ON
cmake --build build-linux --target aowis-bgfx-probe
./build-linux/tests/bgfx_probe/aowis-bgfx-probe

# WASM: opt in (the Docker build uses this environment variable):
AOWIS_BUILD_BGFX_PROBE=ON ./compile_wasm.sh
# Serve build-wasm-dist/bgfx-probe.html using the same COOP/COEP headers
# as the main app. Run it manually in a browser for the GPU stacking test.
```

In the probe, click **Open Qt dialog** while watching the continuously updated
GPU text/frame counter. The dialog should remain visible above the bgfx canvas.
Close the dialog and watch timing logs printed to the browser console (JSON
records, 120-frame intervals, separated by `dialog_open` true/false). Test at your usual map viewport size and on
multiple GPUs. This is a *measurement harness*, not yet an automated
pass/fail GPU throughput benchmark: frame timings need a verified baseline and
normal browser load controls before a hard threshold can be established. In the
browser the JSON report additionally contains `stacking_ok`, which checks the
**actual live Qt windows**, not just the synthetic DOM test. It must remain
`true` both with the dialog closed and open; any `false` value is a failure.

For a repeatable **relative WASM regression check**, capture the browser console
output with at least three 120-frame reports for each of `dialog_open=false`
and `dialog_open=true` **at the same viewport dimensions**, then run:

```bash
node tests/bgfx_probe/performance_check.js browser-console.log
# Optional: set maximum tolerated median FPS loss (default: 10 percent)
node tests/bgfx_probe/performance_check.js --max-loss-pct 10 browser-console.log
```

The checker compares median FPS at one common viewport size and requires
`stacking_ok=true` in **every compared browser sample**. It returns exit code 0
only when FPS and live stacking both pass, 1 for a measured regression or a
reported stacking failure, and 2 when results are inconclusive (including
missing `stacking_ok`). Run both dialog scenarios without resizing between
them; resize/warm-up samples can be outliers.

To compare older, FPS-only console logs **without treating them as a successful
stacking test**, explicitly use:

```bash
node tests/bgfx_probe/performance_check.js --allow-unverified-stacking browser-console.log
```

If your browser still logs no `stacking_ok` although the current
`tests/bgfx_probe/main.cpp` contains it, the browser is probably running an
older JS/WASM bundle. Rebuild with `AOWIS_BUILD_BGFX_PROBE=ON`, serve the freshly
generated `build-wasm-dist/bgfx-probe.html` and assets, and hard-reload the page.
Ensure your web server actually points at that updated directory. A previous
HTML/JS/WASM deployment cannot fulfill the live stacking assertion.

The 16 ms probe timer caps observed FPS near 62.5, so this test can catch major
composition regressions but **does not measure peak GPU throughput** or prove
performance parity with AOWIS's eventual full map workload.

The WebAssembly probe runs in a dedicated HTML page to avoid interference with
existing browser renderers. The actual map isn't migrated by this option.
The official `bgfx.cmake` port describes its Emscripten build as experimental;
compiler or browser integration failures must be resolved before promoting
bgfx to a default backend. The native Wayland path uses Qt GuiPrivate's
platform-native window resource API and therefore requires that component.


## Build

**Compiler requirement:** C++20 (a compiler with C++20 support, for example GCC 12 or newer on Debian 12+). The GUI, its tests, and the AOWIS Model/EPANET components use C++20. CMake enforces this requirement across native and WebAssembly builds.

### Linux
Run the script `compile_linux.sh`

Find the result in the folder `build-linux`

### WebAssembly
The WebAssembly compiler is fully self-contained in an AOWIS-owned Docker image. It does not depend on a prebuilt third-party Qt/Emscripten image.

`compile_wasm.sh` uses the pinned versions from `tools/qt-emscripten/toolchain_versions.sh`. On the first build it automatically runs `prepare_wasm_docker.sh`, which starts from Ubuntu, installs the matching Emscripten SDK, builds same-version Qt host tools, and builds Qt for WebAssembly with thread support (`-feature-thread`). The image also performs a small Qt Widgets/Network/QThread smoke build before it is accepted.

The default toolchain is Qt 6.10.2 with Emscripten 4.0.7. Re-run `prepare_wasm_docker.sh` manually whenever the Docker definition or pinned toolchain versions change. Building the image compiles Qt from source and is therefore a one-time heavyweight operation; normal `compile_wasm.sh` runs reuse the local Docker image.

Your system user needs to be allowed to run docker, or run the script as root.

Adding your system user to the group `docker`, and log out and in again should be enough:
```
usermod -aG docker [USERNAME]
```

Find the result in the folder `build-wasm`.

To run the result, you need to have a web server set up. Threaded WebAssembly additionally requires a secure context and the `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` response headers. The generated webroot includes an `.htaccess` with these headers for Apache deployments that allow overrides.

### Windows / macOS
Usually as a Desktop-Client, you might want to prefer [AOWIS-SERVER-Standalone](https://github.com/aowis-org/AOWIS-SERVER-Standalone).


## GUI configuration

Native builds create the configuration file automatically on first startup. The default location is platform-specific:

```text
Windows: %APPDATA%\aowis-server-gui\aowis-server-gui.ini
Linux:   ~/.local/share/aowis-server-gui/aowis-server-gui.ini
macOS:   ~/Library/Application Support/aowis-server-gui/aowis-server-gui.ini
```

On Windows, `%APPDATA%` normally resolves to `C:\Users\<user>\AppData\Roaming`. Existing configurations from the previous `AppData\Local` location are migrated automatically when the roaming file does not exist. On Linux, `XDG_DATA_HOME` is respected when it is set. The default file contents are:

```ini
[gui]
examples_builtin_enable=true
map_desktop_renderer=rhi

[shortcuts]
sidebar_toggle=Win+Tab
fullscreen=F11
simulation_run=Ctrl+R
simulation_run_alternate=Shift+Enter
map_zoom_in=E
map_zoom_out=Q
map_pan_up=W
map_pan_down=S
map_pan_left=A
map_pan_right=D
map_provider_arcgis_sat=F1
map_provider_openstreetmap=F2
map_provider_opentopomap=F3
map_provider_cycloosm=F4
map_editor_select=Esc
map_editor_delete=Del
map_editor_add_pipe=1
map_editor_add_junction=2
map_editor_add_valve=3
map_editor_add_demand_point=4
map_editor_add_pump=5
map_editor_add_tank=6
map_editor_add_power_source=7
map_editor_add_reservoir=8
map_editor_add_note=9

[map_server]
base_url=http://aowis-server-map.localhost:80
api_key=
delete_api_key=
```

Restart the application after editing the file. Missing native `[shortcuts]` entries are added automatically using the advertised UI shortcuts.

Set `examples_builtin_enable=false` to hide the bundled `Examples` project and its revisions from the toolbar. If the option is missing, built-in examples are enabled.

Native builds accept `map_desktop_renderer=cpu` or `map_desktop_renderer=rhi`. `rhi` is the default and requests the QRhi backend when the build provides the required Qt support; `cpu` keeps the QPainter fallback. If RHI initialization fails at runtime, the existing CPU renderer is promoted automatically.

WebAssembly builds accept `map_wasm_renderer=rhi` or `map_wasm_renderer=browser`. `rhi` is the default for Map Monitor and uses the same QRhi renderer through WebGL 2. `browser` keeps the existing JavaScript/WebGL renderer. If QRhi initialization fails in the browser, Map Monitor automatically falls back to the browser renderer. Map Editor remains on the existing browser renderer during this first WASM runtime-validation stage.

WebAssembly builds include `aowis-server-gui.ini` beside the generated `index.html`. Set `examples_builtin_enable`, `map_wasm_renderer`, `base_url`, `api_key`, and `delete_api_key` there and reload the page. The browser first loads the configuration from the same deployment directory as `index.html`, so subdirectory deployments work correctly; `/aowis-server-gui.ini` at the origin root remains a compatibility fallback. Existing administrator-edited files in `build-wasm` and `build-wasm-dist` are preserved across rebuilds. The normal API key is used for tile requests; the delete API key is used only for tile-cache deletion requests.
