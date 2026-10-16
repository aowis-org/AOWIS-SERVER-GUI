#include "map/bgfx/map_bgfx_hud.h"
#include "map/bgfx/map_bgfx_hud_canvas.h"
#include "map/bgfx/map_bgfx_hud_text_cache.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QGuiApplication>
#include <QImage>
#include <QLocale>
#include <QRectF>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>

// Native bgfx HUD (Phase 1 slice 6a) without a GPU: the draw-list canvas,
// the text/image atlas (caching, packing, dirty regions, eviction, device
// pixel ratio) and the HUD layout/input contract (which buttons exist where,
// what they consume, Cancel pending, scale sampling, GPS dot).
namespace
{
int fails = 0;

void req(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++fails;
    }
}

constexpr double ViewportWidth = 800.0;
constexpr double ViewportHeight = 600.0;

struct ScaleProbe
{
    int calls = 0;
    double lon0 = 0.0;
    double lat0 = 0.0;
    double lon1 = 0.0;
    double lat1 = 0.0;
    double bar_px = 0.0;
    bool valid = true;
};

MapBgfxHud::FrameInputs frameInputs(bool two_d)
{
    MapBgfxHud::FrameInputs inputs;
    inputs.viewport = QSizeF(ViewportWidth, ViewportHeight);
    inputs.two_d = two_d;
    inputs.camera = MapBgfx2d::cameraFor(9.0, 47.0, 15.0, ViewportWidth, ViewportHeight);
    inputs.time_s = 1.25;
    return inputs;
}

// Clicks (press + release at the same point) on a grid over the viewport
// and records where each action fired. Clicking a Cancel marks it pending,
// exactly like a user click.
struct ActionScan
{
    QHash<int, QRectF> bounds;

    bool found(MapBgfxHud::Action action) const
    {
        return this->bounds.contains(static_cast<int>(action));
    }
    QRectF where(MapBgfxHud::Action action) const
    {
        return this->bounds.value(static_cast<int>(action));
    }
};

ActionScan scanActions(MapBgfxHud *hud, double step)
{
    ActionScan scan;
    for (double y = 0.5; y < ViewportHeight; y += step)
    {
        for (double x = 0.5; x < ViewportWidth; x += step)
        {
            hud->mousePress(x, y);
            bool consumed = false;
            const MapBgfxHud::Action action = hud->mouseRelease(x, y, &consumed);
            if (action == MapBgfxHud::Action::None)
                continue;
            const int key = static_cast<int>(action);
            const QRectF point(x, y, 0.001, 0.001);
            scan.bounds.insert(key, scan.bounds.contains(key)
                ? scan.bounds.value(key).united(point) : point);
        }
    }
    return scan;
}

bool canvasHasColorNear(const MapBgfxHudCanvas &canvas, quint32 abgr, double x, double y,
                        double radius)
{
    for (const MapBgfxHudVertex &vertex : canvas.vertices())
    {
        if (vertex.abgr == abgr && std::fabs(vertex.x - x) <= radius
            && std::fabs(vertex.y - y) <= radius)
        {
            return true;
        }
    }
    return false;
}

void testCanvas()
{
    // Premultiplied ABGR (bgfx Color0 as 4 x uint8 normalized, little endian).
    req(MapBgfxHudCanvas::premultipliedAbgr(qRgba(255, 0, 0, 128)) == 0x80000080u,
        "half transparent red premultiplies to 0x80000080");
    req(MapBgfxHudCanvas::premultipliedAbgr(qRgba(10, 20, 30, 255)) == 0xff1e140au,
        "opaque colour keeps its channels in ABGR order");
    req(MapBgfxHudCanvas::premultipliedAbgr(qRgba(200, 100, 50, 0)) == 0u,
        "fully transparent colour is all zero");

    MapBgfxHudCanvas canvas;
    canvas.setWhiteTexel(0.25f, 0.5f);
    canvas.fillRect(MapBgfxHudRect{10.0, 20.0, 30.0, 40.0}, qRgb(1, 2, 3));
    req(canvas.vertices().size() == 4, "fillRect emits one quad");
    req(canvas.indices().size() == 6, "fillRect emits two triangles");
    bool corners_ok = canvas.vertices().size() == 4;
    for (const MapBgfxHudVertex &vertex : canvas.vertices())
    {
        corners_ok = corners_ok && (vertex.x == 10.0f || vertex.x == 40.0f)
            && (vertex.y == 20.0f || vertex.y == 60.0f)
            && vertex.u == 0.25f && vertex.v == 0.5f;
    }
    req(corners_ok, "fillRect corners are the rect, solid fills sample the white texel");
    for (const quint32 index : canvas.indices())
        req(index < 4, "fillRect indices stay inside its vertices");

    canvas.fillRect(MapBgfxHudRect{0.0, 0.0, 0.0, 5.0}, qRgb(1, 2, 3));
    req(canvas.vertices().size() == 4, "an empty rect emits nothing");

    // Sprites are placed by their layout origin (text padding).
    canvas.clear();
    MapBgfxHudSprite sprite;
    sprite.valid = true;
    sprite.u0 = 0.1f;
    sprite.v0 = 0.2f;
    sprite.u1 = 0.3f;
    sprite.v1 = 0.4f;
    sprite.width = 12.0;
    sprite.height = 8.0;
    sprite.origin_x = 1.0;
    canvas.sprite(50.0, 60.0, sprite, 0xffffffffu);
    req(canvas.vertices().size() == 4 && canvas.vertices().at(0).x == 49.0f
            && canvas.vertices().at(0).y == 60.0f && canvas.vertices().at(2).x == 61.0f
            && canvas.vertices().at(2).y == 68.0f,
        "sprite quad is shifted by its origin and has its logical size");
    req(canvas.vertices().at(0).u == 0.1f && canvas.vertices().at(2).v == 0.4f,
        "sprite quad carries its atlas rectangle");
    MapBgfxHudSprite invalid;
    canvas.sprite(0.0, 0.0, invalid, 0xffffffffu);
    req(canvas.vertices().size() == 4, "an invalid sprite emits nothing");

    // Rounded rects and circles are convex fans: indices stay in range.
    canvas.clear();
    canvas.fillRoundedRect(MapBgfxHudRect{0.0, 0.0, 100.0, 30.0}, 4.0, qRgb(9, 9, 9));
    canvas.strokeRoundedRect(MapBgfxHudRect{0.0, 0.0, 100.0, 30.0}, 4.0, 1.0, qRgb(9, 9, 9));
    canvas.fillCircle(50.0, 50.0, 5.0, qRgb(255, 0, 0));
    canvas.strokeCircle(50.0, 50.0, 5.5, 1.0, qRgb(0, 0, 0));
    canvas.line(0.0, 0.0, 10.0, 0.0, 2.0, qRgb(0, 0, 0));
    bool indices_in_range = canvas.indices().size() % 3 == 0;
    for (const quint32 index : canvas.indices())
        indices_in_range = indices_in_range && int(index) < canvas.vertices().size();
    req(indices_in_range, "shape indices form triangles inside the vertex list");
    bool line_ok = false;
    for (const MapBgfxHudVertex &vertex : canvas.vertices())
    {
        if (vertex.x == 10.0f && std::fabs(vertex.y) == 1.0f)
            line_ok = true;
    }
    req(line_ok, "a 2 px horizontal line extends 1 px to each side");

    // 32-bit indices: more than 65535 vertices are fine (editor overlay);
    // beyond the transient budget a list is reported and never drawn.
    canvas.clear();
    while (canvas.vertices().size() <= 70000)
        canvas.fillCircle(1.0, 1.0, 50.0, qRgb(1, 1, 1));
    bool large_indices_ok = !canvas.overflowed();
    for (const quint32 index : canvas.indices())
        large_indices_ok = large_indices_ok && qsizetype(index) < canvas.vertices().size();
    req(large_indices_ok, "lists beyond 65535 vertices keep valid 32-bit indices");
    while (canvas.vertices().size() <= MapBgfxHudCanvas::MaximumVertices)
        canvas.fillCircle(1.0, 1.0, 50.0, qRgb(1, 1, 1));
    req(canvas.overflowed(), "a list beyond the transient budget reports overflow");
    canvas.clear();
    req(!canvas.overflowed() && canvas.vertices().isEmpty() && canvas.indices().isEmpty(),
        "clear() empties the list");
}

