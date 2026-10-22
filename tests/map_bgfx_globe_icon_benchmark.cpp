#include "map/bgfx/map_bgfx_globe_icon_index.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <vector>

// Stage 10: CPU-only workload comparison, not a bgfx/GPU, DEM-service, or
// browser frame benchmark. Deliberately exercises the production spatial index
// and the projection-and-depth-sort work which it avoids for distant anchors.
namespace
{
using Clock = std::chrono::steady_clock;
using Position = MapBgfxGlobeIconIndex::Position;
using Frustum = MapBgfxGlobeIconIndex::Frustum;
using Plane = MapBgfxGlobeIconIndex::Plane;

struct View
{
    double x = 0.0;
    double y = 0.0;
    double axis_x = 1.0;
    double axis_y = 0.0;
    double half_width = 1000.0;
    double half_height = 600.0;
    double relief_m = 150.0;
};

struct Projected
{
    double distance = 0.0;
    std::size_t id = 0;
};

struct Work
{
    std::size_t projected = 0;
    std::size_t visible = 0;
    double checksum = 0.0;
};

struct CaseResult
{
    std::size_t count = 0;
    const char *scenario = nullptr;
    int frames = 0;
    double build_ms = 0.0;
    double index_p50_ms = 0.0;
    double index_p95_ms = 0.0;
    double brute_p50_ms = 0.0;
    double brute_p95_ms = 0.0;
    double average_candidates = 0.0;
    double average_visible = 0.0;
    double average_examined = 0.0;
    std::size_t index_bytes = 0;
    std::size_t rebuilds_during_navigation = 0;
};

double elapsedMs(Clock::time_point first, Clock::time_point last)
{
    return std::chrono::duration<double, std::milli>(last - first).count();
}

double percentile(std::vector<double> measurements, double fraction)
{
    if (measurements.empty())
        return 0.0;
    std::sort(measurements.begin(), measurements.end());
    const std::size_t position = std::size_t(std::ceil(fraction * measurements.size()));
    return measurements[std::max(std::size_t(1), position) - 1];
}

std::vector<Position> makeNetwork(std::size_t count, double extent_m)
{
    std::vector<Position> result;
    result.reserve(count);
    const std::size_t columns = std::size_t(std::ceil(std::sqrt(double(count) * 1.6)));
    const std::size_t rows = (count + columns - 1) / columns;
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t col = i % columns;
        const std::size_t row = i / columns;
        // Deterministic, slightly perturbed distribution about an ECEF
        // origin. The z variation is not a modeled terrain-height query.
        const double x = (double(col) + 0.25 * std::sin(double(i) * 0.25))
            / double(columns) * extent_m - 0.5 * extent_m;
        const double y = (double(row) + 0.25 * std::cos(double(i) * 0.17))
            / double(rows) * extent_m - 0.5 * extent_m;
        result.push_back({4200000.0 + x, 850000.0 + y, 4700000.0});
    }
    return result;
}

View camera(int frame, int frames, double extent_m, bool all_visible)
{
    View view;
    const double phase = double(frame) / double(std::max(1, frames));
    const double angle = phase * 1.3;
    view.axis_x = std::cos(angle);
    view.axis_y = std::sin(angle);
    const double offset = extent_m * 0.18;
    view.x = 4200000.0 + offset * std::sin(phase * 6.283185307179586);
    view.y = 850000.0 + offset * std::cos(phase * 6.283185307179586);
    // Fixed physical camera footprint across regional and urban scenes:
    // density, not a rescaled camera, determines how many icons are visible.
    view.half_width = all_visible ? extent_m * 1.7
        : (frame % 3 == 0 ? 600.0 : (frame % 3 == 1 ? 1200.0 : 2400.0));
    view.half_height = view.half_width * 0.625;
    return view;
}

