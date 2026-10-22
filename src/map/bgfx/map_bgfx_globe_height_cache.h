#ifndef MAP_BGFX_GLOBE_HEIGHT_CACHE_H
#define MAP_BGFX_GLOBE_HEIGHT_CACHE_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>

// Height lookups are expensive mesh-ray queries, but the drawn terrain is
// unchanged for most camera frames. Cache only successful finite samples;
// unavailable terrain must be retried when its data arrives. The caller
// supplies the revision of the *drawn* surface, not the camera revision.
// Screen positions and camera distances are deliberately never cached.
class MapBgfxGlobeHeightCache
{
public:
    void synchronize(std::uint64_t surface_revision, double vertical_exaggeration)
    {
        if (!this->source_valid || this->surface_revision != surface_revision
            || this->vertical_exaggeration != vertical_exaggeration)
        {
            this->heights.clear();
            this->surface_revision = surface_revision;
            this->vertical_exaggeration = vertical_exaggeration;
            this->source_valid = true;
        }
    }

    void clear()
    {
        this->heights.clear();
        this->source_valid = false;
    }

    // The sampler returns false when terrain is unavailable. Such a miss is
    // intentionally not stored: it may become available without an immediate
    // drawn-surface revision update, e.g. during an asynchronous tile load.
    template <typename Sampler>
    bool height(double longitude_deg, double latitude_deg, double *height_m,
                const Sampler &sample)
    {
        if (height_m == nullptr || !std::isfinite(longitude_deg)
            || !std::isfinite(latitude_deg))
            return false;
        const Key key{bits(longitude_deg), bits(latitude_deg)};
        const std::unordered_map<Key, double, Hash>::const_iterator found =
            this->heights.find(key);
        if (found != this->heights.end())
        {
            ++this->cache_hits;
            *height_m = found->second;
            return true;
        }
        double sampled_height_m = 0.0;
        ++this->terrain_samples;
        if (!sample(&sampled_height_m) || !std::isfinite(sampled_height_m))
            return false;
        this->heights.emplace(key, sampled_height_m);
        *height_m = sampled_height_m;
        return true;
    }

    std::size_t size() const { return this->heights.size(); }
    std::uint64_t hits() const { return this->cache_hits; }
    std::uint64_t samples() const { return this->terrain_samples; }

private:
    struct Key
    {
        std::uint64_t longitude = 0;
        std::uint64_t latitude = 0;
        bool operator==(const Key &other) const
        {
            return this->longitude == other.longitude && this->latitude == other.latitude;
        }
    };

    struct Hash
    {
        std::size_t operator()(const Key &key) const
        {
            const std::size_t a = std::hash<std::uint64_t>{}(key.longitude);
            const std::size_t b = std::hash<std::uint64_t>{}(key.latitude);
            return a ^ (b + std::size_t(0x9e3779b9u) + (a << 6) + (a >> 2));
        }
    };

    static std::uint64_t bits(double value)
    {
        // +0 and -0 represent the same coordinate; avoid separate entries.
        if (value == 0.0)
            value = 0.0;
        std::uint64_t result = 0;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    std::unordered_map<Key, double, Hash> heights;
    std::uint64_t surface_revision = 0;
    double vertical_exaggeration = 0.0;
    bool source_valid = false;
    std::uint64_t cache_hits = 0;
    std::uint64_t terrain_samples = 0;
};

#endif // MAP_BGFX_GLOBE_HEIGHT_CACHE_H
