#include "map/bgfx/map_bgfx_editor_icons.h"

#include "map/render/map_entity_pixmap_renderer.h"

#include <QColor>
#include <QPainter>
#include <QPoint>

#include <algorithm>
#include <cmath>

namespace
{
// MapEntityPixmapRenderer's glow offsets (logical px).
const QPoint outer_glow_offsets[] = {
    QPoint(-9, 0), QPoint(9, 0), QPoint(0, -9), QPoint(0, 9),
    QPoint(-8, -4), QPoint(-8, 4), QPoint(8, -4), QPoint(8, 4),
    QPoint(-4, -8), QPoint(-4, 8), QPoint(4, -8), QPoint(4, 8),
    QPoint(-7, 0), QPoint(7, 0), QPoint(0, -7), QPoint(0, 7),
    QPoint(-6, -4), QPoint(-6, 4), QPoint(6, -4), QPoint(6, 4)
};

const QPoint glow_offsets[] = {
    QPoint(-6, 0), QPoint(6, 0), QPoint(0, -6), QPoint(0, 6),
    QPoint(-5, -3), QPoint(-5, 3), QPoint(5, -3), QPoint(5, 3),
    QPoint(-3, -5), QPoint(-3, 5), QPoint(3, -5), QPoint(3, 5),
    QPoint(-4, 0), QPoint(4, 0), QPoint(0, -4), QPoint(0, 4),
    QPoint(-3, -3), QPoint(-3, 3), QPoint(3, -3), QPoint(3, 3),
    QPoint(-2, 0), QPoint(2, 0), QPoint(0, -2), QPoint(0, 2),
    QPoint(-2, -2), QPoint(-2, 2), QPoint(2, -2), QPoint(2, 2)
};

// MapEntityPixmapRenderer::tintedPixmap(): the icon's shape in one colour.
QImage tinted(const QImage &source, const QColor &color)
{
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.drawImage(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), color);
    return result;
}

void drawGlow(QPainter *painter, const QImage &glow, const QPoint *offsets, int count,
              double ratio, int padding_px)
{
    painter->save();
    painter->setOpacity(0.8);
    for (int index = 0; index < count; ++index)
    {
        painter->drawImage(QPointF(padding_px + offsets[index].x() * ratio,
                                   padding_px + offsets[index].y() * ratio), glow);
    }
    painter->restore();
}
}

