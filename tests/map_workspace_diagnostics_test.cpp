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
        || !require(totals.contains(QStringLiteral("workspace_mode_switch")),
                    "mode switch timing not reported"))
        return 1;

    std::puts("Map workspace render lifecycle diagnostics passed");
    return 0;
}
