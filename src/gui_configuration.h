#ifndef GUI_CONFIGURATION_H
#define GUI_CONFIGURATION_H

#include <QKeySequence>
#include <QString>

#include "network_symbology.h"

class QKeyEvent;

enum class DesktopMapRenderer
{
    Cpu,
    Rhi
};

enum class WasmMapRenderer
{
    Browser,
    Rhi
};

struct GuiShortcutConfiguration
{
    QString sidebar_toggle = QStringLiteral("Win+Tab");
    QString fullscreen = QStringLiteral("F11");
    QString map_monitor_fullscreen = QStringLiteral("Ctrl+F11");
    QString simulation_run = QStringLiteral("Ctrl+R");
    QString simulation_run_alternate = QStringLiteral("Shift+Enter");

    QString map_zoom_in = QStringLiteral("E");
    QString map_zoom_out = QStringLiteral("Q");
    QString map_pan_up = QStringLiteral("W");
    QString map_pan_down = QStringLiteral("S");
    QString map_pan_left = QStringLiteral("A");
    QString map_pan_right = QStringLiteral("D");
    QString map_provider_arcgis_sat = QStringLiteral("F1");
    QString map_provider_openstreetmap = QStringLiteral("F2");
    QString map_provider_opentopomap = QStringLiteral("F3");
    QString map_provider_cycloosm = QStringLiteral("F4");

    QString map_editor_select = QStringLiteral("Esc");
    QString map_editor_delete = QStringLiteral("Del");
    QString map_editor_add_pipe = QStringLiteral("1");
    QString map_editor_add_junction = QStringLiteral("2");
    QString map_editor_add_valve = QStringLiteral("3");
    QString map_editor_add_customer_point = QStringLiteral("4");
    QString map_editor_add_pump = QStringLiteral("5");
    QString map_editor_add_tank = QStringLiteral("6");
    QString map_editor_add_power_source = QStringLiteral("7");
    QString map_editor_add_reservoir = QStringLiteral("8");
    QString map_editor_add_note = QStringLiteral("9");
};

struct GuiSymbologyPaletteConfiguration
{
    NetworkSymbologyPalette node_palette = NetworkSymbologyDefaultNodePalette;
    bool node_palette_flipped = false;
    NetworkSymbologyPalette link_palette = NetworkSymbologyDefaultLinkPalette;
    bool link_palette_flipped = false;
    NetworkSymbologyPalette heatmap_palette = NetworkSymbologyDefaultHeatmapPalette;
    bool heatmap_palette_flipped = false;
};

struct GuiMapPerformanceConfiguration
{
    // How much farther than the zoom level's default distance the 3D
    // camera can be pulled back. Mirrors MapModel::MaxView3dCameraDistanceAboveDefaultM's
    // default; kept independent (not referencing MapModel directly) so this
    // header doesn't need to depend on the map module.
    double max_view_distance_above_default_m = 500.0;
    // Target on-screen size, in pixels, of one terrain relief mesh cell.
    // Smaller values keep a denser (higher quality) mesh out to a greater
    // distance; larger values let quality fall off sooner. Mirrors
    // MapRhiBasemapRenderer's TerrainTargetCellSizePixels default.
    double terrain_lod_target_cell_size_px = 32.0;
    // Highest zoom level at which the terrain DEM is still fetched/used at
    // increasing resolution; beyond this the same elevation data is reused
    // and just re-subdivided rather than re-fetched at a finer level.
    // Mirrors MapRhiBasemapRenderer's TerrainReliefMaximumZoom default.
    int terrain_max_detail_zoom = 14;
    // Whether the RHI 3D renderer's texture-array draw-call batching is
    // used at all. Purely a performance path -- disabling it always falls
    // back to the per-tile path, never changes what is drawn.
    bool array_batching_enabled = true;
};

struct GuiConfiguration
{
    bool examples_builtin_enable = true;
    DesktopMapRenderer map_desktop_renderer = DesktopMapRenderer::Rhi;
    WasmMapRenderer map_wasm_renderer = WasmMapRenderer::Rhi;
    GuiSymbologyPaletteConfiguration symbology_palettes;
    GuiShortcutConfiguration shortcuts;
    GuiMapPerformanceConfiguration map_performance;
};

QString guiConfigurationFilePath();
const GuiConfiguration &guiConfiguration();
QKeySequence guiShortcutKeySequence(const QString &shortcut);
bool guiShortcutMatches(const QKeyEvent *event, const QString &shortcut,
                        Qt::KeyboardModifiers allowed_extra_modifiers = Qt::NoModifier);
DesktopMapRenderer desktopMapRenderer();
bool desktopMapRhiBuildAvailable();
const char *desktopMapRendererName(DesktopMapRenderer renderer);
WasmMapRenderer wasmMapRenderer();
bool wasmMapRhiBuildAvailable();
const char *wasmMapRendererName(WasmMapRenderer renderer);
bool saveGuiNodeSymbologyPalette(NetworkSymbologyPalette palette, bool flipped);
bool saveGuiLinkSymbologyPalette(NetworkSymbologyPalette palette, bool flipped);
bool saveGuiHeatmapSymbologyPalette(NetworkSymbologyPalette palette, bool flipped);
bool saveGuiMapPerformanceConfiguration(const GuiMapPerformanceConfiguration &configuration);

#endif // GUI_CONFIGURATION_H