QRect spritePixels(const MapBgfxHudSprite &sprite, const QImage &atlas)
{
    const int x0 = int(std::lround(double(sprite.u0) * atlas.width()));
    const int y0 = int(std::lround(double(sprite.v0) * atlas.height()));
    const int x1 = int(std::lround(double(sprite.u1) * atlas.width()));
    const int y1 = int(std::lround(double(sprite.v1) * atlas.height()));
    return QRect(QPoint(x0, y0), QPoint(x1 - 1, y1 - 1));
}

void testTextCache()
{
    QFont font;
    font.setPixelSize(13);

    MapBgfxHudTextCache cache(256);
    cache.setLogicalDpi(96.0);
    req(cache.atlas().format() == QImage::Format_RGBA8888_Premultiplied,
        "atlas is RGBA8 premultiplied (uploaded as bgfx RGBA8 as-is)");
    req(cache.dirty() && cache.dirtyRect() == cache.atlas().rect(),
        "a fresh atlas needs a full upload");
    const int white_x = int(cache.whiteU() * float(cache.atlas().width()));
    const int white_y = int(cache.whiteV() * float(cache.atlas().height()));
    req(cache.atlas().pixel(white_x, white_y) == 0xffffffffu, "white texel is opaque white");
    cache.markUploaded();
    req(!cache.dirty(), "markUploaded() clears the dirty region");

    const MapBgfxHudSprite hello = cache.text(QStringLiteral("Hello"), font);
    req(hello.valid, "text run is stored");
    req(cache.dirty(), "a new text run dirties the atlas");
    const QRect hello_rect = spritePixels(hello, cache.atlas());
    req(cache.dirtyRect().contains(hello_rect), "dirty region covers the new run");
    cache.markUploaded();

    const MapBgfxHudSprite again = cache.text(QStringLiteral("Hello"), font);
    req(again.u0 == hello.u0 && again.v0 == hello.v0 && again.u1 == hello.u1,
        "the same run is served from the cache");
    req(!cache.dirty(), "a cached run does not dirty the atlas");

    const MapBgfxHudSprite world = cache.text(QStringLiteral("World"), font);
    const QRect world_rect = spritePixels(world, cache.atlas());
    req(world.valid && !world_rect.intersects(hello_rect), "different runs do not overlap");
    req(cache.dirtyRect() == world_rect, "only the new run is dirty");
    // Several runs before the next upload: the dirty region covers all.
    const MapBgfxHudSprite third = cache.text(QStringLiteral("Third"), font);
    req(cache.dirtyRect().contains(world_rect)
            && cache.dirtyRect().contains(spritePixels(third, cache.atlas())),
        "the dirty region accumulates until uploaded");

    QFont bold = font;
    bold.setBold(true);
    const MapBgfxHudSprite hello_bold = cache.text(QStringLiteral("Hello"), bold);
    req(hello_bold.u0 != hello.u0 || hello_bold.v0 != hello.v0,
        "the font is part of the cache key");

    // Rendered white, premultiplied, with real coverage.
    bool premultiplied = true;
    int covered = 0;
    for (int y = hello_rect.top(); y <= hello_rect.bottom(); ++y)
    {
        for (int x = hello_rect.left(); x <= hello_rect.right(); ++x)
        {
            const QRgb pixel = cache.atlas().pixel(x, y);
            premultiplied = premultiplied && qRed(pixel) <= qAlpha(pixel);
            if (qAlpha(pixel) > 128)
                ++covered;
        }
    }
    req(premultiplied, "text pixels are premultiplied");
    req(covered > 10, "text run has visible glyph coverage");
    const QSizeF advance = cache.measure(QStringLiteral("Hello"), font);
    req(hello.width >= advance.width() && hello.width <= advance.width() + 3.0,
        "run width is the advance plus the overhang padding");
    req(hello.origin_x > 0.0, "run origin skips the left padding");

    // Device pixel ratio 2: runs are rasterized at twice the resolution and
    // keep their logical size; changing it starts a new atlas.
    const quint64 generation = cache.generation();
    cache.setDevicePixelRatio(2.0);
    req(cache.generation() != generation, "a new device pixel ratio resets the atlas");
    const MapBgfxHudSprite hello_hidpi = cache.text(QStringLiteral("Hello"), font);
    const QRect hidpi_rect = spritePixels(hello_hidpi, cache.atlas());
    req(std::fabs(hello_hidpi.width - hello.width) < 0.01,
        "a run keeps its logical width at any ratio");
    req(std::abs(hidpi_rect.width() - int(std::ceil(hello.width * 2.0))) <= 1,
        "a ratio-2 run has twice the pixels");
    // The glyphs are drawn at that ratio too (not into the top-left quarter).
    int covered_right_half = 0;
    for (int y = hidpi_rect.top(); y <= hidpi_rect.bottom(); ++y)
    {
        for (int x = hidpi_rect.left() + hidpi_rect.width() / 2 + 2; x <= hidpi_rect.right(); ++x)
        {
            if (qAlpha(cache.atlas().pixel(x, y)) > 128)
                ++covered_right_half;
        }
    }
    req(covered_right_half > 10, "a ratio-2 run is rasterized at twice the size");
    cache.setDevicePixelRatio(2.0);
    const MapBgfxHudSprite hello_cached = cache.text(QStringLiteral("Hello"), font);
    req(hello_cached.u0 == hello_hidpi.u0, "setting the same ratio keeps the atlas");

    // Images: scaled to the physical size of their logical size.
    QImage icon(64, 64, QImage::Format_ARGB32);
    icon.fill(qRgba(0, 0, 255, 255));
    const MapBgfxHudSprite image = cache.image(QStringLiteral("icon"), icon, QSizeF(25.0, 25.0));
    const QRect image_rect = spritePixels(image, cache.atlas());
    req(image.valid && image.width == 25.0 && image_rect.width() == 50,
        "an image is stored at logical size x ratio");
    req(qBlue(cache.atlas().pixel(image_rect.center())) == 255, "image pixels are copied");

    // Full atlas: cleared, flagged for the frame, and the request still served.
    MapBgfxHudTextCache small(64);
    small.beginFrame();
    bool evicted = false;
    for (int index = 0; index < 64 && !evicted; ++index)
    {
        small.text(QStringLiteral("%1").arg(index * 1000), font);
        evicted = small.evictedSinceFrameStart();
    }
    req(evicted, "a full atlas is cleared and reports the eviction");
    req(small.text(QStringLiteral("after"), font).valid, "requests after an eviction succeed");
    small.beginFrame();
    req(!small.evictedSinceFrameStart(), "beginFrame() resets the eviction flag");
}

