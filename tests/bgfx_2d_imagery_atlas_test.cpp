#include "map/bgfx/map_bgfx_2d_imagery_atlas.h"
#include "map/bgfx/map_bgfx_globe_imagery.h"

#include <QSet>

#include <cmath>
#include <cstdio>

// 2D imagery tile atlas without a GPU (MapBgfx2dImageryGpu draws all tiles
// of a page with one draw call): where cells and gutters lie, the texture
// coordinates of a tile, pages sized for the budget, reuse of the least
// recently used cell, never evicting a tile the current frame draws. Also
// the globe's use of the atlas (map_bgfx_globe_imagery.h): a tile drawn from
// the covering part of an ancestor's cell.
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

constexpr int Budget = 384;
constexpr int Tile = 256;
constexpr int Cell = Tile + 2 * MapBgfx2dImageryAtlas::Gutter;

void testPlacement()
{
    MapBgfx2dImageryAtlas atlas;
    atlas.reset(4096, Budget);
    const MapBgfx2dImageryAtlas::Allocation first = atlas.allocate(1, Tile, Tile, 1);
    req(first.page == 0 && first.cell == 0 && first.page_created, "first tile: new page, cell 0");
    const MapBgfx2dImageryAtlas::Page &page = atlas.page(0);
    req(page.columns == 4096 / Cell && page.columns == 15, "15 cells of 258 per 4096 row");
    req(page.width == page.columns * Cell, "page width: whole cells");
    req(page.rows == 15 && page.height == 15 * Cell, "first page: as many rows as fit (budget 384)");

    const MapBgfx2dImageryAtlas::Allocation second = atlas.allocate(2, Tile, Tile, 1);
    req(second.page == 0 && second.cell == 1 && !second.page_created, "second tile: next cell");
    const MapBgfx2dImageryAtlas::Slot *slot = atlas.slot(2);
    req(slot != nullptr && atlas.tileOrigin(*slot) == QPoint(Cell + 1, 1), "tile origin past the gutter");
    // Tile coordinates map into the cell, inside the gutter.
    const double u0 = atlas.atlasU(*slot, 0.0) * page.width;
    const double u1 = atlas.atlasU(*slot, 1.0) * page.width;
    const double v0 = atlas.atlasV(*slot, 0.0) * page.height;
    const double v1 = atlas.atlasV(*slot, 1.0) * page.height;
    req(std::fabs(u0 - (Cell + 1)) < 1e-9 && std::fabs(u1 - (Cell + 1 + Tile)) < 1e-9,
        "u spans exactly the tile's texels");
    req(std::fabs(v0 - 1.0) < 1e-9 && std::fabs(v1 - (1.0 + Tile)) < 1e-9,
        "v spans exactly the tile's texels");
    const double half = atlas.atlasU(*slot, 0.5) * page.width;
    req(std::fabs(half - (Cell + 1 + 128)) < 1e-9, "ancestor sub-rectangles map linearly");

    // The same tile again keeps its cell.
    const MapBgfx2dImageryAtlas::Allocation again = atlas.allocate(2, Tile, Tile, 2);
    req(again.page == 0 && again.cell == 1 && atlas.tileCount() == 2, "re-upload overwrites its cell");
}

