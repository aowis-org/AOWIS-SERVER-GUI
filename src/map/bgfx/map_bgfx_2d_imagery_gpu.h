#ifndef MAP_BGFX_2D_IMAGERY_GPU_H
#define MAP_BGFX_2D_IMAGERY_GPU_H

#include "map/bgfx/map_bgfx_2d_imagery_layout.h"

#include <QColor>
#include <QHash>
#include <QImage>

#include <bgfx/bgfx.h>

// bgfx resources for 2D imagery: one texture per tile in an LRU cache and
// one program. Tile choice lives in MapBgfx2dImagery::buildLayout(); this
// class only uploads, draws and evicts.
class MapBgfx2dImageryGpu
{
public:
    // Textures kept resident. ~256 KiB each for 256x256 RGBA8: a 4K viewport
    // needs ~190 target tiles plus fallbacks, so this keeps one extra level.
    static constexpr int TextureBudget = 384;

    bool initialize();
    void release();
    // Provider switch / deleted tiles: the same tile id now means other pixels.
    void clearTextures();

    bool hasTexture(const MapBgfx2dImagery::TileId &tile) const;
    bool uploadTexture(const MapBgfx2dImagery::TileId &tile, const QImage &image);

    // Draws the layout's quads in order. viewport is in logical pixels, the
    // same units as the quads. background_opacity is 0..1.
    void draw(bgfx::ViewId view, const MapBgfx2dImagery::Layout &layout,
              double viewport_width, double viewport_height,
              const QColor &background, double background_opacity);

    // Evicts least recently drawn textures above TextureBudget.
    void endFrame();

    int textureCount() const { return int(this->textures.size()); }
    quint64 uploadCount() const { return this->upload_count; }

private:
    struct Texture
    {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        quint64 last_used_frame = 0;
    };

    static quint64 keyFor(const MapBgfx2dImagery::TileId &tile);

    QHash<quint64, Texture> textures;
    bgfx::VertexLayout layout;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tile_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle background_uniform = BGFX_INVALID_HANDLE;
    quint64 frame = 1;
    quint64 upload_count = 0;
};

#endif // MAP_BGFX_2D_IMAGERY_GPU_H
