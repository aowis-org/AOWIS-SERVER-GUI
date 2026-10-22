#include "map/bgfx/map_bgfx_globe_height_cache.h"

#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
void require(bool condition, const char *description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}
}

int main()
{
    MapBgfxGlobeHeightCache cache;
    int samples = 0;
    double current = 123.0;
    const std::function<bool(double *)> sample = [&samples, &current](double *height)
    {
        ++samples;
        *height = current;
        return true;
    };
    double height = 0.0;
    cache.synchronize(10, 1.0);
    require(cache.height(9.0, 47.0, &height, sample) && height == 123.0,
            "initial drawn-terrain query");
    current = 456.0;
    require(cache.height(9.0, 47.0, &height, sample) && height == 123.0 && samples == 1,
            "camera frames reuse stable terrain, not screen projection");
    cache.synchronize(10, 1.0);
    require(cache.height(9.0, 47.0, &height, sample) && samples == 1,
            "same terrain revision does not invalidate cache");
    cache.synchronize(11, 1.0);
    require(cache.height(9.0, 47.0, &height, sample) && height == 456.0 && samples == 2,
            "new visible terrain invalidates height");
    current = 789.0;
    cache.synchronize(11, 2.0);
    require(cache.height(9.0, 47.0, &height, sample) && height == 789.0 && samples == 3,
            "vertical exaggeration invalidates height");
    int missing_calls = 0;
    const std::function<bool(double *)> missing = [&missing_calls](double *)
    {
        ++missing_calls;
        return false;
    };
    require(!cache.height(9.1, 47.1, &height, missing)
            && !cache.height(9.1, 47.1, &height, missing) && missing_calls == 2,
            "unavailable terrain is retried rather than cached flat");
    require(cache.height(9.1, 47.1, &height, sample) && height == 789.0,
            "terrain arriving after missing sample takes effect");
    require(cache.hits() == 2 && cache.samples() == 6,
            "cache telemetry distinguishes reused and sampled heights");
    cache.clear();
    require(cache.size() == 0, "explicit disposal clears retained terrain heights");
    cache.synchronize(11, 2.0);
    require(cache.height(9.0, 47.0, &height, sample) && samples == 5,
            "backend restart invalidates even with same revision");
    require(cache.hits() == 2 && cache.samples() == 7,
            "cache invalidation does not reset cumulative telemetry");
    if (failures == 0)
        std::puts("All globe terrain-height cache tests passed.");
    return failures == 0 ? 0 : 1;
}