Frustum frustum(const View &view)
{
    Frustum result;
    result.valid = true;
    result.relief_allowance_m = view.relief_m;
    const double ux = view.axis_x;
    const double uy = view.axis_y;
    const double vx = -uy;
    const double vy = ux;
    result.sides = {{{ux, uy, 0.0, view.half_width - ux * view.x - uy * view.y},
                     {-ux, -uy, 0.0, view.half_width + ux * view.x + uy * view.y},
                     {vx, vy, 0.0, view.half_height - vx * view.x - vy * view.y},
                     {-vx, -vy, 0.0, view.half_height + vx * view.x + vy * view.y}}};
    return result;
}

bool inExpandedFrustum(const Position &position, const Frustum &view)
{
    for (const Plane &plane : view.sides)
    {
        const double normal = std::sqrt(plane.x * plane.x + plane.y * plane.y
                                        + plane.z * plane.z);
        const double signed_distance = plane.x * position.x + plane.y * position.y
            + plane.z * position.z + plane.offset;
        if (signed_distance < -view.relief_allowance_m * normal)
            return false;
    }
    return true;
}

Work projectAndSort(const std::vector<Position> &positions,
                    const std::vector<std::size_t> &indices, const View &view)
{
    Work result;
    std::vector<Projected> visible;
    visible.reserve(indices.size());
    for (std::size_t index : indices)
    {
        const Position &point = positions[index];
        const double dx = point.x - view.x;
        const double dy = point.y - view.y;
        const double x = dx * view.axis_x + dy * view.axis_y;
        const double y = -dx * view.axis_y + dy * view.axis_x;
        // Stand-in for CPU projection math (not the real DEM lookup). It is
        // deliberately executed for every supplied candidate, even offscreen.
        const double height = 55.0 * std::sin(point.x * 0.00013)
            * std::cos(point.y * 0.00017);
        const double distance = std::sqrt(dx * dx + dy * dy + (height + 500.0) * (height + 500.0));
        ++result.projected;
        if (std::abs(x) <= view.half_width && std::abs(y) <= view.half_height)
            visible.push_back({distance, index});
    }
    std::stable_sort(visible.begin(), visible.end(), [](const Projected &a, const Projected &b)
    {
        return a.distance != b.distance ? a.distance > b.distance : a.id < b.id;
    });
    result.visible = visible.size();
    for (const Projected &entry : visible)
        result.checksum += double(entry.id % 101) * 0.001 + entry.distance * 0.00000001;
    return result;
}

