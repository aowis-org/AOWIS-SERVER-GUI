#ifndef MAP_BGFX_HUD_CANVAS_H
#define MAP_BGFX_HUD_CANVAS_H

#include <QtGlobal>
#include <QVector>

#include <algorithm>
#include <cmath>

// Immediate-mode draw list for the native bgfx HUD (slice 6).
//
// Everything is in logical pixels (x right, y down, origin top-left) and is
// drawn with ONE texture, the HUD atlas (MapBgfxHudTextCache): text runs and
// images are atlas sprites, solid shapes sample the atlas' white texel. One
// indexed triangle list per frame, one draw call (MapBgfxHudGpu).
//
// Colours are passed as QRgb-style 0xAARRGGBB and stored premultiplied as
// ABGR (bytes R,G,B,A), because the HUD shader multiplies the premultiplied
// atlas texel by the vertex colour and blends One / OneMinusSrcAlpha. Shapes
// are plain triangles; their edges are antialiased by the view's MSAA.
struct MapBgfxHudRect
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    bool contains(double px, double py) const
    {
        return px >= this->x && py >= this->y
            && px < this->x + this->width && py < this->y + this->height;
    }
    double right() const { return this->x + this->width; }
    double bottom() const { return this->y + this->height; }
};

// An atlas region and the logical size it is drawn at. origin_x/origin_y:
// where the layout position lies inside the sprite (text runs carry a
// padding column for glyph overhang; layout uses the advance width).
struct MapBgfxHudSprite
{
    bool valid = false;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    double width = 0.0;
    double height = 0.0;
    double origin_x = 0.0;
    double origin_y = 0.0;
};

struct MapBgfxHudVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    quint32 abgr = 0;
};

struct MapBgfxHudPoint
{
    double x = 0.0;
    double y = 0.0;
};

class MapBgfxHudCanvas
{
public:
    static constexpr double Pi = 3.14159265358979323846;

    // Premultiplied ABGR from 0xAARRGGBB.
    static quint32 premultipliedAbgr(quint32 argb)
    {
        const quint32 alpha = (argb >> 24) & 0xffu;
        const quint32 red = ((argb >> 16) & 0xffu) * alpha / 255u;
        const quint32 green = ((argb >> 8) & 0xffu) * alpha / 255u;
        const quint32 blue = (argb & 0xffu) * alpha / 255u;
        return (alpha << 24) | (blue << 16) | (green << 8) | red;
    }

    void clear()
    {
        this->vertex_list.clear();
        this->index_list.clear();
    }

    // Atlas coordinate of an opaque white texel (used for solid shapes).
    void setWhiteTexel(float u, float v)
    {
        this->white_u = u;
        this->white_v = v;
    }

    void fillRect(const MapBgfxHudRect &rect, quint32 argb)
    {
        if (!(rect.width > 0.0) || !(rect.height > 0.0))
            return;
        QVector<MapBgfxHudPoint> corners;
        corners.append({rect.x, rect.y});
        corners.append({rect.right(), rect.y});
        corners.append({rect.right(), rect.bottom()});
        corners.append({rect.x, rect.bottom()});
        fillConvexPolygon(corners, argb);
    }

    void fillRoundedRect(const MapBgfxHudRect &rect, double radius, quint32 argb)
    {
        fillConvexPolygon(roundedRectOutline(rect, radius), argb);
    }

    // Border of width line_width inside rect.
    void strokeRoundedRect(const MapBgfxHudRect &rect, double radius, double line_width,
                           quint32 argb)
    {
        if (!(line_width > 0.0))
            return;
        const MapBgfxHudRect inner{rect.x + line_width, rect.y + line_width,
                                   rect.width - 2.0 * line_width, rect.height - 2.0 * line_width};
        if (!(inner.width > 0.0) || !(inner.height > 0.0))
        {
            fillRoundedRect(rect, radius, argb);
            return;
        }
        fillRing(roundedRectOutline(rect, radius),
                 roundedRectOutline(inner, std::max(0.0, radius - line_width)), argb);
    }

    void fillCircle(double center_x, double center_y, double radius, quint32 argb)
    {
        fillConvexPolygon(circleOutline(center_x, center_y, radius), argb);
    }

    void strokeCircle(double center_x, double center_y, double radius, double line_width,
                      quint32 argb)
    {
        fillRing(circleOutline(center_x, center_y, radius),
                 circleOutline(center_x, center_y, std::max(0.0, radius - line_width)), argb);
    }

    // Straight line of width line_width (square ends).
    void line(double x0, double y0, double x1, double y1, double line_width, quint32 argb)
    {
        const double dx = x1 - x0;
        const double dy = y1 - y0;
        const double length = std::hypot(dx, dy);
        if (!(length > 0.0) || !(line_width > 0.0))
            return;
        const double nx = -dy / length * 0.5 * line_width;
        const double ny = dx / length * 0.5 * line_width;
        QVector<MapBgfxHudPoint> corners;
        corners.append({x0 + nx, y0 + ny});
        corners.append({x1 + nx, y1 + ny});
        corners.append({x1 - nx, y1 - ny});
        corners.append({x0 - nx, y0 - ny});
        fillConvexPolygon(corners, argb);
    }

