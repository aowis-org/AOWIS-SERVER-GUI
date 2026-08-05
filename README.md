# AOWIS-SERVER-GUI (*AOWIS Controller*)

[EPANET](https://github.com/OpenWaterAnalytics/EPANET) based Controller for Water Networks.

This is the GUI for the AOWIS-SERVER infrastucture.

This software is written in C++ with Qt and can be compiled for all major platforms, including WebAssembly.

## Build

### Linux
Run the script `compile_linux.sh`

Find the result in the folder `build-linux`

### WebAssembly
Preparing a working Qt + Emscripten environment takes some effort.

Therefore, `compile_wasm.sh` will pull and run a docker-image first, where everything is already set up, and uses this for the build.

Your system user needs to be allowed to run docker, or run the script as root.

Adding your system user to the group `docker`, and log out and in again should be enough:
```
usermod -aG docker [USERNAME]
```

Find the result in the folder `build-wasm`

To run the result, you need to have a web server set up.

### Windows / macOS
Usually as a Desktop-Client, you might want to prefer [AOWIS-SERVER-Standalone](https://github.com/aowis-org/AOWIS-SERVER-Standalone).


## Map server client configuration

Native builds create the following file automatically on first use:

```text
~/.local/share/aowis-server-gui/aowis-server-gui.ini
```

`XDG_DATA_HOME` is respected when it is set. The default file is:

```ini
[map_server]
base_url=http://aowis-server-map.localhost:80
api_key=
delete_api_key=
```

Restart the application after editing the file.

WebAssembly builds include `aowis-server-gui.ini` in the generated webroot. Set `base_url`, `api_key`, and `delete_api_key` there and reload the page. The browser always loads `/aowis-server-gui.ini` from the webroot root. Existing administrator-edited files in `build-wasm` and `build-wasm-dist` are preserved across rebuilds. The normal API key is used for tile requests; the delete API key is used only for tile-cache deletion requests.
