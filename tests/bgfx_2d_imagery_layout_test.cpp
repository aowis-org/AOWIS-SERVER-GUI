#include "map/bgfx/map_bgfx_2d_imagery_layout.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <tuple>

namespace
{
using MapBgfx2dImagery::Camera;
using MapBgfx2dImagery::Layout;
using MapBgfx2dImagery::Quad;
using MapBgfx2dImagery::TileId;

int g_failures = 0;

void require(bool valid, const char *reason)
{
    if (!valid)
    {
        std::fprintf(stderr, "bgfx_2d_imagery_layout: %s\n", reason);
        ++g_failures;
    }
}

bool allAvailable(const TileId &)
{
    return true;
}

Camera camera(double center_x, double center_y, double zoom)
{
    Camera result;
    result.center_x = center_x;
    result.center_y = center_y;
    result.zoom = zoom;
    result.viewport_width = 1280.0;
    result.viewport_height = 720.0;
    return result;
}

// Screen position of a normalized world point as drawn by the layout: find
// the target quad whose tile contains the point and interpolate inside it.
bool drawnScreenPosition(const Layout &layout, double world_x, double world_y,
                         double *screen_x, double *screen_y)
{
    const int count = 1 << layout.imagery_zoom;
    const int tile_x = int(std::floor(world_x * count));
    const int tile_y = int(std::floor(world_y * count));
    for (const Quad &quad : layout.quads)
    {
        if (quad.tile.zoom != layout.imagery_zoom || quad.tile.y != tile_y
            || quad.tile.x != MapBgfx2dImagery::wrapTileX(tile_x, layout.imagery_zoom))
        {
            continue;
        }
        const double fx = world_x * count - tile_x;
        const double fy = world_y * count - tile_y;
        *screen_x = quad.left + fx * (quad.right - quad.left);
        *screen_y = quad.top + fy * (quad.bottom - quad.top);
        return true;
    }
    return false;
}
}

