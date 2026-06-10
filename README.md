# AOWIS-EPANET-GUI
EPANET based Controller for Water Networks.

This is the GUI for the AOWIS-EPANET-Server.

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
At this point, Windows and macOS are not officially supported by AOWIS. However, you can build this application for them, just as any other Qt-Application.


