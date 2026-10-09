#ifndef MAP_NETWORK_SCENE_DIAGNOSTICS_H
#define MAP_NETWORK_SCENE_DIAGNOSTICS_H

#include <QtGlobal>

// Full retained-network geometry invalidation is intentionally tracked separately
// from camera-scale changes. Smooth zoom/orbit scale changes may rebuild derived
// screen-space decoration such as flow-direction chevrons, but retained network
// and icon geometry must stay intact. RenderOriginRebase is the one camera-driven
// full rebuild that remains legitimate: Globe float precision requires
// re-expressing retained ECEF vertices relative to the new origin.
enum class MapNetworkGeometryRebuildReason
{
    None,
    NetworkSnapshot,
    HiddenEntitySet,
    NodeDecluttering,
    GroundOffset,
    VerticalExaggeration,
    RenderOriginRebase
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