int main()
{
    // 1. Full coverage when all tiles are available, no fallbacks/requests.
    {
        const Layout layout = MapBgfx2dImagery::buildLayout(
            camera(0.53, 0.37, 12.3), 1, 19, allAvailable);
        require(layout.imagery_zoom == 12, "imagery zoom must round the continuous zoom");
        require(layout.requests.empty(), "available tiles were requested");
        bool covered_everywhere = true;
        for (double sy = 0.5; sy < 720.0; sy += 37.0)
        {
            for (double sx = 0.5; sx < 1280.0; sx += 41.0)
            {
                bool covered = false;
                for (const Quad &quad : layout.quads)
                {
                    if (sx >= quad.left && sx < quad.right && sy >= quad.top && sy < quad.bottom)
                    {
                        covered = true;
                        break;
                    }
                }
                covered_everywhere = covered_everywhere && covered;
            }
        }
        require(covered_everywhere, "viewport not fully covered by target tiles");
        for (const Quad &quad : layout.quads)
        {
            require(quad.tile.zoom == 12 && quad.u0 == 0.0 && quad.u1 == 1.0,
                    "unexpected fallback quad with all tiles available");
        }
    }

    // 2. THE regression this slice exists for: a world point's drawn screen
    //    position is continuous in the zoom, also across the x.5 boundary
    //    where the imagery level switches. Analytic reference:
    //    screen = half_viewport + (world - centre) * 256 * 2^zoom.
    {
        const double world_x = 0.5302;
        const double world_y = 0.3701; // ~90-180 px below the centre over 12.2..12.8
        double maximum_error = 0.0;
        double previous_x = NAN;
        double maximum_step = 0.0;
        for (int step = 0; step <= 400; ++step)
        {
            const double zoom = 12.2 + 0.0015 * step; // 12.2 .. 12.8, crosses 12.5
            const Camera view = camera(0.53, 0.37, zoom);
            const Layout layout = MapBgfx2dImagery::buildLayout(view, 1, 19, allAvailable);
            double sx = 0.0;
            double sy = 0.0;
            if (!drawnScreenPosition(layout, world_x, world_y, &sx, &sy))
            {
                require(false, "reference point not drawn");
                break;
            }
            const double scale = 256.0 * std::exp2(zoom);
            const double expected_x = 640.0 + (world_x - 0.53) * scale;
            const double expected_y = 360.0 + (world_y - 0.37) * scale;
            maximum_error = std::max({maximum_error, std::abs(sx - expected_x),
                                      std::abs(sy - expected_y)});
            if (std::isfinite(previous_x))
                maximum_step = std::max(maximum_step, std::abs(sx - previous_x));
            previous_x = sx;
        }
        require(maximum_error < 1e-6, "imagery position deviates from the continuous camera");
        // At ~0.0015 octave per step the point moves < 1 px per step; a level
        // snap would move it by ~40 % of its distance from the centre.
        require(maximum_step < 1.0, "imagery jumps when the imagery level switches");
    }

    // 3. Horizontal wrap and vertical clamp at low zoom.
    {
        const Layout layout = MapBgfx2dImagery::buildLayout(
            camera(0.999, 0.5, 2.0), 1, 19, allAvailable);
        bool wrapped_ok = true;
        bool clamped_ok = true;
        for (const Quad &quad : layout.quads)
        {
            wrapped_ok = wrapped_ok && quad.tile.x >= 0 && quad.tile.x < 4;
            clamped_ok = clamped_ok && quad.tile.y >= 0 && quad.tile.y < 4;
        }
        require(wrapped_ok, "tile x not wrapped into [0, 2^zoom)");
        require(clamped_ok, "tile y outside the Mercator extent");
    }

    // 4. Ancestor fallback: only level 11 exists; level 12 tiles are drawn
    //    from the matching quarter of their parent and are requested.
    {
        const MapBgfx2dImagery::Availability only_parents = [](const TileId &tile)
        {
            return tile.zoom == 11;
        };
        const Layout layout = MapBgfx2dImagery::buildLayout(
            camera(0.53, 0.37, 12.2), 1, 19, only_parents);
        require(!layout.requests.empty(), "missing tiles were not requested");
        bool uv_ok = !layout.quads.empty();
        for (const Quad &quad : layout.quads)
        {
            uv_ok = uv_ok && quad.tile.zoom == 11
                && std::abs((quad.u1 - quad.u0) - 0.5) < 1e-12
                && std::abs((quad.v1 - quad.v0) - 0.5) < 1e-12
                && (quad.u0 == 0.0 || quad.u0 == 0.5)
                && (quad.v0 == 0.0 || quad.v0 == 0.5);
        }
        require(uv_ok, "ancestor fallback sub-rectangle is wrong");
        // Requests start with the tile under the centre.
        const TileId first = layout.requests.front().tile;
        require(first.x == int(std::floor(0.53 * 4096.0)) && first.y == int(std::floor(0.37 * 4096.0)),
                "requests are not ordered from the centre outwards");
    }

    // 5. Child fallback: only level 13 exists (zoomed out from 13 to 12).
    {
        const MapBgfx2dImagery::Availability only_children = [](const TileId &tile)
        {
            return tile.zoom == 13;
        };
        const Layout layout = MapBgfx2dImagery::buildLayout(
            camera(0.53, 0.37, 11.8), 1, 19, only_children);
        bool children_ok = !layout.quads.empty();
        for (const Quad &quad : layout.quads)
            children_ok = children_ok && quad.tile.zoom == 13;
        require(children_ok, "children were not used as zoom-out fallback");
    }

    // 6. The request-batch key is stable within a pan inside one tile and
    //    changes with the imagery level.
    {
        const Layout a = MapBgfx2dImagery::buildLayout(camera(0.53, 0.37, 12.2), 1, 19, allAvailable);
        const Layout b = MapBgfx2dImagery::buildLayout(
            camera(0.53 + 0.01 / 4096.0, 0.37, 12.2), 1, 19, allAvailable);
        const Layout c = MapBgfx2dImagery::buildLayout(camera(0.53, 0.37, 12.7), 1, 19, allAvailable);
        require(a.layout_key == b.layout_key, "layout key changed without a tile set change");
        require(a.layout_key != c.layout_key, "layout key did not change with the imagery level");
    }

    // 7. Invalid camera input produces nothing instead of garbage.
    {
        const Layout layout = MapBgfx2dImagery::buildLayout(
            camera(NAN, 0.5, 12.0), 1, 19, allAvailable);
        require(layout.quads.empty() && layout.requests.empty(), "invalid camera produced quads");
    }

    if (g_failures != 0)
        return 1;
    std::puts("bgfx 2D imagery layout passed");
    return 0;
}
