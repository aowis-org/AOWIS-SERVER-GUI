#include "map/rhi/map_rhi_globe_terrain_height_cache.h"

#include "map/data/map_terrain_repository.h"
#include "map/data/map_terrain_tile.h"
#include "map/render/map_globe_surface_preparation.h"
#include "map/rhi/map_rhi_globe_surface_backend.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QSize>

Q_LOGGING_CATEGORY(
    globeTerrainHeightCachePerformanceLog,
    "aowis.map.rhi.globe.terrain.height_cache.performance",
    QtInfoMsg)

namespace
{
// A single R32F page is roughly 4.13 MiB (256 * 65 * 65 * 4 bytes). Eight
// lazy pages cap this preparatory cache at roughly 33 MiB while retaining
// 2048 unique DEM tiles -- normally far more than one quadtree window needs.
// When full, only least-recently-used, currently invisible entries can be
// recycled. The CPU terrain renderer remains authoritative in this patch.
constexpr int GlobeTerrainHeightArrayLayerCount = 256;
constexpr int GlobeTerrainHeightArrayMaximumPageCount = 8;
constexpr int GlobeTerrainHeightUploadBudgetPerFrame = 32;
static_assert(sizeof(float) == 4, "R32F terrain uploads require 32-bit float");
constexpr quint64 GlobeTerrainHeightTileBytes =
    quint64(MapTerrainTileSampleCount) * quint64(sizeof(float));
}

void MapRhiGlobeTerrainHeightCache::reset(MapRhiGlobeSurfaceBackend &surface_backend)
{
    surface_backend.clearTerrainHeightArrayPages();
    this->cache.clear();
    this->frame = 0;
    this->disabled = false;
    this->page_growth_disabled = false;
    this->profile = ProfileCounters();
}

void MapRhiGlobeTerrainHeightCache::invalidate(const QString &terrain_key)
{
    const QHash<QString, CacheEntry>::iterator iterator = this->cache.find(terrain_key);
    if (iterator != this->cache.end())
        iterator.value().uploaded = false;
}

bool MapRhiGlobeTerrainHeightCache::createArrayPage(
    MapRhiGlobeSurfaceBackend &surface_backend)
{
    return surface_backend.createTerrainHeightArrayPage(
        QSize(MapTerrainTileGridSize, MapTerrainTileGridSize),
        GlobeTerrainHeightArrayLayerCount,
        GlobeTerrainHeightArrayMaximumPageCount);
}

void MapRhiGlobeTerrainHeightCache::releaseEntry(
    MapRhiGlobeSurfaceBackend &surface_backend, const QString &terrain_key)
{
    const QHash<QString, CacheEntry>::iterator iterator =
        this->cache.find(terrain_key);
    if (iterator == this->cache.end())
        return;

    const CacheEntry entry = iterator.value();
    if (entry.array_page >= 0
        && entry.array_page < int(surface_backend.terrainHeightArrayPages().size())
        && entry.array_layer >= 0
        && entry.array_layer < GlobeTerrainHeightArrayLayerCount)
    {
        MapRhiGlobeTerrainHeightArrayPage &page =
            surface_backend.terrainHeightArrayPages()[entry.array_page];
        if (!page.free_layers.contains(entry.array_layer))
            page.free_layers.append(entry.array_layer);
    }
    this->cache.erase(iterator);
}

