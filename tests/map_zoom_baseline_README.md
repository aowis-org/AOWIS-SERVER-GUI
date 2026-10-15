# Map rendering baseline — bgfx migration, Step 1

This is **diagnostics only**. Nothing here changes how the current network is
prepared, cached or rendered. Zoom-dependent flow/icon rebuilding remains a
known baseline cost until the early architectural migration steps.

## Headless synthetic benchmark

Use the existing tests-enabled CMake build on a machine with Qt 6 and the
AOWIS-SERVER-MODEL submodule available:

```sh
cmake --build build-linux-tests-release --target aowis-map-zoom-baseline-benchmark
ctest --test-dir build-linux-tests-release -R '^aowis-map-zoom-baseline-benchmark$' -V
```

The benchmark also belongs to `aowis-benchmarks` and has the CTest labels
`aowis;performance`. It makes a fixed **100 × 100 grid (10,000 demand points),
10,000 attachment lines, 100 pipes and 200 junctions** in memory and exercises
the real retained **2D CPU-side scene preparation** code (`MapRhiScene`) through
80 integer zoom transitions (15 ↔ 16). Its JSON report includes counts,
total elapsed preparation times and vertex counts. Structural assertions
ensure that zoom alone does not change static link/node vertex *counts* or
the network geometry revision. They deliberately do **not** assert zero
flow/icon rebuilds: those are the defects the subsequent steps will correct.

This benchmark **does not** exercise the asynchronous QWidget raster cache,
physical GPU uploads, a real on-screen pan, or the globe. Use the interactive
baseline procedure below for those paths. The benchmark's timing fields are
informational, never pass/fail criteria, and are only comparable using the
same hardware, compiler, build type and conditions.

## Desktop interactive baseline

Run the ordinary application with diagnostic logging enabled **before** Qt
starts, for example:

```sh
AOWIS_MAP_RENDER_DIAGNOSTICS=1 ./build-linux/aowis-server-gui 2> map-render-baseline.log
```

Load a representative demand-point-heavy network in Map Monitor. Repeat this
sequence in 2D and Globe mode, letting the counter stream report between
phases:

1. Stand still for at least five seconds after the initial network load.
2. Slowly zoom in/out across integer zoom boundaries for 15 seconds.
3. Pan for 15 seconds without changing the zoom.
4. Rotate/tilt/orbit the globe for 15 seconds.
5. Select entities and change a simulation visualization, then repeat zooming.

Every ~5 seconds of active rendering, a cumulative JSON log entry is emitted
with `kind=aowis_map_render_diagnostics`. `counts` contains event totals;
`total_duration_ms` contains accumulated **CPU wall-clock duration** for
instrumented operations, while `max_duration_ms` reports the longest observed
single invocation (useful for detecting frame-time spikes). These are cumulative since process startup; use
*differences between consecutive reports* for an individual scenario.
`rhi_render_cpu_frames` is **not GPU timing** and includes Qt rendering/command
preparation; it is not a GPU frame latency measurement. Counters for GPU
uploads measure bytes **submitted** through QRhi, not GPU transfer completion
or VRAM consumption. CPU cache counters are from the QPainter fallback.

Of particular interest:

- `zoom_boundary`, `fractional_zoom`, `center_change`
- `cpu_cache_zoom_requests`, `cpu_cache_forced`, `cpu_cache_submitted`,
  `cpu_cache_cancelled`, `cpu_cache_accepted`, `cpu_cache_rejected`
- `cpu_geometry_preparation_calls`, `cpu_raster_attempts`, `cpu_overlay_paint`
- `flat_geometry_rebuilt`, `flat_icons_rebuilt`, `flat_flow_rebuilt`
- `globe_geometry_rebuilt`, `globe_origin_rebased`, `globe_flow_rebuilt`
- `demand_attachments_prepared` (sum of attachment records visited by scene builds)
- `network_buffer_uploads`, `network_buffer_bytes`, `terrain_buffer_uploads`,
  `terrain_buffer_bytes`

## WebAssembly / legacy browser network baseline

After deploying the WASM build, open the application URL with
`?aowisMapDiagnostics=1` and repeat the 2D zoom and pan experiment. The
browser console prints cumulative
`kind=aowis_browser_map_render_diagnostics` entries with the JS network
geometry, flow-geometry, frame counts/durations and WebGL network buffer
upload counts/bytes. Like native diagnostics, browser `total_duration_ms` is
**JavaScript CPU time**, not GPU timing. This measures the existing browser
WebGL path, not the separate bgfx probe.

Do **not** compare JS and native timings as equivalent performance metrics.
Do not treat these baselines as evidence of achieved bgfx performance.

The diagnostics are disabled by default. To return to ordinary behavior,
unset `AOWIS_MAP_RENDER_DIAGNOSTICS` or remove the browser URL parameter.