void testHud()
{
    QFont font;
    font.setPixelSize(13);
    MapBgfxHud hud;
    hud.setFont(font);
    ScaleProbe probe;
    hud.setScaleProvider([&probe](double lon0, double lat0, double lon1, double lat1,
                                  double bar_px)
    {
        ++probe.calls;
        probe.lon0 = lon0;
        probe.lat0 = lat0;
        probe.lon1 = lon1;
        probe.lat1 = lat1;
        probe.bar_px = bar_px;
        MapBgfxHud::ScaleBar result;
        result.valid = probe.valid;
        result.width_px = 80.0;
        result.label = QStringLiteral("100 m");
        return result;
    });
    QImage crosshair(60, 60, QImage::Format_ARGB32);
    crosshair.fill(qRgba(255, 255, 255, 255));
    QImage fullscreen(32, 32, QImage::Format_ARGB32);
    fullscreen.fill(qRgba(0, 0, 0, 255));
    hud.setImages(crosshair, fullscreen, fullscreen);
    hud.setViewMode(true, false);

    MapBgfxHudTextCache cache(1024);
    MapBgfxHudCanvas canvas;
    const MapBgfxHud::Palette palette;
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(!canvas.vertices().isEmpty() && !canvas.overflowed(), "HUD frame produces a draw list");

    // Scale: sampled through the viewport centre over the bar width.
    req(probe.calls == 1, "scale is sampled once per build");
    req(std::fabs(probe.bar_px - (MapBgfxHud::ScaleWidthPx - 2.0 * MapBgfxHud::ScalePaddingPx)) < 1e-9,
        "scale sample spans the maximum bar width");
    req(std::fabs(probe.lat0 - probe.lat1) < 1e-12 && probe.lon1 > probe.lon0,
        "scale sample is a horizontal line");
    // 1e-6 deg is far below one pixel row at zoom 15 (~1.3e-5 deg).
    req(std::fabs(probe.lat0 - 47.0) < 1e-6 && std::fabs(0.5 * (probe.lon0 + probe.lon1) - 9.0) < 1e-6,
        "scale sample is centred on the camera");
    // At zoom 15 the 104 px sample spans 104 / (256 * 2^15) of the world.
    const double expected_span = 104.0 / (256.0 * std::exp2(15.0)) * 360.0;
    req(std::fabs((probe.lon1 - probe.lon0) - expected_span) < 1e-9,
        "scale sample uses the continuous camera");
    bool scale_drawn = false;
    for (const MapBgfxHudVertex &vertex : canvas.vertices())
    {
        if (vertex.x >= MapBgfxHud::MarginPx - 1.0
            && vertex.x <= MapBgfxHud::MarginPx + MapBgfxHud::ScaleWidthPx + 1.0
            && vertex.y >= ViewportHeight - MapBgfxHud::MarginPx - MapBgfxHud::ScaleHeightPx - 1.0
            && vertex.y <= ViewportHeight - MapBgfxHud::MarginPx + 1.0)
        {
            scale_drawn = true;
        }
    }
    req(scale_drawn, "scale panel is bottom left");
    req(!hud.blocksInput(MapBgfxHud::MarginPx + 10.0, ViewportHeight - MapBgfxHud::MarginPx - 10.0),
        "the scale panel lets map input through");

    // Crosshair in the centre, untinted.
    req(canvasHasColorNear(canvas, 0xffffffffu, 0.5 * ViewportWidth - 30.0,
                           0.5 * ViewportHeight - 30.0, 0.5),
        "crosshair sprite is centred");

    // Without downloads and fullscreen only the 2D segment is clickable; 3D is
    // disabled until the bgfx globe exists.
    ActionScan scan = scanActions(&hud, 2.0);
    req(scan.found(MapBgfxHud::Action::ViewMode2d), "2D segment is clickable");
    req(!scan.found(MapBgfxHud::Action::ViewMode3d), "3D segment is disabled without a globe");
    req(!scan.found(MapBgfxHud::Action::ToggleFullscreen), "no fullscreen button unless available");
    req(!scan.found(MapBgfxHud::Action::CancelMapTiles), "no download panel without activity");
    const QRectF segment_2d = scan.where(MapBgfxHud::Action::ViewMode2d);
    req(segment_2d.left() >= MapBgfxHud::MarginPx && segment_2d.top() >= MapBgfxHud::MarginPx
            && segment_2d.right() < 120.0 && segment_2d.bottom() < 60.0,
        "view switch is top left");
    bool consumed = false;
    req(!hud.mousePress(0.5 * ViewportWidth, 0.5 * ViewportHeight),
        "a press on the map is not consumed");
    hud.mouseRelease(0.5 * ViewportWidth, 0.5 * ViewportHeight, &consumed);
    req(!consumed, "its release is not consumed either");

    // Press on a button, drag off, release: consumed, but no action.
    const double button_x = segment_2d.center().x();
    const double button_y = segment_2d.center().y();
    req(hud.mousePress(button_x, button_y), "a press on a button is consumed");
    bool over_button = false;
    req(hud.mouseMove(300.0, 300.0, &over_button), "moves are consumed while a HUD press is held");
    req(!over_button, "the map is not a button");
    req(hud.mouseRelease(300.0, 300.0, &consumed) == MapBgfxHud::Action::None && consumed,
        "releasing off the button cancels the click but stays consumed");
    req(!hud.mouseMove(button_x, button_y, &over_button) && over_button,
        "hovering an enabled button reports it");
    req(hud.blocksInput(MapBgfxHud::MarginPx + 2.0, MapBgfxHud::MarginPx + 2.0),
        "the view-switch frame blocks map input");
    // 3D: inside the frame, not a button.
    const double segment_3d_x = segment_2d.right() + 20.0;
    hud.mouseMove(segment_3d_x, button_y, &over_button);
    req(!over_button, "a disabled segment is no button");
    req(hud.mousePress(segment_3d_x, button_y), "a press on the disabled segment is still consumed");
    req(hud.mouseRelease(segment_3d_x, button_y, &consumed) == MapBgfxHud::Action::None,
        "a disabled segment never acts");

    // Globe available: 3D becomes clickable.
    hud.setViewMode(true, true);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    scan = scanActions(&hud, 2.0);
    req(scan.found(MapBgfxHud::Action::ViewMode3d)
            && scan.where(MapBgfxHud::Action::ViewMode3d).left()
                > scan.where(MapBgfxHud::Action::ViewMode2d).right(),
        "3D segment follows the 2D segment once a globe exists");
    hud.setViewMode(true, false);

    // Fullscreen, top right, 35 x 35 frame.
    hud.setFullscreen(true, false);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    scan = scanActions(&hud, 1.0);
    const QRectF fullscreen_button = scan.where(MapBgfxHud::Action::ToggleFullscreen);
    req(scan.found(MapBgfxHud::Action::ToggleFullscreen), "fullscreen button is clickable");
    req(fullscreen_button.left() >= ViewportWidth - MapBgfxHud::MarginPx - 35.0
            && fullscreen_button.right() <= ViewportWidth - MapBgfxHud::MarginPx
            && fullscreen_button.top() >= MapBgfxHud::MarginPx
            && fullscreen_button.bottom() <= MapBgfxHud::MarginPx + 35.0,
        "fullscreen button sits in the top-right frame");
    req(std::fabs(fullscreen_button.width() - 26.0) <= 1.01, "fullscreen button is 27 px");

    // Download activity: top centre, one panel per source, tiles first.
    hud.setDownloadActivity(2, 3, 1, 0);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    scan = scanActions(&hud, 1.0);
    req(scan.found(MapBgfxHud::Action::CancelMapTiles) && scan.found(MapBgfxHud::Action::CancelTerrain),
        "both download panels offer Cancel");
    const QRectF cancel_tiles = scan.where(MapBgfxHud::Action::CancelMapTiles);
    const QRectF cancel_terrain = scan.where(MapBgfxHud::Action::CancelTerrain);
    req(cancel_tiles.right() < cancel_terrain.left(), "map tiles panel is left of terrain");
    req(cancel_tiles.top() >= MapBgfxHud::MarginPx && cancel_tiles.bottom() < 60.0,
        "download panels are at the top");
    req(hud.blocksInput(0.5 * ViewportWidth, MapBgfxHud::MarginPx + 3.0),
        "download panels block map input");

    // The scan clicked both Cancels: they stay disabled until fresh numbers.
    hud.build(&canvas, &cache, frameInputs(true), palette);
    scan = scanActions(&hud, 1.0);
    req(!scan.found(MapBgfxHud::Action::CancelMapTiles) && !scan.found(MapBgfxHud::Action::CancelTerrain),
        "a clicked Cancel stays disabled");
    hud.setDownloadActivity(1, 0, 1, 0);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    scan = scanActions(&hud, 1.0);
    req(scan.found(MapBgfxHud::Action::CancelMapTiles), "fresh activity re-enables Cancel");
    hud.setDownloadActivity(0, 0, 0, 0);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(!hud.blocksInput(0.5 * ViewportWidth, MapBgfxHud::MarginPx + 3.0),
        "idle repositories hide the download panels");

    // GPS dot at the camera centre (opaque red, premultiplied ABGR).
    hud.setGps(true, 9.0, 47.0);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(canvasHasColorNear(canvas, 0xff0000ffu, 0.5 * ViewportWidth, 0.5 * ViewportHeight,
                           MapBgfxHud::GpsRadiusPx + 0.01),
        "GPS dot is drawn at its projection");
    hud.setGps(false, 9.0, 47.0);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(!canvasHasColorNear(canvas, 0xff0000ffu, 0.5 * ViewportWidth, 0.5 * ViewportHeight,
                            MapBgfxHud::GpsRadiusPx + 0.01),
        "no GPS dot without a fix");

    // Globe mode: no 2D scale; an invalid scale hides the bar.
    const int calls_before = probe.calls;
    hud.build(&canvas, &cache, frameInputs(false), palette);
    req(probe.calls == calls_before, "no 2D scale in globe mode");
    probe.valid = false;
    hud.build(&canvas, &cache, frameInputs(true), palette);
    const int without_scale = canvas.vertices().size();
    probe.valid = true;
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(canvas.vertices().size() > without_scale, "an invalid scale draws no bar");

    // A nearly full atlas is cleared mid-build: the HUD rebuilds once, so
    // the final draw list only references the refilled atlas.
    // Count how many equally wide runs fit, then fill a fresh atlas with one
    // less: the HUD (crosshair, labels) cannot fit into what is left.
    int fitting_runs = 0;
    {
        MapBgfxHudTextCache counter(256);
        counter.beginFrame();
        while (!counter.evictedSinceFrameStart() && fitting_runs < 10000)
        {
            counter.text(QStringLiteral("filler %1").arg(fitting_runs, 4, 10, QLatin1Char('0')), font);
            if (!counter.evictedSinceFrameStart())
                ++fitting_runs;
        }
    }
    req(fitting_runs > 4 && fitting_runs < 10000, "a 256 px atlas holds a bounded number of runs");
    MapBgfxHudTextCache tight(256);
    for (int index = 0; index + 1 < fitting_runs; ++index)
        tight.text(QStringLiteral("filler %1").arg(index, 4, 10, QLatin1Char('0')), font);
    tight.beginFrame();
    const quint64 generation = tight.generation();
    hud.setDownloadActivity(5, 7, 3, 2);
    hud.build(&canvas, &tight, frameInputs(true), palette);
    req(!tight.evictedSinceFrameStart(), "the rebuild fits into the refilled atlas");
    req(tight.generation() != generation, "the nearly full atlas was refilled");
    scan = scanActions(&hud, 2.0);
    req(scan.found(MapBgfxHud::Action::CancelMapTiles), "hit boxes match the rebuilt frame");
}