MapRhiGlobeTerrainHeightCache::CacheEntry *
MapRhiGlobeTerrainHeightCache::ensureEntry(
    MapRhiGlobeSurfaceBackend &surface_backend,
    const QString &terrain_key,
    const QSet<QString> &protected_terrain_keys)
{
    QHash<QString, CacheEntry>::iterator existing = this->cache.find(terrain_key);
    if (existing != this->cache.end())
    {
        existing.value().last_used_frame = this->frame;
        return &existing.value();
    }

    const std::function<int()> free_page_index = [&surface_backend]()
    {
        for (int page_index = 0;
             page_index < int(surface_backend.terrainHeightArrayPages().size());
             ++page_index)
        {
            if (!surface_backend.terrainHeightArrayPages()[page_index]
                     .free_layers.isEmpty())
            {
                return page_index;
            }
        }
        return -1;
    };

    int page_index = free_page_index();
    if (page_index < 0
        && !this->page_growth_disabled
        && int(surface_backend.terrainHeightArrayPages().size())
            < GlobeTerrainHeightArrayMaximumPageCount)
    {
        if (createArrayPage(surface_backend))
        {
            page_index = int(surface_backend.terrainHeightArrayPages().size()) - 1;
        }
        else
        {
            // A backend allocation failure should not be retried once per
            // remaining visible key. Existing pages remain useful and can
            // continue recycling invisible entries.
            this->page_growth_disabled = true;
        }
    }

    if (page_index < 0)
    {
        QHash<QString, CacheEntry>::const_iterator oldest = this->cache.cend();
        for (QHash<QString, CacheEntry>::const_iterator iterator =
                 this->cache.cbegin();
             iterator != this->cache.cend(); ++iterator)
        {
            if (protected_terrain_keys.contains(iterator.key()))
                continue;
            if (oldest == this->cache.cend()
                || iterator.value().last_used_frame < oldest.value().last_used_frame)
            {
                oldest = iterator;
            }
        }

        if (oldest != this->cache.cend())
        {
            const QString oldest_key = oldest.key();
            releaseEntry(surface_backend, oldest_key);
            ++this->profile.evictions;
            page_index = free_page_index();
        }
    }

    if (page_index < 0)
    {
        ++this->profile.capacity_misses;
        return nullptr;
    }

    MapRhiGlobeTerrainHeightArrayPage &page =
        surface_backend.terrainHeightArrayPages()[page_index];
    if (!surface_backend.terrainHeightArrayPageReady(page_index)
        || page.free_layers.isEmpty())
    {
        ++this->profile.capacity_misses;
        return nullptr;
    }

    CacheEntry entry;
    entry.array_page = page_index;
    entry.array_layer = page.free_layers.takeLast();
    entry.last_used_frame = this->frame;
    const QHash<QString, CacheEntry>::iterator inserted =
        this->cache.insert(terrain_key, entry);
    return &inserted.value();
}

void MapRhiGlobeTerrainHeightCache::prepare(
    MapRhiGlobeSurfaceBackend &surface_backend,
    MapTerrainRepository *terrain_repository,
    QRhiResourceUpdateBatch *resource_updates,
    bool map_visible,
    const QVector<MapGlobeSurfaceTile> &window_tiles,
    const TileReadyCallback &on_tile_ready)
{
    if (terrain_repository == nullptr || !surface_backend.hasContext()
        || resource_updates == nullptr || !map_visible
        || window_tiles.isEmpty() || this->disabled)
    {
        return;
    }

    this->profile = ProfileCounters();
    this->profile.enabled = globeTerrainHeightCachePerformanceLog().isDebugEnabled();
    QElapsedTimer profile_timer;
    if (this->profile.enabled)
        profile_timer.start();

    QVector<QString> visible_terrain_keys;
    visible_terrain_keys.reserve(window_tiles.size());
    QSet<QString> protected_terrain_keys;
    protected_terrain_keys.reserve(window_tiles.size());
    for (const MapGlobeSurfaceTile &tile : window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;
        ++this->profile.visible_terrain_tiles;
        if (protected_terrain_keys.contains(tile.terrain_key))
            continue;
        protected_terrain_keys.insert(tile.terrain_key);
        visible_terrain_keys.append(tile.terrain_key);
    }
    this->profile.unique_visible_dem_tiles = visible_terrain_keys.size();
    if (visible_terrain_keys.isEmpty())
        return;

    if (!surface_backend.supportsTextureArrays() || !surface_backend.supportsR32fTextures())
    {
        this->disabled = true;
        if (this->profile.enabled)
        {
            qCDebug(globeTerrainHeightCachePerformanceLog).nospace()
                << "status=unsupported texture_arrays="
                << (surface_backend.supportsTextureArrays() ? 1 : 0)
                << " r32f="
                << (surface_backend.supportsR32fTextures() ? 1 : 0);
        }
        return;
    }

    ++this->frame;
    if (this->frame == 0)
        this->frame = 1;

    for (const QString &terrain_key : visible_terrain_keys)
    {
        QHash<QString, CacheEntry>::iterator cache_iterator =
            this->cache.find(terrain_key);
        const bool valid_assignment = cache_iterator != this->cache.end()
            && cache_iterator.value().array_page >= 0
            && cache_iterator.value().array_page
                < int(surface_backend.terrainHeightArrayPages().size())
            && cache_iterator.value().array_layer >= 0
            && cache_iterator.value().array_layer < GlobeTerrainHeightArrayLayerCount
            && surface_backend.terrainHeightArrayPageReady(
                cache_iterator.value().array_page);
        if (cache_iterator != this->cache.end() && !valid_assignment)
        {
            releaseEntry(surface_backend, terrain_key);
            cache_iterator = this->cache.end();
        }

        if (cache_iterator != this->cache.end())
        {
            cache_iterator.value().last_used_frame = this->frame;
            if (cache_iterator.value().uploaded)
            {
                ++this->profile.available_dem_tiles;
                ++this->profile.cache_hits;
                continue;
            }
        }

        const MapTerrainTile *terrain_tile = terrain_repository->tile(terrain_key);
        if (terrain_tile == nullptr
            || terrain_tile->elevations_m.size() != MapTerrainTileSampleCount
            || !mapGlobeTerrainDatumUsable(terrain_tile->vertical_datum))
        {
            continue;
        }
        ++this->profile.available_dem_tiles;

        if (this->profile.uploads >= GlobeTerrainHeightUploadBudgetPerFrame)
        {
            if (cache_iterator == this->cache.end())
                ++this->profile.cache_misses;
            ++this->profile.pending_uploads;
            continue;
        }

        CacheEntry *entry = nullptr;
        if (cache_iterator == this->cache.end())
        {
            ++this->profile.cache_misses;
            entry = ensureEntry(surface_backend, terrain_key, protected_terrain_keys);
        }
        else
        {
            entry = &cache_iterator.value();
        }

        if (entry == nullptr)
        {
            if (surface_backend.terrainHeightArrayPages().empty())
            {
                this->disabled = true;
                break;
            }
            continue;
        }

        const qsizetype byte_count =
            terrain_tile->elevations_m.size() * qsizetype(sizeof(float));
        const QByteArray raw_heights(
            reinterpret_cast<const char *>(terrain_tile->elevations_m.constData()),
            int(byte_count));
        if (!surface_backend.uploadTerrainHeightArrayPageLayerRaw(
                resource_updates, entry->array_page, entry->array_layer, raw_heights))
        {
            continue;
        }
        entry->uploaded = true;
        ++this->profile.uploads;
        this->profile.upload_bytes += quint64(byte_count);
    }

    for (const MapGlobeSurfaceTile &tile : window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;
        const QHash<QString, CacheEntry>::const_iterator iterator =
            this->cache.constFind(tile.terrain_key);
        if (iterator == this->cache.cend() || !iterator.value().uploaded)
            continue;

        ++this->profile.ready_terrain_tiles;
        if (on_tile_ready)
            on_tile_ready(tile, iterator.value().array_page, iterator.value().array_layer);
    }

    if (this->profile.enabled)
    {
        this->profile.cpu_ns = profile_timer.nsecsElapsed();
        reportProfile(surface_backend);
    }
}

