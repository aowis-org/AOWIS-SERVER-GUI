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

`git_submodule_init.sh` initializes all submodules: AOWIS-SERVER-EPANET,
AOWIS-SERVER-MODEL and GeographicLib (recursively), and the bgfx rendering
libraries `bgfx`, `bx` and `bimg`. It checks out the exact revisions recorded
by the AOWIS-SERVER-GUI repository; it does not update them to their latest
upstream commits.

The bgfx libraries are fetched **shallow** (only the pinned commit) and checked
out **sparse** (`tools/bgfx_sparse_checkout.sh`): only the headers and sources
AOWIS builds, ~19 MB instead of ~270 MB. AOWIS builds them with its own small
CMake file (`cmake/aowis_bgfx.cmake`), not with `bgfx.cmake`.
`./git_submodule_init.sh --shader-tools` additionally checks out the bgfx shader
compiler sources, which are only needed to regenerate the committed shaders
(see `src/map/bgfx/README.md`, "bgfx build").

To **update** submodules to their remote-tracking branches, use:

```bash
./git_submodule_update.sh          # AOWIS submodules and GeographicLib only
./git_submodule_update.sh --bgfx   # bgfx, bx and bimg only
./git_submodule_update.sh --all    # Both groups
./git_submodule_update.sh --help   # Usage
```

The committed bgfx shader binaries (`src/map/bgfx/shaders/bin`) must match the
bgfx revision. After `--bgfx`, run `./compile_linux.sh --shader-tools` and commit
the regenerated shaders together with the new `external/bgfx`, `external/bx`
and `external/bimg` gitlinks.

**First-time setup of the bgfx patch:** ZIP files cannot contain Git submodule
gitlinks. If the bgfx-group entries exist in `.gitmodules` but have not yet
been registered in Git, either `git_submodule_init.sh` or
`git_submodule_update.sh --bgfx` will register them automatically. Review and
commit the new gitlinks alongside `.gitmodules`, so subsequent clones can
initialize all dependencies normally.

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
The native Wayland path uses Qt GuiPrivate's
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

The WebAssembly build is a `Release` build. Two switches are taken from the environment of `compile_wasm.sh`:

- `AOWIS_WASM_SIMD` (default `ON`): compiles the GUI with WebAssembly SIMD (`-msimd128`), so the map's vertex generation runs vectorized. Every browser with WebGL 2 supports it (Chrome/Edge 91+, Firefox 89+, Safari 16.4+); `AOWIS_WASM_SIMD=OFF ./compile_wasm.sh` builds a module for browsers without it.
- `AOWIS_WASM_FUNCTION_NAMES` (default `OFF`): keeps function names in the module for readable browser stack traces (a few MB larger download).

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
map_desktop_renderer=bgfx

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

Native builds accept `map_desktop_renderer=bgfx`; bgfx is the only map renderer (2D and 3D globe). The former values `cpu` and `rhi` (the CPU and QRhi renderers, both removed) are accepted with a warning and select bgfx. If bgfx cannot start, Map Monitor shows a status message.

WebAssembly builds accept `map_wasm_renderer=bgfx` (WebGL 2 through bgfx). The former values `browser` (the removed JavaScript/WebGL renderer) and `rhi` (the removed QRhi renderer) are accepted with a warning and select bgfx; `compile_wasm.sh` rewrites them to `bgfx` in the deployed configuration. If the GPU renderer cannot start, Map Monitor shows a status message, as on the desktop. Map Editor renders with bgfx as well, as on the desktop.

The bgfx map renders on demand: a frame is drawn only when the camera, the viewport, the content or an animation changes, so an idle map costs no CPU or GPU time in the browser. While the map is only panned, the editor's icons and the map labels are not rebuilt for every frame: they are built once with a margin of about a fifth of the screen around the visible area and then moved with the map, and built again only when the pan leaves that margin (zooming, resizing and editing rebuild them as before). Map tiles are decoded by the browser's own native image decoders (`createImageBitmap` on a dedicated worker, pixels written straight into the GPU's pixel format) and handed to WebGL without a further copy, into a few large atlas textures, so all visible map tiles are drawn with a single draw call; browsers without `OffscreenCanvas` in workers, and the first tiles after start, use Qt's decoders on worker threads instead. The browser canvas uses multisampling only below a device pixel ratio of 1.75 (on HiDPI screens the pixels are small enough without it, and there are four times as many) and requests the high-performance GPU on systems with two. `compile_wasm.sh` preserves an existing `build-wasm-dist/aowis-server-gui.ini` (retired `map_wasm_renderer` values are rewritten to `bgfx`).

WebAssembly builds include `aowis-server-gui.ini` beside the generated `index.html`. Set `examples_builtin_enable`, `map_wasm_renderer`, `base_url`, `api_key`, and `delete_api_key` there and reload the page. The browser first loads the configuration from the same deployment directory as `index.html`, so subdirectory deployments work correctly; `/aowis-server-gui.ini` at the origin root remains a compatibility fallback. Existing administrator-edited files in `build-wasm` and `build-wasm-dist` are preserved across rebuilds. The normal API key is used for tile requests; the delete API key is used only for tile-cache deletion requests.
