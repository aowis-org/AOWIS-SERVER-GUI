#include "map/bgfx/map_bgfx_2d_imagery_gpu.h"

#include "map/bgfx/map_bgfx_backend.h"

#include <QDebug>
#include <QFile>
#include <QVector>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace
{
struct ImageryVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

// Clamp sampling so linear filtering never bleeds the opposite tile edge in.
constexpr uint64_t kTileSamplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
}

quint64 MapBgfx2dImageryGpu::keyFor(const MapBgfx2dImagery::TileId &tile)
{
    // zoom <= 31 and x, y < 2^29 for every Web Mercator level we can request.
    return (quint64(quint32(tile.zoom)) << 58)
        | (quint64(quint32(tile.x)) << 29)
        | quint64(quint32(tile.y));
}

bool MapBgfx2dImageryGpu::initialize()
{
    this->layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    this->program = MapBgfxBackend::loadProgram("vs_imagery_2d", "fs_imagery_2d");
    if (!bgfx::isValid(this->program))
        return false;
    this->tile_sampler = bgfx::createUniform("s_tile", bgfx::UniformType::Sampler);
    this->background_uniform = bgfx::createUniform("u_imagery_background", bgfx::UniformType::Vec4);
    return bgfx::isValid(this->tile_sampler) && bgfx::isValid(this->background_uniform);
}

void MapBgfx2dImageryGpu::release()
{
    clearTextures();
    if (bgfx::isValid(this->tile_sampler))
    {
        bgfx::destroy(this->tile_sampler);
        this->tile_sampler = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(this->background_uniform))
    {
        bgfx::destroy(this->background_uniform);
        this->background_uniform = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(this->program))
    {
        bgfx::destroy(this->program);
        this->program = BGFX_INVALID_HANDLE;
    }
}

void MapBgfx2dImageryGpu::clearTextures()
{
    for (QHash<quint64, Texture>::iterator it = this->textures.begin();
         it != this->textures.end(); ++it)
    {
        if (bgfx::isValid(it->handle))
            bgfx::destroy(it->handle);
    }
    this->textures.clear();
}

bool MapBgfx2dImageryGpu::hasTexture(const MapBgfx2dImagery::TileId &tile) const
{
    return this->textures.contains(keyFor(tile));
}

bool MapBgfx2dImageryGpu::uploadTexture(const MapBgfx2dImagery::TileId &tile,
                                        const QImage &image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0
        || image.width() > 4096 || image.height() > 4096)
    {
        return false;
    }
    // RGBA8888 is byte order R,G,B,A in memory, i.e. bgfx RGBA8. Rows are
    // uploaded top first, so v = 0 is the top edge on every renderer.
    // MapTileRepository already delivers RGBA8888 (converted on its decode
    // threads).
    const QImage rgba = image.format() == QImage::Format_RGBA8888
        ? image : image.convertToFormat(QImage::Format_RGBA8888);
    const int row_bytes = rgba.width() * 4;
    const bgfx::Memory *memory = nullptr;
    if (rgba.bytesPerLine() == row_bytes)
    {
        // Tightly packed (every 256 px tile): no copy at all. bgfx reads the
        // pixels when it creates the texture during the next bgfx::frame()
        // and then calls the release function, which drops this implicitly
        // shared reference (the pixels stay valid even if the repository
        // evicts the tile meanwhile).
        QImage *keep = new QImage(rgba);
        memory = bgfx::makeRef(keep->constBits(), uint32_t(keep->sizeInBytes()),
                               [](void *, void *user_data) { delete static_cast<QImage *>(user_data); },
                               keep);
    }
    else
    {
        // bytesPerLine() is padded; bgfx expects tightly packed rows.
        memory = bgfx::alloc(uint32_t(qsizetype(row_bytes) * rgba.height()));
        for (int row = 0; row < rgba.height(); ++row)
            std::copy_n(rgba.constScanLine(row), row_bytes, memory->data + qsizetype(row) * row_bytes);
    }
    const bgfx::TextureHandle handle = bgfx::createTexture2D(
        uint16_t(rgba.width()), uint16_t(rgba.height()), false, 1,
        bgfx::TextureFormat::RGBA8, kTileSamplerFlags, memory);
    if (!bgfx::isValid(handle))
        return false;

    const quint64 key = keyFor(tile);
    QHash<quint64, Texture>::iterator existing = this->textures.find(key);
    if (existing != this->textures.end() && bgfx::isValid(existing->handle))
        bgfx::destroy(existing->handle);
    Texture texture;
    texture.handle = handle;
    texture.last_used_frame = this->frame;
    this->textures.insert(key, texture);
    ++this->upload_count;
    return true;
}