// --- Slice 6b: map legend --------------------------------------------------------

QVector<QRgb> rampStops(int seed)
{
    QVector<QRgb> stops;
    for (int index = 0; index < 7; ++index)
        stops.append(qRgb(10 * index + seed, 200 - 20 * index, 30 + seed));
    return stops;
}

// Node / link / heatmap like EntityMapLegendHud (13 / 12 / 13 entries); the
// link group has a metric and therefore a ramp.
MapBgfxHud::Legend legendContent()
{
    MapBgfxHud::Legend legend;
    legend.visible = true;
    const int counts[3] = {13, 12, 13};
    for (int group = 0; group < 3; ++group)
    {
        MapBgfxHud::LegendGroup &content = legend.groups[std::size_t(group)];
        for (int index = 0; index < counts[group]; ++index)
        {
            content.items.append(QStringLiteral("Group %1 item %2 [unit]").arg(group).arg(index));
            content.values.append(100 * group + index);
        }
    }
    MapBgfxHud::LegendRamp &ramp = legend.groups[1].ramp;
    ramp.visible = true;
    ramp.minimum = 0.0;
    ramp.maximum = 12.5;
    ramp.unit = QStringLiteral("L/s");
    ramp.colors = rampStops(5);
    ramp.palette = 3;
    ramp.flipped = false;
    ramp.default_palette = 7;
    ramp.default_flipped = false;
    legend.groups[1].current = 4;
    for (int index = 0; index < 9; ++index)
    {
        MapBgfxHud::LegendPaletteChoice choice;
        choice.value = index;
        choice.name = QStringLiteral("Palette %1").arg(index);
        choice.colors = rampStops(index);
        choice.diverging = index >= 7;
        legend.palettes.append(choice);
    }
    return legend;
}

