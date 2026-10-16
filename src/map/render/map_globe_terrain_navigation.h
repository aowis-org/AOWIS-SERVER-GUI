#ifndef MAP_GLOBE_TERRAIN_NAVIGATION_H
#define MAP_GLOBE_TERRAIN_NAVIGATION_H

#include "map/render/map_render_surface.h"

#include <QElapsedTimer>
#include <QPoint>
#include <QPointF>
#include <QSize>

class MapGlobeSurfacePreparation;
class MapModel;
class MapTerrainRepository;

// Terrain-aware Globe navigation, backend-neutral (roadmap Phase 3, slice 2):
//
// - the camera height: the orbit rig follows the DEM perpendicularly beneath
//   the eye (MapModel's vertical offset) and, while rotating around a frozen
//   pivot, an eye-only collision lift keeps the minimum ground clearance.
//   Both approach their targets with a monotonic, frame-rate-independent
//   exponential (no overshoot at ridges or valleys);
// - screen rays against the rendered surface: the retained DEM meshes of
//   MapGlobeSurfacePreparation (the exact triangles on screen), the WGS84
//   ellipsoid where no mesh is loaded;
// - terrain-anchored panning: the grabbed DEM point stays under the pointer
//   (MapModel::panGlobeByTerrainPointerDrag), blended out near the horizon
//   where the ray hits the ground at a grazing angle;
// - the pointer coordinate with its DEM altitude, and cached DEM elevations
//   at the LOD the surface shows (2D footer, X-Ray classification later).
//
// This is the bgfx port of the terrain code in MapRhiWidget (same constants,
// same sampling, same filter); that copy goes with src/map/rhi/ at the end of
// Phase 3. The owner keeps it fed: setSurfacePreparation() with the globe it
// draws, setViewportSize() with the logical size of the map, and calls
// syncCameraHeight() on camera changes and on every frame while following().
class MapGlobeTerrainNavigation
{
public:
    explicit MapGlobeTerrainNavigation(MapModel *map_model);

    void setTerrainRepository(MapTerrainRepository *terrain_repository);
    // The rendered globe whose retained DEM meshes answer ray hits and the
    // visible terrain LOD; nullptr: ellipsoid and repository tiles only.
    void setSurfacePreparation(const MapGlobeSurfacePreparation *surface_preparation);
    // Logical (device-independent) size of the map, as MapWidget's size().
    void setViewportSize(const QSize &viewport_size);

    // DEM elevation at coordinate from cached tiles, preferring the LOD the
    // surface shows there; cell_size_m is that LOD's sampling interval.
    bool terrainElevationAtCoordinate(const CoordinateWGS84 &coordinate,
                                      double *elevation_m, double *cell_size_m) const;
    bool cachedTerrainElevationAtCoordinate(const CoordinateWGS84 &coordinate,
                                            double *elevation_m) const;
    // Ray from the screen position (logical pixels, top-left origin) against
    // the rendered DEM meshes, else the ellipsoid. false: the ray misses.
    bool surfaceRayHitAtScreen(const QPointF &screen_position, MapGlobeSurfaceHit *hit) const;
    // MapWidget's Globe pointer pan with the grabbed DEM point kept under the
    // pointer. false: not applicable (no mesh hit, grazing angle); MapWidget
    // then pans over the ellipsoid.
    bool panByTerrainPixels(const QPoint &delta_pixels);
    // Pointer coordinate: Globe -> surface ray (altitude from the DEM when a
    // mesh was hit); 2D -> the flat map's coordinate with a cached altitude.
    bool terrainCoordinateAtScreen(const QPointF &screen_position, CoordinateWGS84 *coordinate);

    // One step of the camera-height filter (vertical offset and collision
    // lift) towards the DEM under the eye. request_missing_tile also
    // requests the exact DEM tile of the current LOD under the eye (camera
    // changes do; the per-frame continuation does not). Outside Globe mode
    // this only stops the follow.
    void syncCameraHeight(bool request_missing_tile);
    // Still settling: call syncCameraHeight(false) again on the next frame.
    bool following() const { return this->follow_active; }
    // Ends a running follow; the next step starts with a fresh time step
    // (mode switch, start or end of an orbit).
    void stopFollowing();

private:
    // DEM under the eye at the LOD the camera distance selects (requests it
    // when asked), coarser cached levels while it loads.
    bool cameraTerrainElevationAtCoordinate(const CoordinateWGS84 &coordinate,
                                            double *elevation_m, bool request_missing_tile) const;

    MapModel *map_model = nullptr;
    MapTerrainRepository *terrain_repository = nullptr;
    const MapGlobeSurfacePreparation *surface_preparation = nullptr;
    QSize viewport_size;
    // Real time between filter steps (frames, camera changes).
    QElapsedTimer follow_clock;
    bool follow_active = false;
    // setViewGlobeTerrainHeightOffsetsM() can re-enter through MapModel's
    // signals; one step at a time.
    bool sync_active = false;
};

#endif // MAP_GLOBE_TERRAIN_NAVIGATION_H
