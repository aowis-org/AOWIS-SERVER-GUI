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

Native builds create the following file automatically on first use:

```text
~/.local/share/aowis-server-gui/aowis-server-gui.ini
```

`XDG_DATA_HOME` is respected when it is set. The default file is:

```ini
[gui]
examples_builtin_enable=true
map_desktop_renderer=cpu

[map_server]
base_url=http://aowis-server-map.localhost:80
api_key=
delete_api_key=
```

Restart the application after editing the file.

Set `examples_builtin_enable=false` to hide the bundled `Examples` project and its revisions from the toolbar. If the option is missing, built-in examples are enabled.

Native builds accept `map_desktop_renderer=cpu` or `map_desktop_renderer=rhi`. `cpu` requests the existing QPainter-based desktop renderer. `rhi` requests the native QRhi backend when the build provides Qt 6.7+ and `Qt6::GuiPrivate`; otherwise renderer selection resolves to CPU and logs the reason. WebAssembly ignores this desktop-only option and continues to use its browser renderer.

WebAssembly builds include `aowis-server-gui.ini` in the generated webroot. Set `examples_builtin_enable`, `base_url`, `api_key`, and `delete_api_key` there and reload the page. The browser always loads `/aowis-server-gui.ini` from the webroot root. Existing administrator-edited files in `build-wasm` and `build-wasm-dist` are preserved across rebuilds. The normal API key is used for tile requests; the delete API key is used only for tile-cache deletion requests.