void MapBgfx2dImageryGpu::draw(bgfx::ViewId view, const MapBgfx2dImagery::Layout &layout,
                               double viewport_width, double viewport_height,
                               const QColor &background, double background_opacity)
{
    if (!bgfx::isValid(this->program) || layout.quads.empty()
        || viewport_width <= 0.0 || viewport_height <= 0.0)
    {
        return;
    }

    const uint32_t vertices_per_quad = 6;
    uint32_t quad_count = uint32_t(layout.quads.size());
    const uint32_t available = bgfx::getAvailTransientVertexBuffer(
        quad_count * vertices_per_quad, this->layout);
    quad_count = std::min(quad_count, available / vertices_per_quad);
    if (quad_count == 0)
        return;

    bgfx::TransientVertexBuffer buffer;
    bgfx::allocTransientVertexBuffer(&buffer, quad_count * vertices_per_quad, this->layout);
    ImageryVertex *vertex = reinterpret_cast<ImageryVertex *>(buffer.data);
    // Logical pixels -> clip space, still in double until the final cast.
    const std::function<float(double)> clip_x = [viewport_width](double x)
    {
        return float(x / viewport_width * 2.0 - 1.0);
    };
    const std::function<float(double)> clip_y = [viewport_height](double y)
    {
        return float(1.0 - y / viewport_height * 2.0);
    };
    for (uint32_t index = 0; index < quad_count; ++index)
    {
        const MapBgfx2dImagery::Quad &quad = layout.quads[index];
        const float left = clip_x(quad.left);
        const float right = clip_x(quad.right);
        const float top = clip_y(quad.top);
        const float bottom = clip_y(quad.bottom);
        const float u0 = float(quad.u0);
        const float v0 = float(quad.v0);
        const float u1 = float(quad.u1);
        const float v1 = float(quad.v1);
        const ImageryVertex corners[6] = {
            {left, top, u0, v0}, {right, top, u1, v0}, {right, bottom, u1, v1},
            {left, top, u0, v0}, {right, bottom, u1, v1}, {left, bottom, u0, v1}
        };
        std::copy_n(corners, 6, vertex + index * vertices_per_quad);
    }

    const float background_data[4] = {
        float(background.redF()), float(background.greenF()), float(background.blueF()),
        float(std::clamp(background_opacity, 0.0, 1.0))
    };
    for (uint32_t index = 0; index < quad_count; ++index)
    {
        const MapBgfx2dImagery::Quad &quad = layout.quads[index];
        QHash<quint64, Texture>::iterator texture = this->textures.find(keyFor(quad.tile));
        if (texture == this->textures.end() || !bgfx::isValid(texture->handle))
            continue;
        texture->last_used_frame = this->frame;
        bgfx::setVertexBuffer(0, &buffer, index * vertices_per_quad, vertices_per_quad);
        bgfx::setTexture(0, this->tile_sampler, texture->handle, uint32_t(kTileSamplerFlags));
        bgfx::setUniform(this->background_uniform, background_data);
        // Opaque imagery, no depth: painter's order is the layout order.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(view, this->program);
    }
}

void MapBgfx2dImageryGpu::endFrame()
{
    if (this->textures.size() > TextureBudget)
    {
        // Evict oldest first, never anything drawn in the current frame.
        QVector<QPair<quint64, quint64>> by_age;
        by_age.reserve(this->textures.size());
        for (QHash<quint64, Texture>::const_iterator it = this->textures.cbegin();
             it != this->textures.cend(); ++it)
        {
            if (it->last_used_frame != this->frame)
                by_age.append(qMakePair(it->last_used_frame, it.key()));
        }
        std::sort(by_age.begin(), by_age.end());
        int excess = int(this->textures.size()) - TextureBudget;
        for (const QPair<quint64, quint64> &entry : by_age)
        {
            if (excess <= 0)
                break;
            QHash<quint64, Texture>::iterator it = this->textures.find(entry.second);
            if (it != this->textures.end())
            {
                if (bgfx::isValid(it->handle))
                    bgfx::destroy(it->handle);
                this->textures.erase(it);
                --excess;
            }
        }
    }
    ++this->frame;
}