bool benchmark(std::size_t size, const char *scenario, double extent_m,
               bool all_visible, int frames, CaseResult *output)
{
    const std::vector<Position> positions = makeNetwork(size, extent_m);
    MapBgfxGlobeIconIndex index;
    const Clock::time_point started = Clock::now();
    index.rebuild(positions);
    const double build_ms = elapsedMs(started, Clock::now());
    const std::size_t built = index.buildCount();

    std::vector<std::size_t> full(size);
    std::iota(full.begin(), full.end(), std::size_t(0));
    std::vector<std::size_t> candidates;
    std::vector<double> indexed_ms;
    std::vector<double> brute_ms;
    std::size_t candidate_total = 0;
    std::size_t visible_total = 0;
    std::size_t examined_total = 0;
    for (int frame = 0; frame < frames; ++frame)
    {
        const View view = camera(frame, frames, extent_m, all_visible);
        const Frustum clipping = frustum(view);
        // Alternate measurement order to avoid consistently giving one path
        // the warmer cache. Index construction is timed separately above.
        MapBgfxGlobeIconIndex::QueryStats stats;
        Work culled;
        Work baseline;
        double indexed_duration = 0.0;
        double brute_duration = 0.0;
        const Clock::time_point first = Clock::now();
        if (frame % 2 == 0)
        {
            index.query(clipping, &candidates, &stats);
            culled = projectAndSort(positions, candidates, view);
            const Clock::time_point middle = Clock::now();
            baseline = projectAndSort(positions, full, view);
            const Clock::time_point end = Clock::now();
            indexed_duration = elapsedMs(first, middle);
            brute_duration = elapsedMs(middle, end);
        }
        else
        {
            baseline = projectAndSort(positions, full, view);
            const Clock::time_point middle = Clock::now();
            index.query(clipping, &candidates, &stats);
            culled = projectAndSort(positions, candidates, view);
            const Clock::time_point end = Clock::now();
            brute_duration = elapsedMs(first, middle);
            indexed_duration = elapsedMs(middle, end);
        }

        // Correctness must not depend on benchmark speed, camera orientation,
        // density, or the conservative terrain and icon margin allowance.
        if (culled.visible != baseline.visible
            || std::abs(culled.checksum - baseline.checksum) > 0.00001
            || index.buildCount() != built)
        {
            std::fprintf(stderr, "FAIL: icon visibility or retained index: %s %zu frame %d\n",
                         scenario, size, frame);
            return false;
        }
        if (frame == 0 || frame == frames / 2 || frame == frames - 1)
        {
            std::vector<std::size_t> expected;
            for (std::size_t i = 0; i < size; ++i)
                if (inExpandedFrustum(positions[i], clipping))
                    expected.push_back(i);
            if (expected != candidates)
            {
                std::fprintf(stderr, "FAIL: spatial query missed anchors: %s %zu frame %d\n",
                             scenario, size, frame);
                return false;
            }
        }
        indexed_ms.push_back(indexed_duration);
        brute_ms.push_back(brute_duration);
        candidate_total += candidates.size();
        visible_total += culled.visible;
        examined_total += stats.examined_points;
    }

    output->count = size;
    output->scenario = scenario;
    output->frames = frames;
    output->build_ms = build_ms;
    output->index_p50_ms = percentile(indexed_ms, 0.5);
    output->index_p95_ms = percentile(indexed_ms, 0.95);
    output->brute_p50_ms = percentile(brute_ms, 0.5);
    output->brute_p95_ms = percentile(brute_ms, 0.95);
    output->average_candidates = double(candidate_total) / frames;
    output->average_visible = double(visible_total) / frames;
    output->average_examined = double(examined_total) / frames;
    output->index_bytes = index.estimatedStorageBytes();
    output->rebuilds_during_navigation = index.buildCount() - built;
    return true;
}

void printReport(const CaseResult &result)
{
    std::printf("{\"kind\":\"aowis_synthetic_globe_editor_icon_benchmark\","
                "\"anchors\":%zu,\"scenario\":\"%s\",\"frames\":%d,"
                "\"index_build_ms\":%.6f,\"index_storage_bytes_estimate\":%zu,"
                "\"indexed_cpu_p50_ms\":%.6f,\"indexed_cpu_p95_ms\":%.6f,"
                "\"brute_force_cpu_p50_ms\":%.6f,\"brute_force_cpu_p95_ms\":%.6f,"
                "\"mean_candidates\":%.3f,\"mean_visible\":%.3f,"
                "\"mean_leaf_points_examined\":%.3f,\"navigation_index_rebuilds\":%zu}\n",
                result.count, result.scenario, result.frames, result.build_ms,
                result.index_bytes, result.index_p50_ms, result.index_p95_ms,
                result.brute_p50_ms, result.brute_p95_ms, result.average_candidates,
                result.average_visible, result.average_examined,
                result.rebuilds_during_navigation);
}
}

int main(int argc, char **argv)
{
    const bool smoke = argc == 2 && std::strcmp(argv[1], "--smoke") == 0;
    if (argc > 1 && !smoke)
    {
        std::fprintf(stderr, "Usage: %s [--smoke]\n", argv[0]);
        return 2;
    }
    const std::array<std::size_t, 3> sizes = smoke
        ? std::array<std::size_t, 3>{{100, 250, 500}}
        : std::array<std::size_t, 3>{{10000, 50000, 100000}};
    const int frames = smoke ? 4 : 90;
    for (std::size_t size : sizes)
    {
        const struct Scenario
        {
            const char *name;
            double extent;
            bool all_visible;
        } scenarios[] = {{"regional", 100000.0, false},
                         {"urban_dense", 4500.0, false},
                         {"fully_visible", 4500.0, true}};
        for (const Scenario &scenario : scenarios)
        {
            CaseResult result;
            if (!benchmark(size, scenario.name, scenario.extent, scenario.all_visible,
                           frames, &result))
                return 1;
            printReport(result);
        }
    }
    return 0;
}
