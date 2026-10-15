#include "map/render/map_camera_view_state.h"

#include <cmath>
#include <cstdio>

namespace
{
bool require(bool valid, const char *description)
{
    if (!valid)
        std::fprintf(stderr, "map_camera_invalidation: %s\n", description);
    return valid;
}

bool noNetworkChange(const MapCameraUpdate &change)
{
    return !change.network_geometry_invalidated
        && !change.network_attributes_invalidated;
}
}

int main()
{
    MapCameraViewState original;
    original.mode = MapViewMode::TwoD;
    original.center_lon_deg = 18.19;
    original.center_lat_deg = 11.98;
    original.continuous_2d_zoom = 15.4;

    const MapCameraUpdate none = mapCameraUpdate(original, original, 15, 15);
    if (!require(!none.camera_changed && !none.imagery_tile_selection_changed
                     && noNetworkChange(none), "identical frame invalidated resources"))
        return 1;

    MapCameraViewState fractional = original;
    fractional.continuous_2d_zoom = 15.49;
    const MapCameraUpdate continuous = mapCameraUpdate(original, fractional, 15, 15);
    if (!require(continuous.camera_changed && !continuous.imagery_tile_selection_changed
                     && noNetworkChange(continuous), "fractional zoom invalidation"))
        return 1;

    MapCameraViewState boundary = fractional;
    boundary.continuous_2d_zoom = 15.51;
    const MapCameraUpdate crossing = mapCameraUpdate(fractional, boundary, 15, 16);
    if (!require(crossing.camera_changed && crossing.imagery_tile_selection_changed
                     && noNetworkChange(crossing), "integer tile boundary invalidated network"))
        return 1;

    // Resampling the *same* camera view with a different imagery LOD must
    // change tile resources only, not the camera transform or geometry.
    const MapCameraUpdate tile_only = mapCameraUpdate(boundary, boundary, 15, 16);
    if (!require(!tile_only.camera_changed && tile_only.imagery_tile_selection_changed
                     && noNetworkChange(tile_only), "tile selection affected camera/geometry"))
        return 1;

    MapCameraViewState pan = boundary;
    pan.center_lon_deg += 0.001;
    if (!require(mapCameraUpdate(boundary, pan, 16, 16).camera_changed
                     && noNetworkChange(mapCameraUpdate(boundary, pan, 16, 16)),
                 "panning invalidated network"))
        return 1;

    MapCameraViewState globe = pan;
    globe.mode = MapViewMode::Globe;
    globe.globe_yaw_deg = 30.0;
    globe.globe_distance_m = 25000.0;
    if (!require(mapCameraUpdate(pan, globe, 16, 16).camera_changed
                     && noNetworkChange(mapCameraUpdate(pan, globe, 16, 16)),
                 "globe transition invalidated network"))
        return 1;

    MapCameraViewState orbit = globe;
    orbit.globe_yaw_deg += 5.0;
    orbit.globe_distance_m *= 0.9;
    orbit.globe_vertical_offset_m += 20.0;
    const MapCameraUpdate orbit_change = mapCameraUpdate(globe, orbit, 16, 17);
    if (!require(orbit_change.camera_changed
                     && orbit_change.imagery_tile_selection_changed
                     && noNetworkChange(orbit_change),
                 "globe orbit/zoom invalidated network"))
        return 1;

    std::puts("Camera and imagery LOD invalidation contract passed");
    return 0;
}