// The legend's geometry for the 800 x 600 test viewport, from the same
// constants as the HUD (combo height = text height + 8).
struct LegendLayout
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double combo_height = 0.0;
    double node_combo_y = 0.0;
    double link_combo_y = 0.0;
    double ramp_row_y = 0.0;
    double heat_combo_y = 0.0;
    MapBgfxHudRect bar;
};

LegendLayout legendLayout(const QFont &font, double viewport_height = ViewportHeight)
{
    LegendLayout layout;
    layout.combo_height = std::ceil(QFontMetricsF(font).height() + 8.0);
    layout.width = MapBgfxHud::LegendWidthPx;
    layout.height = 2.0 * MapBgfxHud::LegendPaddingPx + 3.0 * layout.combo_height
        + MapBgfxHud::LegendRampRowHeightPx + 3.0 * MapBgfxHud::LegendSpacingPx;
    layout.x = ViewportWidth - MapBgfxHud::MarginPx - layout.width;
    layout.y = viewport_height - MapBgfxHud::MarginPx - layout.height;
    layout.node_combo_y = layout.y + MapBgfxHud::LegendPaddingPx;
    layout.link_combo_y = layout.node_combo_y + layout.combo_height + MapBgfxHud::LegendSpacingPx;
    layout.ramp_row_y = layout.link_combo_y + layout.combo_height + MapBgfxHud::LegendSpacingPx;
    layout.heat_combo_y = layout.ramp_row_y + MapBgfxHud::LegendRampRowHeightPx
        + MapBgfxHud::LegendSpacingPx;
    layout.bar = MapBgfxHudRect{layout.x + MapBgfxHud::LegendPaddingPx + MapBgfxHud::LegendRampInsetXPx,
                                layout.ramp_row_y + MapBgfxHud::LegendRampInsetYPx,
                                layout.width - 2.0 * MapBgfxHud::LegendPaddingPx
                                    - 2.0 * MapBgfxHud::LegendRampInsetXPx,
                                MapBgfxHud::LegendRampHeightPx};
    return layout;
}

MapBgfxHud::Action click(MapBgfxHud *hud, double x, double y, MapBgfxHud::Command *command)
{
    hud->mouseMove(x, y, nullptr);
    hud->mousePress(x, y);
    bool consumed = false;
    return hud->mouseRelease(x, y, &consumed, command);
}

bool canvasHasColor(const MapBgfxHudCanvas &canvas, QRgb argb)
{
    const quint32 abgr = MapBgfxHudCanvas::premultipliedAbgr(argb);
    for (const MapBgfxHudVertex &vertex : canvas.vertices())
    {
        if (vertex.abgr == abgr)
            return true;
    }
    return false;
}

