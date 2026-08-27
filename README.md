# AOWIS-SERVER-GUI (*AOWIS Controller*)

[EPANET](https://github.com/OpenWaterAnalytics/EPANET) based Controller for Water Networks.

This is the GUI for the AOWIS-SERVER infrastucture.

This software is written in C++ with Qt and can be compiled for all major platforms, including WebAssembly.

## Build

### Linux
Run the script `compile_linux.sh`

Find the result in the folder `build-linux`

### WebAssembly
The WebAssembly compiler is fully self-contained in an AOWIS-owned Docker image. It does not depend on a prebuilt third-party Qt/Emscripten image.

`compile_wasm.sh` uses the pinned versions from `tools/qt-emscripten/toolchain_versions.sh`. On the first build it automatically runs `prepare_wasm_docker.sh`, which starts from Ubuntu, installs the matching Emscripten SDK, builds same-version Qt host tools, and builds Qt for WebAssembly with thread support (`-feature-thread`). The image also performs a small Qt Widgets/Network/Sql/QThread smoke build before it is accepted.

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
map_editor_add_customer_point=4
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

WebAssembly builds include `aowis-server-gui.ini` in the generated webroot. Set `examples_builtin_enable`, `map_wasm_renderer`, `base_url`, `api_key`, and `delete_api_key` there and reload the page. The browser always loads `/aowis-server-gui.ini` from the webroot root. Existing administrator-edited files in `build-wasm` and `build-wasm-dist` are preserved across rebuilds. The normal API key is used for tile requests; the delete API key is used only for tile-cache deletion requests.