void MapRhiGlobeTerrainHeightCache::reportProfile(
    const MapRhiGlobeSurfaceBackend &surface_backend) const
{
    if (!this->profile.enabled)
        return;
    if (this->profile.cache_misses <= 0 && this->profile.uploads <= 0
        && this->profile.pending_uploads <= 0 && this->profile.evictions <= 0
        && this->profile.capacity_misses <= 0)
    {
        return;
    }

    constexpr double NsecsPerMillisecond = 1000000.0;
    constexpr double BytesPerMebibyte = 1024.0 * 1024.0;
    const quint64 allocated_bytes =
        quint64(surface_backend.terrainHeightArrayPages().size())
        * quint64(GlobeTerrainHeightArrayLayerCount)
        * GlobeTerrainHeightTileBytes;
    const char *status = this->disabled
        ? "disabled"
        : (this->profile.capacity_misses > 0
            ? "capacity_limited"
            : (this->profile.pending_uploads > 0 ? "warming" : "ready"));
    qCDebug(globeTerrainHeightCachePerformanceLog).noquote().nospace()
        << "frame=" << this->frame
        << " visible_terrain_tiles=" << this->profile.visible_terrain_tiles
        << " unique_visible_dem_tiles=" << this->profile.unique_visible_dem_tiles
        << " available_dem_tiles=" << this->profile.available_dem_tiles
        << " ready_terrain_tiles=" << this->profile.ready_terrain_tiles
        << " cache_hits=" << this->profile.cache_hits
        << " cache_misses=" << this->profile.cache_misses
        << " uploads=" << this->profile.uploads
        << " pending_uploads=" << this->profile.pending_uploads
        << " evictions=" << this->profile.evictions
        << " capacity_misses=" << this->profile.capacity_misses
        << " resident_layers=" << this->cache.size()
        << " pages=" << surface_backend.terrainHeightArrayPages().size()
        << " allocated_mib="
        << QString::number(double(allocated_bytes) / BytesPerMebibyte, 'f', 3)
        << " upload_mib="
        << QString::number(
               double(this->profile.upload_bytes) / BytesPerMebibyte, 'f', 3)
        << " cpu_ms="
        << QString::number(double(this->profile.cpu_ns) / NsecsPerMillisecond, 'f', 3)
        << " max_pages=" << GlobeTerrainHeightArrayMaximumPageCount
        << " status=" << status;
}
