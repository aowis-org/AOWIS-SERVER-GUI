#ifndef MAP_RHI_GLOBE_TERRAIN_HEIGHT_CACHE_H
#define MAP_RHI_GLOBE_TERRAIN_HEIGHT_CACHE_H

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

class MapRhiGlobeSurfaceBackend;
class MapTerrainRepository;
class QRhiResourceUpdateBatch;
struct MapGlobeSurfaceTile;

// Owns the GPU-resident DEM height-texture-array cache used for Globe
// terrain vertex displacement: an LRU keyed by terrain tile key, backed by
// fixed-size array pages allocated from MapRhiGlobeSurfaceBackend on demand.
//
// This class is deliberately blind to everything else MapRhiGlobeRenderer
// juggles per tile (imagery array assignment, heatmap array assignment,
// the shared TileGpuState struct those live in). prepare() reports each
// ready DEM assignment back through a callback instead of writing into that
// state directly, so this cache has no dependency on the renderer's private
// types and can be reasoned about (and eventually unit-tested) entirely on
// its own terms -- mirroring the separation MapGlobeSurfacePreparation
// already keeps between tile-geometry preparation and GPU resource state.
class MapRhiGlobeTerrainHeightCache
{
public:
    // Invoked once per window tile whose DEM is resident and uploaded this
    // frame, with the array page/layer the caller should record against
    // that tile for the vertex-displacement shader to sample.
    using TileReadyCallback = std::function<void(
        const MapGlobeSurfaceTile &tile, int array_page, int array_layer)>;

    // Drops every cached entry and all backing array pages, and clears the
    // disabled/profile state back to fresh-construction defaults. Call
    // when the Globe view resets (terrain repository swapped, imagery
    // provider changed, etc.) so a stale layer assignment cannot leak into
    // unrelated content uploaded into the same physical layer later.
    void reset(MapRhiGlobeSurfaceBackend &surface_backend);

    // Marks terrain_key's cached entry (if any) as needing re-upload on the
    // next prepare() call, without evicting its page/layer assignment. Call
    // when a newer version of that DEM tile becomes available (e.g. a
    // higher-resolution replacement finished loading) so the stale texture
    // isn't kept indefinitely just because the key is still resident.
    void invalidate(const QString &terrain_key);

    // Ensures every DEM visible in window_tiles this frame is uploaded,
    // evicting the least-recently-used entry (never one whose terrain_key
    // appears in window_tiles this frame) when every page is full, growing
    // a new page first if the backend allows it. Safe to call every frame;
    // a no-op when there is nothing to do (no repository, no GPU context,
    // map hidden, cache disabled after an unsupported-format check, no
    // terrain-carrying tiles in the window). on_tile_ready is invoked once
    // per tile whose DEM is already uploaded and ready to sample, whether
    // that happened just now or on an earlier frame.
    void prepare(
        MapRhiGlobeSurfaceBackend &surface_backend,
        MapTerrainRepository *terrain_repository,
        QRhiResourceUpdateBatch *resource_updates,
        bool map_visible,
        const QVector<MapGlobeSurfaceTile> &window_tiles,
        const TileReadyCallback &on_tile_ready);

private:
    struct CacheEntry
    {
        int array_page = -1;
        int array_layer = -1;
        quint64 last_used_frame = 0;
        bool uploaded = false;
    };

    // Per-request counters for the opt-in Globe terrain-height-cache
    // performance log. Kept entirely outside cache state so enabling
    // diagnostics cannot change which tiles/pages/layers are selected.
    struct ProfileCounters
    {
        bool enabled = false;
        int visible_terrain_tiles = 0;
        int unique_visible_dem_tiles = 0;
        int available_dem_tiles = 0;
        int ready_terrain_tiles = 0;
        int cache_hits = 0;
        int cache_misses = 0;
        int uploads = 0;
        int pending_uploads = 0;
        int evictions = 0;
        int capacity_misses = 0;
        quint64 upload_bytes = 0;
        qint64 cpu_ns = 0;
    };

    bool createArrayPage(MapRhiGlobeSurfaceBackend &surface_backend);
    void releaseEntry(
        MapRhiGlobeSurfaceBackend &surface_backend, const QString &terrain_key);
    CacheEntry *ensureEntry(
        MapRhiGlobeSurfaceBackend &surface_backend,
        const QString &terrain_key,
        const QSet<QString> &protected_terrain_keys);
    void reportProfile(const MapRhiGlobeSurfaceBackend &surface_backend) const;

    QHash<QString, CacheEntry> cache;
    quint64 frame = 0;
    bool disabled = false;
    bool page_growth_disabled = false;
    ProfileCounters profile;
};

#endif // MAP_RHI_GLOBE_TERRAIN_HEIGHT_CACHE_H
