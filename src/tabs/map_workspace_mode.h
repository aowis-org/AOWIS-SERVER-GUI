#pragma once

#include <array>
#include <cstddef>

// The map workspace's mode contract is independent of Qt, GPU ownership and
// the concrete tool panels. The integer values are the tab/page indices;
// changing the order is an intentional UI migration, not a renderer change.
namespace MapWorkspace
{
enum class Domain
{
    Distribution,
    Drainage
};

enum class Activity
{
    Monitor,
    Editor
};

struct ModeInfo
{
    const char *title;
    const char *icon_resource;
    Domain domain;
    Activity activity;
    // A blank drainage tool panel is deliberate, not a different renderer.
    bool tools_implemented;
};

inline constexpr std::array<ModeInfo, 4> Modes{{
    {"Distribution Monitor", ":/icon/water_distribution_monitor.png",
     Domain::Distribution, Activity::Monitor, true},
    {"Distribution Editor", ":/icon/water_distribution_editor.png",
     Domain::Distribution, Activity::Editor, true},
    {"Drainage Monitor", ":/icon/water_drainage_monitor.png",
     Domain::Drainage, Activity::Monitor, false},
    {"Drainage Editor", ":/icon/water_drainage_editor.png",
     Domain::Drainage, Activity::Editor, false}
}};

inline constexpr int DistributionMonitor = 0;
inline constexpr int DistributionEditor = 1;
inline constexpr int DrainageMonitor = 2;
inline constexpr int DrainageEditor = 3;

constexpr const ModeInfo *modeInfo(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= Modes.size())
        return nullptr;
    return &Modes[static_cast<std::size_t>(index)];
}

// A drainage mode reuses the map/camera but does not render or pick any
// distribution entities. No geometry snapshot is cleared on mode changes.
constexpr bool rendersDistributionNetwork(int index)
{
    const ModeInfo *mode = modeInfo(index);
    return mode != nullptr && mode->domain == Domain::Distribution;
}

constexpr bool isImplemented(int index)
{
    const ModeInfo *mode = modeInfo(index);
    return mode != nullptr && mode->tools_implemented;
}

// Editor uses its own full-colour pixmaps in the globe HUD overlay (not
// monitor 3D models). Monitor follows its configurable "3D icons" option.
// The underlying MapBgfxSurface is shared across both modes.
constexpr bool useGlobe3dIconModels(int mode, bool monitor_preference = false)
{
    return mode == DistributionMonitor && monitor_preference;
}

static_assert(Modes.size() == 4);
static_assert(Modes[DistributionMonitor].activity == Activity::Monitor);
static_assert(Modes[DistributionEditor].activity == Activity::Editor);
static_assert(Modes[DrainageMonitor].domain == Domain::Drainage);
static_assert(Modes[DrainageEditor].domain == Domain::Drainage);
static_assert(rendersDistributionNetwork(DistributionMonitor));
static_assert(rendersDistributionNetwork(DistributionEditor));
static_assert(!rendersDistributionNetwork(DrainageMonitor));
static_assert(!rendersDistributionNetwork(DrainageEditor));
}