const QImage &MapBgfxEditorIcons::source(const QString &path)
{
    QHash<QString, QImage>::iterator found = this->sources.find(path);
    if (found == this->sources.end())
    {
        found = this->sources.insert(
            path, QImage(path).convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
    return found.value();
}

MapBgfxEditorOverlay::Icon MapBgfxEditorIcons::icon(MapBgfxHudTextCache *cache,
                                                    InfrastructureEntity entity,
                                                    const QString &pixmap_path, int width,
                                                    MapBgfxEditorOverlay::Highlight highlight)
{
    MapBgfxEditorOverlay::Icon result;
    if (cache == nullptr || width <= 0)
        return result;
    // The memo belongs to one atlas generation (a cleared atlas, also after
    // a device pixel ratio change, invalidates every sprite).
    if (this->type_icons_cache != cache || this->type_icons_generation != cache->generation())
    {
        this->type_icons.clear();
        this->type_icons_cache = cache;
        this->type_icons_generation = cache->generation();
    }
    const quint64 type_key = (quint64(quint8(entity)) << 40) | (quint64(quint8(highlight)) << 32)
        | quint64(quint32(width));
    if (pixmap_path.isEmpty())
    {
        const QHash<quint64, MapBgfxEditorOverlay::Icon>::const_iterator memo =
            this->type_icons.constFind(type_key);
        if (memo != this->type_icons.cend())
            return memo.value();
    }
    result = typeIcon(cache, entity, pixmap_path, width, highlight);
    // Only a sprite that is still in the atlas is memoized (storing it may
    // have cleared the atlas: the generation check above then drops it).
    if (pixmap_path.isEmpty() && result.sprite.valid
        && this->type_icons_generation == cache->generation())
        this->type_icons.insert(type_key, result);
    return result;
}

MapBgfxEditorOverlay::Icon MapBgfxEditorIcons::typeIcon(MapBgfxHudTextCache *cache,
                                                        InfrastructureEntity entity,
                                                        const QString &pixmap_path, int width,
                                                        MapBgfxEditorOverlay::Highlight highlight)
{
    MapBgfxEditorOverlay::Icon result;
    const QString path = pixmap_path.isEmpty()
        ? MapEntityPixmapRenderer::pixmapPathForEntity(entity) : pixmap_path;
    const QImage &image = source(path);
    if (image.isNull() || image.width() <= 0)
        return result;

    // Logical size of MapEntityPixmapRenderer::pixmap(path, width)
    // (QPixmap::scaledToWidth()): the anchoring rects depend on it.
    result.width = double(width);
    result.height = double(std::max(1, qRound(double(image.height()) * double(width)
                                              / double(image.width()))));
    const bool glowing = highlight != MapBgfxEditorOverlay::Highlight::None;
    result.padding = glowing ? double(GlowPaddingPx) : 0.0;
    const QSizeF logical(result.width + 2.0 * result.padding, result.height + 2.0 * result.padding);
    const QString key = QStringLiteral("E|%1|%2|%3").arg(path).arg(width).arg(int(highlight));
    result.sprite = cache->cachedImage(key, logical);
    if (result.sprite.valid)
        return result;

    // Composite once at the device pixel ratio (sharp on HiDPI): glows
    // first, the icon on top (MapEntityPixmapRenderer::paint()).
    const double ratio = cache->devicePixelRatio();
    const int icon_width = std::max(1, qRound(result.width * ratio));
    const int icon_height = std::max(1, qRound(result.height * ratio));
    const int padding = qRound(result.padding * ratio);
    const QImage scaled = image.scaled(icon_width, icon_height, Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation);
    QImage composite(icon_width + 2 * padding, icon_height + 2 * padding,
                     QImage::Format_ARGB32_Premultiplied);
    composite.fill(Qt::transparent);
    {
        QPainter painter(&composite);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const int glow_count = int(sizeof(glow_offsets) / sizeof(glow_offsets[0]));
        const int outer_count = int(sizeof(outer_glow_offsets) / sizeof(outer_glow_offsets[0]));
        switch (highlight)
        {
            case MapBgfxEditorOverlay::Highlight::SelectedError:
            case MapBgfxEditorOverlay::Highlight::SelectedStale:
                drawGlow(&painter, tinted(scaled, QColor(0, 190, 255, 255)), outer_glow_offsets,
                         outer_count, ratio, padding);
                drawGlow(&painter,
                         tinted(scaled, highlight == MapBgfxEditorOverlay::Highlight::SelectedStale
                                            ? QColor(128, 128, 128, 255)
                                            : QColor(255, 0, 0, 255)),
                         glow_offsets, glow_count, ratio, padding);
                break;
            case MapBgfxEditorOverlay::Highlight::Selected:
                drawGlow(&painter, tinted(scaled, QColor(0, 190, 255, 255)), glow_offsets,
                         glow_count, ratio, padding);
                break;
            case MapBgfxEditorOverlay::Highlight::Stale:
                drawGlow(&painter, tinted(scaled, QColor(128, 128, 128, 255)), glow_offsets,
                         glow_count, ratio, padding);
                break;
            case MapBgfxEditorOverlay::Highlight::Error:
                drawGlow(&painter, tinted(scaled, QColor(255, 0, 0, 255)), glow_offsets,
                         glow_count, ratio, padding);
                break;
            case MapBgfxEditorOverlay::Highlight::None:
                break;
        }
        painter.drawImage(padding, padding, scaled);
    }
    result.sprite = cache->image(key, composite, logical);
    return result;
}
