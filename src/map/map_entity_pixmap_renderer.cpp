#include "map_entity_pixmap_renderer.h"

#include <QColor>
#include <QPainter>
#include <QPointF>


namespace
{
const QPoint glow_offsets[] = {
    QPoint(-6, 0), QPoint(6, 0), QPoint(0, -6), QPoint(0, 6),
    QPoint(-5, -3), QPoint(-5, 3), QPoint(5, -3), QPoint(5, 3),
    QPoint(-3, -5), QPoint(-3, 5), QPoint(3, -5), QPoint(3, 5),
    QPoint(-4, 0), QPoint(4, 0), QPoint(0, -4), QPoint(0, 4),
    QPoint(-3, -3), QPoint(-3, 3), QPoint(3, -3), QPoint(3, 3),
    QPoint(-2, 0), QPoint(2, 0), QPoint(0, -2), QPoint(0, 2),
    QPoint(-2, -2), QPoint(-2, 2), QPoint(2, -2), QPoint(2, 2)
};
}


QString MapEntityPixmapRenderer::pixmapPathForEntity(InfrastructureEntity entity)
{
    switch (entity)
    {
    case InfrastructureEntity::Junction:
        return QStringLiteral(":/icon/junction.png");
    case InfrastructureEntity::Reservoir:
        return QStringLiteral(":/icon/reservoir.png");
    case InfrastructureEntity::Tank:
        return QStringLiteral(":/icon/tower.png");
    case InfrastructureEntity::Pipe:
        return QStringLiteral(":/icon/pipe.png");
    case InfrastructureEntity::Pump:
        return QStringLiteral(":/icon/pump.png");
    case InfrastructureEntity::Valve:
        return QStringLiteral(":/icon/valve.png");
    case InfrastructureEntity::CustomerPoint:
        return QStringLiteral(":/icon/customer.png");
    case InfrastructureEntity::ElectricJunction:
    case InfrastructureEntity::Cable:
    case InfrastructureEntity::Switch:
    case InfrastructureEntity::Fuse:
    case InfrastructureEntity::CircuitBreaker:
        return QStringLiteral(":/icon/electricity.png");
    case InfrastructureEntity::Battery:
    case InfrastructureEntity::Generator:
    case InfrastructureEntity::SolarPanel:
    case InfrastructureEntity::Inverter:
    case InfrastructureEntity::Transformer:
        return QStringLiteral(":/icon/energy.png");
    case InfrastructureEntity::Note:
    case InfrastructureEntity::Unknown:
        return QStringLiteral(":/icon/geomarker.png");
    }

    return QStringLiteral(":/icon/geomarker.png");
}

QPixmap MapEntityPixmapRenderer::pixmap(const QString &path, int width) const
{
    const QString key = cacheKey(path, width);
    const QHash<QString, QPixmap>::const_iterator iterator = this->pixmap_cache.constFind(key);
    if (iterator != this->pixmap_cache.constEnd())
        return iterator.value();

    QPixmap result(path);
    if (!result.isNull() && width > 0)
        result = result.scaledToWidth(width, Qt::SmoothTransformation);

    this->pixmap_cache.insert(key, result);
    return result;
}

QRectF MapEntityPixmapRenderer::bottomAnchoredRect(const QPointF &anchor,
                                                    const QString &path,
                                                    int width) const
{
    const QPixmap image = pixmap(path, width);
    return QRectF(anchor.x(), anchor.y() - image.height(), image.width(), image.height());
}

QRectF MapEntityPixmapRenderer::centeredRect(const QPointF &center,
                                              const QString &path,
                                              int width) const
{
    const QPixmap image = pixmap(path, width);
    return QRectF(center.x() - image.width() / 2.0,
                  center.y() - image.height() / 2.0,
                  image.width(), image.height());
}

bool MapEntityPixmapRenderer::hitTest(const QString &path, int width,
                                      const QRectF &target_rect,
                                      const QPointF &position) const
{
    const QPixmap image = pixmap(path, width);
    return !image.isNull() && target_rect.contains(position);
}

void MapEntityPixmapRenderer::paint(QPainter &painter, const QString &path, int width,
                                    const QRectF &target_rect, Highlight highlight) const
{
    const QPixmap image = pixmap(path, width);
    if (image.isNull())
        return;

    QColor highlight_color;
    if (highlight == Highlight::Selected)
        highlight_color = QColor(0, 190, 255, 255);
    else if (highlight == Highlight::Error)
        highlight_color = QColor(255, 0, 0, 255);

    if (highlight != Highlight::None)
    {
        const QPixmap glow = tintedPixmap(image, highlight_color);
        painter.save();
        painter.setOpacity(0.8);
        for (const QPoint &offset : glow_offsets)
            painter.drawPixmap(target_rect.topLeft() + QPointF(offset), glow);
        painter.restore();
    }

    painter.drawPixmap(target_rect, image, QRectF(image.rect()));
}

void MapEntityPixmapRenderer::clearCache()
{
    this->pixmap_cache.clear();
}

QString MapEntityPixmapRenderer::cacheKey(const QString &path, int width) const
{
    return path + QLatin1Char('|') + QString::number(width);
}

QPixmap MapEntityPixmapRenderer::tintedPixmap(const QPixmap &source, const QColor &color) const
{
    QPixmap tinted(source.size());
    tinted.fill(Qt::transparent);

    QPainter painter(&tinted);
    painter.drawPixmap(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    return tinted;
}
