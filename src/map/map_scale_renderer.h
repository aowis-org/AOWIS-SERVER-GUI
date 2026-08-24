#ifndef MAP_SCALE_RENDERER_H
#define MAP_SCALE_RENDERER_H

#include "map_model.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QtGlobal>

#include <cmath>

namespace MapScaleRenderer
{
namespace Detail
{
constexpr int MarginPixels = 10;
constexpr int MaximumScaleWidthPixels = 100;
constexpr int PanelPaddingPixels = 4;
constexpr int ScaleTickHeightPixels = 5;
constexpr int TextGapPixels = 2;
constexpr double EarthRadiusMeters = 6371000.0;
constexpr double Pi = 3.14159265358979323846;

inline double degreesToRadians(double degrees)
{
    return degrees * Pi / 180.0;
}

inline double distanceMeters(const CoordinateWGS84 &first, const CoordinateWGS84 &second)
{
    const double latitude_first = degreesToRadians(first.latitude_deg);
    const double latitude_second = degreesToRadians(second.latitude_deg);
    const double latitude_delta = latitude_second - latitude_first;
    const double longitude_delta = degreesToRadians(second.longitude_deg - first.longitude_deg);

    const double sine_latitude = std::sin(latitude_delta / 2.0);
    const double sine_longitude = std::sin(longitude_delta / 2.0);
    const double haversine = sine_latitude * sine_latitude
        + std::cos(latitude_first) * std::cos(latitude_second)
        * sine_longitude * sine_longitude;
    const double bounded_haversine = qBound(0.0, haversine, 1.0);

    return 2.0 * EarthRadiusMeters * std::asin(std::sqrt(bounded_haversine));
}

inline double roundedDistanceMeters(double maximum_distance_m)
{
    if (!(maximum_distance_m > 0.0) || !std::isfinite(maximum_distance_m))
        return 0.0;

    const double magnitude = std::pow(10.0, std::floor(std::log10(maximum_distance_m)));
    const double normalized = maximum_distance_m / magnitude;

    double multiplier = 1.0;
    if (normalized >= 5.0)
        multiplier = 5.0;
    else if (normalized >= 3.0)
        multiplier = 3.0;
    else if (normalized >= 2.0)
        multiplier = 2.0;

    return multiplier * magnitude;
}

inline QString distanceLabel(double distance_m)
{
    if (distance_m >= 1000.0)
    {
        const double distance_km = distance_m / 1000.0;
        const double rounded_km = std::round(distance_km);
        if (std::abs(distance_km - rounded_km) < 1e-9)
            return QStringLiteral("%1 km").arg(qRound64(rounded_km));

        return QStringLiteral("%1 km").arg(QString::number(distance_km, 'g', 3));
    }

    const double rounded_m = std::round(distance_m);
    if (std::abs(distance_m - rounded_m) < 1e-9)
        return QStringLiteral("%1 m").arg(qRound64(rounded_m));

    return QStringLiteral("%1 m").arg(QString::number(distance_m, 'g', 3));
}
}

inline void draw(QPainter &painter, const MapModel &map_model, const QSize &viewport)
{
    if (viewport.width() <= Detail::MarginPixels * 2
        || viewport.height() <= Detail::MarginPixels * 2)
    {
        return;
    }

    const int maximum_scale_width = qMin(
        Detail::MaximumScaleWidthPixels,
        viewport.width() - Detail::MarginPixels * 2);
    if (maximum_scale_width <= 0)
        return;

    const int sample_y = viewport.height() / 2;
    const CoordinateWGS84 sample_start = map_model.wgs84FromScreen(
        QPoint(Detail::MarginPixels, sample_y), viewport);
    const CoordinateWGS84 sample_end = map_model.wgs84FromScreen(
        QPoint(Detail::MarginPixels + maximum_scale_width, sample_y), viewport);
    const double maximum_distance_m = Detail::distanceMeters(sample_start, sample_end);
    const double scale_distance_m = Detail::roundedDistanceMeters(maximum_distance_m);
    if (!(scale_distance_m > 0.0) || !(maximum_distance_m > 0.0))
        return;

    const int scale_width = qBound(
        1,
        qRound(maximum_scale_width * scale_distance_m / maximum_distance_m),
        maximum_scale_width);
    const QString label = Detail::distanceLabel(scale_distance_m);

    QFont font = painter.font();
    font.setPixelSize(11);
    const QFontMetrics font_metrics(font);
    const int text_width = font_metrics.horizontalAdvance(label);
    const int text_height = font_metrics.height();
    const int content_width = qMax(scale_width, text_width);
    const int panel_width = content_width + Detail::PanelPaddingPixels * 2;
    const int panel_height = Detail::PanelPaddingPixels * 2
        + text_height + Detail::TextGapPixels + Detail::ScaleTickHeightPixels + 2;
    const int panel_left = Detail::MarginPixels;
    const int panel_top = viewport.height() - Detail::MarginPixels - panel_height;
    const QRect panel_rect(panel_left, panel_top, panel_width, panel_height);

    const int scale_left = panel_left + Detail::PanelPaddingPixels
        + (content_width - scale_width) / 2;
    const int scale_right = scale_left + scale_width;
    const int scale_bottom = panel_rect.bottom() - Detail::PanelPaddingPixels;
    const int scale_top = scale_bottom - Detail::ScaleTickHeightPixels;
    const QRect text_rect(
        panel_left + Detail::PanelPaddingPixels,
        panel_top + Detail::PanelPaddingPixels,
        content_width,
        text_height);

    painter.save();
    painter.resetTransform();
    painter.setOpacity(1.0);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(panel_rect, QColor(255, 255, 255, 180));

    QPen scale_pen(QColor(85, 85, 85, 235));
    scale_pen.setWidth(2);
    painter.setPen(scale_pen);
    painter.drawLine(scale_left, scale_bottom, scale_right, scale_bottom);
    painter.drawLine(scale_left, scale_top, scale_left, scale_bottom);
    painter.drawLine(scale_right, scale_top, scale_right, scale_bottom);

    painter.setFont(font);
    painter.setPen(QColor(35, 35, 35));
    painter.drawText(text_rect, Qt::AlignHCenter | Qt::AlignVCenter, label);
    painter.restore();
}
}

#endif // MAP_SCALE_RENDERER_H