void testLegendFormatting()
{
    req(MapBgfxHud::formatLegendValue(0.0) == QStringLiteral("0.00"), "zero shows two decimals");
    req(MapBgfxHud::formatLegendValue(12.345) == QStringLiteral("12.3"), "three significant digits");
    req(MapBgfxHud::formatLegendValue(0.5) == QStringLiteral("0.500"), "small values keep three digits");
    req(MapBgfxHud::formatLegendValue(999.6) == QStringLiteral("1.00e+03"),
        "a value rounding to 1000 switches to scientific");
    req(MapBgfxHud::formatLegendValue(0.0004).contains(QStringLiteral("e-04")),
        "tiny values are scientific");
    req(MapBgfxHud::formatLegendValue(std::nan("")) == QStringLiteral("—"), "NaN is a dash");
}

void testGradient()
{
    MapBgfxHudCanvas canvas;
    QVector<quint32> stops;
    stops.append(qRgb(0, 0, 0));
    stops.append(qRgb(200, 100, 50));
    stops.append(qRgb(255, 255, 255));
    canvas.fillHorizontalGradient(MapBgfxHudRect{0.0, 0.0, 60.0, 10.0}, 4.0, stops);
    bool middle_stop = false;
    bool left_stop = false;
    bool in_range = true;
    for (const MapBgfxHudVertex &vertex : canvas.vertices())
    {
        if (std::fabs(vertex.x - 30.0f) < 1e-4f
            && vertex.abgr == MapBgfxHudCanvas::premultipliedAbgr(qRgb(200, 100, 50)))
            middle_stop = true;
        if (vertex.x < 0.01f && vertex.abgr == MapBgfxHudCanvas::premultipliedAbgr(qRgb(0, 0, 0)))
            left_stop = true;
        in_range = in_range && vertex.x >= -1e-4f && vertex.x <= 60.0001f
            && vertex.y >= -1e-4f && vertex.y <= 10.0001f;
    }
    req(middle_stop, "the middle stop colour sits exactly at the strip boundary");
    req(left_stop, "the left edge has the first stop colour");
    req(in_range, "gradient strips stay inside the rounded rect");
    bool indices_ok = canvas.indices().size() % 3 == 0;
    for (const quint32 index : canvas.indices())
        indices_ok = indices_ok && int(index) < canvas.vertices().size();
    req(indices_ok, "gradient strips are valid triangles");
    // A corner vertex inside the first strip interpolates between stops 0 and 1.
    const quint32 quarter = MapBgfxHudCanvas::interpolateArgb(qRgb(0, 0, 0), qRgb(200, 100, 50), 0.5);
    req(quarter == qRgb(100, 50, 25), "per-channel interpolation rounds like the ramp colours");
}

