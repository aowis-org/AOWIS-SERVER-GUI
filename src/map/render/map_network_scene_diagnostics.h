#ifndef MAP_NETWORK_SCENE_DIAGNOSTICS_H
#define MAP_NETWORK_SCENE_DIAGNOSTICS_H

#include <QtGlobal>

// Full retained-network geometry invalidation is intentionally tracked separately
// from camera-scale changes. Smooth zoom/orbit scale changes may rebuild derived
// screen-space decoration such as flow-direction chevrons, but retained network
// and icon geometry must stay intact. Globe camera-origin rebasing is not a
// geometry rebuild reason once the retained network has its own ECEF frame.
enum class MapNetworkGeometryRebuildReason
{
    None,
    NetworkSnapshot,
    HiddenEntitySet,
    NodeDecluttering,
    GroundOffset,
    VerticalExaggeration
};

// Lightweight renderer diagnostics used by neutral regression tests and during
// performance work. These are counters only; they do not participate in render
// decisions and therefore cannot themselves invalidate scene state.
struct MapNetworkSceneDiagnostics
{
    quint64 full_geometry_rebuilds = 0;
    quint64 icon_rebuilds = 0;
    quint64 flow_direction_rebuilds = 0;
    quint64 highlight_rebuilds = 0;
    quint64 style_rebuilds = 0;
    quint64 camera_scale_updates = 0;
    quint64 demand_point_attachment_symbology_updates = 0;
    MapNetworkGeometryRebuildReason last_geometry_rebuild_reason =
        MapNetworkGeometryRebuildReason::None;
};

#endif // MAP_NETWORK_SCENE_DIAGNOSTICS_H
