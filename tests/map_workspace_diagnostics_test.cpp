#include "map/render/map_render_diagnostics.h"

#include <QByteArray>
#include <QJsonObject>

#include <cstdio>

namespace
{
bool require(bool valid, const char *message)
{
    if (!valid)
        std::fprintf(stderr, "map_workspace_diagnostics: %s\n", message);
    return valid;
}
}

int main()
{
    // enabled() reads its flag once: set it before the first counter action.
    qputenv("AOWIS_MAP_RENDER_DIAGNOSTICS", QByteArrayLiteral("1"));
    using namespace MapRenderDiagnostics;
    if (!require(enabled(), "diagnostic flag not enabled"))
        return 1;

    add(Counter::BgfxDeviceStarted);
    add(Counter::Bgfx2dScenePrepared, 2);
    {
        const Scope switch_duration(Counter::WorkspaceModeSwitch);
    }
    add(Counter::BgfxDeviceStopped);
    add(Counter::GlobeEditorIconIndexBuild);
    add(Counter::GlobeEditorIconAnchors, 100000);
    add(Counter::GlobeEditorIconCandidates, 740);
    add(Counter::GlobeEditorIconVisible, 350);
    {
        const Scope icon_frame(Counter::GlobeEditorIconFrame);
        {
            const Scope query(Counter::GlobeEditorIconIndexQuery);
        }
        {
            const Scope projection(Counter::GlobeEditorIconProjection);
        }
        {
            const Scope drawing(Counter::GlobeEditorIconDraw);
        }
    }

    const QJsonObject result = snapshot();
    const QJsonObject counts = result.value(QStringLiteral("counts")).toObject();
    const QJsonObject totals = result.value(QStringLiteral("total_duration_ms")).toObject();
    if (!require(value(Counter::BgfxDeviceStarted) == 1, "device start not counted")
        || !require(value(Counter::BgfxDeviceStopped) == 1, "device stop not counted")
        || !require(value(Counter::Bgfx2dScenePrepared) == 2, "geometry prep not counted")
        || !require(value(Counter::WorkspaceModeSwitch) == 1, "mode switch not counted")
        || !require(counts.value(QStringLiteral("workspace_mode_switch")).toDouble() == 1.0,
                    "mode switch JSON key mismatch")
        || !require(counts.value(QStringLiteral("bgfx_device_started")).toDouble() == 1.0,
                    "device start JSON key mismatch")
        || !require(counts.value(QStringLiteral("globe_editor_icon_index_build")).toDouble() == 1.0,
                    "icon spatial-index creation not reported")
        || !require(counts.value(QStringLiteral("globe_editor_icon_anchors")).toDouble() == 100000.0,
                    "icon anchor population not reported")
        || !require(counts.value(QStringLiteral("globe_editor_icon_candidates")).toDouble() == 740.0,
                    "icon spatial-culling candidates not reported")
        || !require(counts.value(QStringLiteral("globe_editor_icon_visible")).toDouble() == 350.0,
                    "actual projected icons not reported")
        || !require(counts.value(QStringLiteral("globe_editor_icon_frame")).toDouble() == 1.0,
                    "icon frame count not reported")
        || !require(counts.value(QStringLiteral("globe_editor_icon_index_query")).toDouble() == 1.0,
                    "icon query count not reported")
        || !require(counts.value(QStringLiteral("globe_editor_icon_projection")).toDouble() == 1.0,
                    "icon projection count not reported")
        || !require(counts.value(QStringLiteral("globe_editor_icon_draw")).toDouble() == 1.0,
                    "icon draw count not reported")
        || !require(totals.contains(QStringLiteral("globe_editor_icon_frame")),
                    "3D icon frame timing not reported")
        || !require(totals.contains(QStringLiteral("workspace_mode_switch")),
                    "mode switch timing not reported"))
        return 1;

    std::puts("Map workspace render lifecycle diagnostics passed");
    return 0;
}
