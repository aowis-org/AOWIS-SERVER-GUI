#ifndef MAP_ENTITY_PIXMAP_RENDERER_H
#define MAP_ENTITY_PIXMAP_RENDERER_H

#include <QHash>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "common/_enums_structs.h"

class QColor;
class QPainter;

class MapEntityPixmapRenderer
{
public:
    enum class Highlight
    {
        None,
        Selected,
        Error,
        SelectedError,
        Stale,
        SelectedStale
    };

    static QString pixmapPathForEntity(InfrastructureEntity entity);

    QPixmap pixmap(const QString &path, int width) const;
    QRectF bottomAnchoredRect(const QPointF &anchor, const QString &path, int width) const;
    QRectF centeredRect(const QPointF &center, const QString &path, int width) const;
    bool hitTest(const QString &path, int width, const QRectF &target_rect,
                 const QPointF &position) const;
    void paint(QPainter &painter, const QString &path, int width,
               const QRectF &target_rect, Highlight highlight = Highlight::None) const;
    void clearCache();

private:
    QString cacheKey(const QString &path, int width) const;
    QPixmap tintedPixmap(const QPixmap &source, const QColor &color) const;

    mutable QHash<QString, QPixmap> pixmap_cache;
};

#endif // MAP_ENTITY_PIXMAP_RENDERER_H