    // Atlas sprite stretched to rect, tinted (white = original colours).
    void sprite(const MapBgfxHudRect &rect, const MapBgfxHudSprite &sprite, quint32 tint_argb)
    {
        if (!sprite.valid || !(rect.width > 0.0) || !(rect.height > 0.0))
            return;
        const quint32 color = premultipliedAbgr(tint_argb);
        const quint32 base = quint32(this->vertex_list.size());
        this->vertex_list.append({float(rect.x), float(rect.y), sprite.u0, sprite.v0, color});
        this->vertex_list.append({float(rect.right()), float(rect.y), sprite.u1, sprite.v0, color});
        this->vertex_list.append({float(rect.right()), float(rect.bottom()), sprite.u1, sprite.v1, color});
        this->vertex_list.append({float(rect.x), float(rect.bottom()), sprite.u0, sprite.v1, color});
        appendQuadIndices(base);
    }

    // Sprite at its own logical size, its layout origin at (x, y) (for text:
    // the left end of the advance and the top of the line).
    void sprite(double x, double y, const MapBgfxHudSprite &atlas_sprite, quint32 tint_argb)
    {
        sprite(MapBgfxHudRect{x - atlas_sprite.origin_x, y - atlas_sprite.origin_y,
                              atlas_sprite.width, atlas_sprite.height},
               atlas_sprite, tint_argb);
    }

    const QVector<MapBgfxHudVertex> &vertices() const { return this->vertex_list; }
    const QVector<quint16> &indices() const { return this->index_list; }
    // 16-bit indices: a HUD frame has a few hundred vertices.
    bool overflowed() const { return this->vertex_list.size() > 65535; }

private:
    static QVector<MapBgfxHudPoint> circleOutline(double cx, double cy, double radius)
    {
        QVector<MapBgfxHudPoint> points;
        if (!(radius > 0.0))
            return points;
        // About one segment per 2 px of circumference, at least 12.
        const int segments = std::clamp(int(std::ceil(radius * 3.0)), 12, 64);
        for (int index = 0; index < segments; ++index)
        {
            const double angle = 2.0 * Pi * double(index) / double(segments);
            points.append({cx + radius * std::cos(angle), cy + radius * std::sin(angle)});
        }
        return points;
    }

    static QVector<MapBgfxHudPoint> roundedRectOutline(const MapBgfxHudRect &rect, double radius)
    {
        QVector<MapBgfxHudPoint> points;
        if (!(rect.width > 0.0) || !(rect.height > 0.0))
            return points;
        const double r = std::clamp(radius, 0.0, 0.5 * std::min(rect.width, rect.height));
        if (r <= 0.0)
        {
            points.append({rect.x, rect.y});
            points.append({rect.right(), rect.y});
            points.append({rect.right(), rect.bottom()});
            points.append({rect.x, rect.bottom()});
            return points;
        }
        constexpr int CornerSegments = 6;
        // Corner centres clockwise from top-left, each arc spanning 90 deg.
        const double centers[4][2] = {
            {rect.x + r, rect.y + r}, {rect.right() - r, rect.y + r},
            {rect.right() - r, rect.bottom() - r}, {rect.x + r, rect.bottom() - r}};
        const double start_angles[4] = {Pi, 1.5 * Pi, 0.0, 0.5 * Pi};
        for (int corner = 0; corner < 4; ++corner)
        {
            for (int step = 0; step <= CornerSegments; ++step)
            {
                const double angle = start_angles[corner]
                    + 0.5 * Pi * double(step) / double(CornerSegments);
                points.append({centers[corner][0] + r * std::cos(angle),
                               centers[corner][1] + r * std::sin(angle)});
            }
        }
        return points;
    }

    void fillConvexPolygon(const QVector<MapBgfxHudPoint> &points, quint32 argb)
    {
        if (points.size() < 3)
            return;
        const quint32 color = premultipliedAbgr(argb);
        const quint32 base = quint32(this->vertex_list.size());
        for (const MapBgfxHudPoint &point : points)
            this->vertex_list.append({float(point.x), float(point.y), this->white_u, this->white_v, color});
        for (qsizetype index = 1; index + 1 < points.size(); ++index)
        {
            this->index_list.append(quint16(base));
            this->index_list.append(quint16(base + quint32(index)));
            this->index_list.append(quint16(base + quint32(index) + 1u));
        }
    }

    // Area between two outlines with the same number of points (same shape
    // generator, inner one inset).
    void fillRing(const QVector<MapBgfxHudPoint> &outer, const QVector<MapBgfxHudPoint> &inner,
                  quint32 argb)
    {
        if (outer.size() < 3 || outer.size() != inner.size())
        {
            fillConvexPolygon(outer, argb);
            return;
        }
        const quint32 color = premultipliedAbgr(argb);
        const quint32 base = quint32(this->vertex_list.size());
        const quint32 count = quint32(outer.size());
        for (const MapBgfxHudPoint &point : outer)
            this->vertex_list.append({float(point.x), float(point.y), this->white_u, this->white_v, color});
        for (const MapBgfxHudPoint &point : inner)
            this->vertex_list.append({float(point.x), float(point.y), this->white_u, this->white_v, color});
        for (quint32 index = 0; index < count; ++index)
        {
            const quint32 next = (index + 1u) % count;
            const quint32 quad[6] = {base + index, base + next, base + count + next,
                                     base + index, base + count + next, base + count + index};
            for (const quint32 vertex : quad)
                this->index_list.append(quint16(vertex));
        }
    }

    void appendQuadIndices(quint32 base)
    {
        const quint32 quad[6] = {base, base + 1u, base + 2u, base, base + 2u, base + 3u};
        for (const quint32 vertex : quad)
            this->index_list.append(quint16(vertex));
    }

    QVector<MapBgfxHudVertex> vertex_list;
    QVector<quint16> index_list;
    float white_u = 0.0f;
    float white_v = 0.0f;
};

#endif // MAP_BGFX_HUD_CANVAS_H