void testLegend()
{
    QFont font;
    font.setPixelSize(13);
    MapBgfxHud hud;
    hud.setFont(font);
    hud.setViewMode(true, false);
    MapBgfxHudTextCache cache(1024);
    MapBgfxHudCanvas canvas;
    MapBgfxHud::Palette palette;
    palette.tooltip = qRgb(250, 250, 200);
    const LegendLayout layout = legendLayout(font);

    // Hidden by default: nothing at the bottom right.
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(!hud.blocksInput(ViewportWidth - 20.0, ViewportHeight - 20.0), "no legend until it is set");

    hud.setLegend(legendContent());
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(hud.blocksInput(ViewportWidth - MapBgfxHud::MarginPx - 2.0,
                        ViewportHeight - MapBgfxHud::MarginPx - 2.0),
        "legend panel sits bottom right");
    req(hud.blocksInput(layout.x + 2.0, layout.y + 2.0)
            && !hud.blocksInput(layout.x - 2.0, layout.y + 2.0)
            && !hud.blocksInput(layout.x + 2.0, layout.y - 2.0),
        "legend is 380 px wide with the combo/ramp stack height");
    bool over_button = false;
    hud.mouseMove(layout.x + 100.0, layout.link_combo_y + 5.0, &over_button);
    req(over_button && hud.cursor() == MapBgfxHud::Cursor::PointingHand, "combos are buttons");
    req(hud.mouseWheel(layout.x + 100.0, layout.y + 3.0, -120.0), "wheel over the legend is consumed");
    req(!hud.mouseWheel(100.0, 300.0, -120.0), "wheel over the map is not");

    // Ramp: cross cursor, hover swatch with the ramp colour at the pointer.
    const double ramp_mid_x = layout.bar.x + 0.5 * layout.bar.width;
    const double ramp_mid_y = layout.bar.y + 0.5 * layout.bar.height;
    hud.mouseMove(ramp_mid_x, ramp_mid_y, &over_button);
    req(!over_button && hud.cursor() == MapBgfxHud::Cursor::Cross, "the ramp shows a cross cursor");
    hud.build(&canvas, &cache, frameInputs(true), palette);
    // Fraction 0.5 of 7 stops = stop 3 exactly.
    req(canvasHasColor(canvas, rampStops(5).at(3)), "hover swatch shows the ramp colour");

    // Tooltip after the QToolTip delay.
    MapBgfxHud::FrameInputs later = frameInputs(true);
    hud.build(&canvas, &cache, later, palette);
    req(!canvasHasColor(canvas, palette.tooltip), "no tooltip before the delay");
    later.time_s += MapBgfxHud::TooltipDelayS + 0.05;
    hud.build(&canvas, &cache, later, palette);
    req(canvasHasColor(canvas, palette.tooltip), "ramp tooltip after the delay");

    // Combo: press opens the list (above, it does not fit below), click an
    // item -> LegendVisual with the group and the metric value.
    MapBgfxHud::Command command;
    req(hud.mousePress(layout.x + 100.0, layout.link_combo_y + 5.0), "press on a combo is consumed");
    req(hud.popupOpen(), "press opens the combo list");
    bool consumed = false;
    req(hud.mouseRelease(layout.x + 100.0, layout.link_combo_y + 5.0, &consumed, &command)
            == MapBgfxHud::Action::None && consumed,
        "the opening press does not select anything");
    hud.build(&canvas, &cache, frameInputs(true), palette);
    const double item_height = std::ceil(QFontMetricsF(font).height() + 6.0);
    // Room above the combo: the list is tall enough for all 12 entries and
    // has no scroll bar (the rows reach the right edge).
    const double list_height = 12.0 * item_height + 2.0;
    const double list_top = layout.link_combo_y - list_height;
    const double list_right = layout.x + MapBgfxHud::LegendPaddingPx + layout.width
        - 2.0 * MapBgfxHud::LegendPaddingPx;
    req(hud.blocksInput(layout.x + 50.0, list_top + 2.0)
            && !hud.blocksInput(layout.x + 50.0, list_top - 2.0),
        "12 entries show all 12 rows above the combo");
    const double first_row_y = list_top + 1.0 + 0.5 * item_height;
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(click(&hud, list_right - 3.0, first_row_y + 2.0 * item_height, &command)
            == MapBgfxHud::Action::LegendVisual,
        "a list that fits has no scroll bar: rows reach its right edge");
    req(command.group == 1 && command.value == 102, "the command carries group and metric");
    req(!hud.popupOpen() && hud.legend().groups[1].current == 2, "the list closes, the combo shows it");
    hud.mousePress(layout.x + 100.0, layout.link_combo_y + 5.0);
    hud.mouseRelease(layout.x + 100.0, layout.link_combo_y + 5.0, &consumed, &command);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    hud.mouseWheel(layout.x + 50.0, first_row_y, -120.0);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(click(&hud, layout.x + 50.0, first_row_y, &command) == MapBgfxHud::Action::LegendVisual
            && command.value == 100,
        "the wheel does not scroll a list that fits");

    // Too little room on both sides (300 px map): the larger side, scrolled,
    // with a scroll bar on the right.
    {
        MapBgfxHud::FrameInputs small = frameInputs(true);
        small.viewport = QSizeF(ViewportWidth, 300.0);
        const LegendLayout compact = legendLayout(font, 300.0);
        MapBgfxHud::Legend reset_current = hud.legend();
        reset_current.groups[1].current = 0;
        const std::function<void()> reopen = [&]()
        {
            hud.closePopup();
            hud.setLegend(reset_current);
            hud.build(&canvas, &cache, small, palette);
            hud.mousePress(compact.x + 100.0, compact.link_combo_y + 5.0);
            hud.mouseRelease(compact.x + 100.0, compact.link_combo_y + 5.0, &consumed, &command);
            hud.build(&canvas, &cache, small, palette);
        };
        reopen();
        const int rows = int(std::floor((compact.link_combo_y - 2.0 - 2.0) / item_height));
        const int maximum = 12 - rows;
        const double small_top = compact.link_combo_y - (double(rows) * item_height + 2.0);
        const double small_first = small_top + 1.0 + 0.5 * item_height;
        const double bar_x = list_right - 1.0 - 6.0;
        req(rows > 2 && rows < 12 && small_top >= 2.0 && hud.blocksInput(compact.x + 50.0, small_top + 1.0)
                && !hud.blocksInput(compact.x + 50.0, small_top - 2.0),
            "an overflowing list fills the larger side");
        // Near the bottom of the track, below the thumb (scroll 0).
        req(click(&hud, bar_x, small_top + double(rows) * item_height - 3.0, &command)
                == MapBgfxHud::Action::None && hud.popupOpen(),
            "the scroll bar is no entry");
        // The press above paged down (the track below the thumb).
        hud.build(&canvas, &cache, small, palette);
        req(click(&hud, compact.x + 50.0, small_first, &command) == MapBgfxHud::Action::LegendVisual
                && command.value == 100 + std::min(maximum, rows - 1),
            "a click on the track pages down");

        reopen();
        req(hud.mouseWheel(compact.x + 50.0, small_first, -120.0), "wheel over an overflowing list");
        hud.build(&canvas, &cache, small, palette);
        req(click(&hud, compact.x + 50.0, small_first, &command) == MapBgfxHud::Action::LegendVisual
                && command.value == 100 + std::min(maximum, 3),
            "one notch scrolls three rows");

        // Drag the thumb from its top to far below the list: the end.
        reopen();
        const double grab_y = small_top + 1.0 + 2.0 + 2.0;
        req(hud.mousePress(bar_x, grab_y), "press on the thumb");
        hud.mouseMove(bar_x, grab_y + 1000.0, nullptr);
        hud.mouseRelease(bar_x, grab_y + 1000.0, &consumed, &command);
        hud.build(&canvas, &cache, small, palette);
        req(click(&hud, compact.x + 50.0, small_first, &command) == MapBgfxHud::Action::LegendVisual
                && command.value == 100 + maximum,
            "dragging the thumb to the bottom scrolls to the last page");
        hud.setLegend(reset_current);
        hud.build(&canvas, &cache, frameInputs(true), palette);
    }

    // Press outside an open popup closes it and is swallowed.
    hud.mousePress(layout.x + 100.0, layout.heat_combo_y + 5.0);
    hud.mouseRelease(layout.x + 100.0, layout.heat_combo_y + 5.0, &consumed, &command);
    req(hud.popupOpen(), "heatmap list open");
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(hud.mousePress(100.0, 100.0), "a press on the map closes the popup and is consumed");
    req(hud.mouseRelease(100.0, 100.0, &consumed, &command) == MapBgfxHud::Action::None && consumed,
        "its release is consumed too");
    req(!hud.popupOpen(), "the popup is closed");
    hud.mousePress(layout.x + 100.0, layout.node_combo_y + 5.0);
    hud.mouseRelease(layout.x + 100.0, layout.node_combo_y + 5.0, &consumed, &command);
    req(hud.closePopup() && !hud.popupOpen() && !hud.closePopup(), "Escape closes the popup once");

    // Palette menu: press on the ramp, then palette / flip / reset.
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(hud.mousePress(ramp_mid_x, ramp_mid_y) && hud.popupOpen(), "press on the ramp opens the menu");
    hud.mouseRelease(ramp_mid_x, ramp_mid_y, &consumed, &command);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    // Find the menu's buttons by scanning for actions (each click closes the
    // menu, so reopen before every probe).
    // Every hit with its command; the reference palette (3, unflipped) is
    // restored after each click.
    struct MenuHit
    {
        double x = 0.0;
        double y = 0.0;
        int value = 0;
        bool flipped = false;
    };
    QVector<MenuHit> hits;
    for (double y = 1.0; y < ViewportHeight; y += 3.0)
    {
        for (double x = 1.0; x < ViewportWidth; x += 6.0)
        {
            if (!hud.popupOpen())
            {
                hud.mousePress(ramp_mid_x, ramp_mid_y);
                hud.mouseRelease(ramp_mid_x, ramp_mid_y, &consumed, &command);
                hud.build(&canvas, &cache, frameInputs(true), palette);
            }
            hud.mouseMove(x, y, nullptr);
            hud.mousePress(x, y);
            const MapBgfxHud::Action action = hud.mouseRelease(x, y, &consumed, &command);
            if (action != MapBgfxHud::Action::LegendPalette)
                continue;
            hits.append(MenuHit{x, y, command.value, command.flipped});
            MapBgfxHud::Legend reference = hud.legend();
            reference.groups[1].ramp.palette = 3;
            reference.groups[1].ramp.flipped = false;
            hud.setLegend(reference);
        }
    }
    // Preview buttons give (value, false), Flip buttons (value, true); Reset
    // gives the default (7, false) and is the only hit left of the preview
    // column (its row spans the menu, the palette rows have labels there).
    double preview_left = ViewportWidth;
    for (const MenuHit &hit : hits)
    {
        if (!hit.flipped && hit.value != 7)
            preview_left = std::min(preview_left, hit.x);
    }
    QHash<int, QRectF> palette_hits;
    QHash<int, QRectF> flip_hits;
    bool reset_found = false;
    double reset_top = 0.0;
    for (const MenuHit &hit : hits)
    {
        const QRectF point(hit.x, hit.y, 0.001, 0.001);
        if (hit.value == 7 && !hit.flipped && hit.x < preview_left)
        {
            reset_top = reset_found ? std::min(reset_top, hit.y) : hit.y;
            reset_found = true;
            continue;
        }
        QHash<int, QRectF> &target = hit.flipped ? flip_hits : palette_hits;
        target.insert(hit.value, target.contains(hit.value)
            ? target.value(hit.value).united(point) : point);
    }
    req(reset_found, "the menu has a Reset row");
    req(reset_found && palette_hits.contains(8) && reset_top > palette_hits.value(8).bottom(),
        "Reset is below the palettes");
    int palette_buttons = 0;
    for (int value = 0; value < 9; ++value)
        palette_buttons += palette_hits.contains(value) ? 1 : 0;
    req(palette_buttons >= 8, "every palette has a preview button");
    req(flip_hits.contains(0) && flip_hits.contains(8), "every palette has a Flip button");
    req(flip_hits.contains(0) && palette_hits.contains(0)
            && flip_hits.value(0).left() > palette_hits.value(0).right(),
        "Flip sits right of the preview");
    req(palette_hits.contains(0) && palette_hits.contains(8)
            && palette_hits.value(8).top() > palette_hits.value(0).bottom(),
        "palettes are listed top to bottom");

    // Flip semantics: on the active palette it toggles, elsewhere it selects
    // the other palette flipped. Reset restores the default.
    MapBgfxHud::Legend reference = hud.legend();
    reference.groups[1].ramp.palette = 3;
    reference.groups[1].ramp.flipped = true;
    hud.setLegend(reference);
    hud.closePopup();
    hud.mousePress(ramp_mid_x, ramp_mid_y);
    hud.mouseRelease(ramp_mid_x, ramp_mid_y, &consumed, &command);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(flip_hits.contains(3), "flip of the active palette found");
    if (flip_hits.contains(3))
    {
        const QPointF at = flip_hits.value(3).center();
        req(click(&hud, at.x(), at.y(), &command) == MapBgfxHud::Action::LegendPalette
                && command.value == 3 && !command.flipped,
            "Flip on the flipped active palette unflips it");
        req(!hud.popupOpen(), "a menu choice closes the menu");
    }

    // Hiding the ramp closes its menu; hiding the legend closes everything.
    hud.mousePress(ramp_mid_x, ramp_mid_y);
    hud.mouseRelease(ramp_mid_x, ramp_mid_y, &consumed, &command);
    req(hud.popupOpen(), "menu open again");
    MapBgfxHud::Legend without_ramp = hud.legend();
    without_ramp.groups[1].ramp.visible = false;
    hud.setLegend(without_ramp);
    req(!hud.popupOpen(), "a hidden ramp closes its palette menu");
    MapBgfxHud::Legend hidden = hud.legend();
    hidden.visible = false;
    hud.setLegend(hidden);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(!hud.blocksInput(ViewportWidth - 20.0, ViewportHeight - 20.0), "a hidden legend draws nothing");

    // No finite values: no cross cursor, the message instead of labels.
    MapBgfxHud::Legend no_values = legendContent();
    no_values.groups[1].ramp.minimum = std::nan("");
    hud.setLegend(no_values);
    hud.build(&canvas, &cache, frameInputs(true), palette);
    hud.mouseMove(ramp_mid_x, ramp_mid_y, &over_button);
    req(hud.cursor() == MapBgfxHud::Cursor::Arrow, "a ramp without values has no cross cursor");

    // Out-of-range current index and mismatched lists are clamped.
    MapBgfxHud::Legend broken = legendContent();
    broken.groups[0].current = 99;
    broken.groups[2].values.resize(3);
    hud.setLegend(broken);
    req(hud.legend().groups[0].current == 12 && hud.legend().groups[2].items.size() == 3,
        "setLegend() clamps the current entry and trims parallel lists");
    hud.build(&canvas, &cache, frameInputs(true), palette);
    req(!canvas.overflowed(), "legend frame fits the draw list budget");
}
}

int main(int argc, char **argv)
{
    // Fonts need a GUI application; no window is ever shown.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    // Legend numbers follow the default locale (decimal comma in German);
    // the expectations below are written for the C locale.
    QLocale::setDefault(QLocale::c());

    testCanvas();
    testTextCache();
    testHud();
    testLegendFormatting();
    testGradient();
    testLegend();

    if (fails == 0)
        std::printf("bgfx HUD tests passed\n");
    return fails == 0 ? 0 : 1;
}