void testBudgetPagesAndReuse()
{
    MapBgfx2dImageryAtlas atlas;
    atlas.reset(4096, Budget);
    int created = 0;
    for (quint64 key = 1; key <= quint64(Budget); ++key)
    {
        const MapBgfx2dImageryAtlas::Allocation allocation = atlas.allocate(key, Tile, Tile, key);
        created += allocation.page_created ? 1 : 0;
    }
    req(created == 2 && atlas.pageCount() == 2, "budget of 384 tiles: two pages");
    req(atlas.page(1).rows == (Budget - 225 + 14) / 15, "second page only as high as the budget needs");
    // All cells distinct.
    QSet<qint64> cells;
    for (quint64 key = 1; key <= quint64(Budget); ++key)
    {
        const MapBgfx2dImageryAtlas::Slot *slot = atlas.slot(key);
        cells.insert(qint64(slot->page) * 100000 + slot->cell);
    }
    req(cells.size() == Budget, "every tile its own cell");

    // A budget of whole rows (225 + 10 x 15 cells): full pages.
    const int budget = 375;
    atlas.reset(4096, budget);
    for (quint64 key = 1; key <= quint64(budget); ++key)
        atlas.allocate(key, Tile, Tile, key);
    req(atlas.pageCount() == 2 && atlas.page(1).rows == 10, "whole-row budget: pages exactly full");

    // Full: the least recently used tile not drawn in this frame gives up
    // its cell, no new page.
    const quint64 frame = 1000;
    // Tiles 1..3 are drawn in this frame (marked before the uploads).
    for (quint64 key = 1; key <= 3; ++key)
        atlas.slot(key)->last_used_frame = frame;
    const MapBgfx2dImageryAtlas::Slot victim = *atlas.slot(4);
    const MapBgfx2dImageryAtlas::Allocation reused = atlas.allocate(5000, Tile, Tile, frame);
    req(!reused.page_created && atlas.pageCount() == 2, "full atlas reuses a cell");
    req(!atlas.contains(4) && atlas.contains(1) && atlas.contains(2) && atlas.contains(3),
        "oldest tile not drawn this frame is evicted, drawn ones stay");
    req(reused.page == victim.page && reused.cell == victim.cell, "the new tile takes the evicted cell");
    req(atlas.tileCount() == budget, "tile count stays at the budget");

    // A frame drawing every tile: nothing evictable, a page is added; the
    // trim after the frame brings the atlas back to the budget.
    for (quint64 key = 1; key <= quint64(budget) + 5000; ++key)
    {
        MapBgfx2dImageryAtlas::Slot *slot = atlas.slot(key);
        if (slot != nullptr)
            slot->last_used_frame = frame + 1;
    }
    const MapBgfx2dImageryAtlas::Allocation extra = atlas.allocate(6000, Tile, Tile, frame + 1);
    req(extra.page_created && extra.page == 2 && atlas.page(2).rows == 1,
        "over budget within a frame: one extra row page");
    QVector<int> dropped = atlas.trim(budget, frame + 1);
    req(atlas.tileCount() == budget + 1 && dropped.isEmpty(),
        "trim never evicts tiles of the current frame");
    dropped = atlas.trim(budget, frame + 2);
    req(atlas.tileCount() == budget, "next frame's trim gets back to the budget");

    // Emptied pages are dropped; their slots are reused by the next page.
    atlas.clear();
    req(atlas.pageCount() == 0 && atlas.tileCount() == 0, "clear drops everything");
    atlas.allocate(1, Tile, Tile, 1);
    atlas.allocate(2, 512, 512, 1);
    req(atlas.pageCount() == 2 && atlas.page(1).tile_width == 512, "other tile sizes get their own page");
    atlas.slot(1)->last_used_frame = 0;
    atlas.remove(2);
    dropped = atlas.trim(Budget, 5);
    req(dropped.size() == 1 && dropped.first() == 1 && atlas.pageCount() == 1,
        "empty page dropped (texture to destroy)");
    const MapBgfx2dImageryAtlas::Allocation slot_reuse = atlas.allocate(3, 128, 128, 6);
    req(slot_reuse.page_created && slot_reuse.page == 1, "a dropped page index is reused");
}

void testSmallLimits()
{
    // WebGL 2 minimum texture size: 2048 -> 7 x 7 cells per page.
    MapBgfx2dImageryAtlas atlas;
    atlas.reset(2048, Budget);
    atlas.allocate(1, Tile, Tile, 1);
    req(atlas.page(0).columns == 7 && atlas.page(0).rows == 7, "2048 limit: 7 x 7 cells");
    // A tile larger than a page does not fit.
    req(atlas.allocate(2, 4096, 4096, 1).page == -1, "oversized tile rejected");
    req(!atlas.contains(2), "rejected tile is not resident");
}
}

void testGlobeAncestorFallback()
{
    MapBgfx2dImagery::TileId tile;
    tile.zoom = 3;
    tile.x = 5;
    tile.y = 6;
    const MapBgfxGlobeImagery::Source own = MapBgfxGlobeImagery::ancestor(tile, 0);
    req(own.tile == tile && own.u0 == 0.0 && own.v0 == 0.0 && own.u1 == 1.0 && own.v1 == 1.0,
        "globe: level 0 is the tile itself, whole texture");
    // Parent (2, 3) at zoom 2: tile 5 is its right half, tile 6 its top half.
    const MapBgfxGlobeImagery::Source parent = MapBgfxGlobeImagery::ancestor(tile, 1);
    req(parent.tile.zoom == 2 && parent.tile.x == 2 && parent.tile.y == 3,
        "globe: parent tile id");
    req(parent.u0 == 0.5 && parent.u1 == 1.0 && parent.v0 == 0.0 && parent.v1 == 0.5,
        "globe: the parent's quarter covering the tile");
    // Three levels up: (0, 0) at zoom 0, the tile is cell (5, 6) of 8 x 8.
    const MapBgfxGlobeImagery::Source root = MapBgfxGlobeImagery::ancestor(tile, 3);
    req(root.tile.zoom == 0 && root.tile.x == 0 && root.tile.y == 0
            && root.u0 == 5.0 / 8.0 && root.u1 == 6.0 / 8.0
            && root.v0 == 6.0 / 8.0 && root.v1 == 7.0 / 8.0,
        "globe: the root's 1/64 covering the tile");

    // Atlas mapping: the tile's (0, 0) and (1, 1) land on the corners of
    // the ancestor's covering part inside its cell.
    MapBgfx2dImageryAtlas atlas;
    atlas.reset(4096, Budget);
    atlas.allocate(77, Tile, Tile, 1);
    atlas.allocate(78, Tile, Tile, 1);
    const MapBgfx2dImageryAtlas::Slot *slot = atlas.slot(78);
    const float rect[4] = {float(atlas.atlasU(*slot, 0.0)), float(atlas.atlasV(*slot, 0.0)),
                           float(atlas.atlasU(*slot, 1.0)), float(atlas.atlasV(*slot, 1.0))};
    float uniform[4];
    MapBgfxGlobeImagery::tileUniform(rect, parent, uniform);
    const double page_width = atlas.page(slot->page).width;
    const double page_height = atlas.page(slot->page).height;
    const double u_at_0 = uniform[0] * page_width;
    const double u_at_1 = (uniform[0] + uniform[2]) * page_width;
    const double v_at_0 = uniform[1] * page_height;
    const double v_at_1 = (uniform[1] + uniform[3]) * page_height;
    const QPoint origin = atlas.tileOrigin(*slot);
    req(std::fabs(u_at_0 - (origin.x() + 0.5 * Tile)) < 1e-2
            && std::fabs(u_at_1 - (origin.x() + Tile)) < 1e-2,
        "globe: u spans the right half of the parent's texels");
    req(std::fabs(v_at_0 - origin.y()) < 1e-2 && std::fabs(v_at_1 - (origin.y() + 0.5 * Tile)) < 1e-2,
        "globe: v spans the top half of the parent's texels");
    MapBgfxGlobeImagery::tileUniform(rect, own, uniform);
    req(uniform[0] == rect[0] && uniform[1] == rect[1]
            && std::fabs(uniform[2] - (rect[2] - rect[0])) < 1e-7
            && std::fabs(uniform[3] - (rect[3] - rect[1])) < 1e-7,
        "globe: own imagery uses the whole cell");
}

int main()
{
    testPlacement();
    testBudgetPagesAndReuse();
    testSmallLimits();
    testGlobeAncestorFallback();
    if (fails == 0)
        std::printf("bgfx 2D imagery atlas tests passed\n");
    return fails == 0 ? 0 : 1;
}
