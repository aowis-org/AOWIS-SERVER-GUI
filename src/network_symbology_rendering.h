#ifndef NETWORK_SYMBOLOGY_RENDERING_H
#define NETWORK_SYMBOLOGY_RENDERING_H

#include <QColor>
#include <QString>
#include <QtGlobal>

#include <array>
#include <cmath>

constexpr int NetworkSymbologyColorBucketCount = 256;
constexpr int NetworkSymbologyRampColorCount = 7;

inline QRgb networkSymbologyDefaultColor()
{
    return qRgb(0, 0, 0);
}

inline QRgb networkSymbologyUnavailableColor()
{
    return qRgb(0, 0, 0);
}

inline qreal networkSymbologyNodeSizeScale(int node_size_percent)
{
    return qBound<qreal>(0.5, node_size_percent / 100.0, 2.5);
}

inline qreal networkSymbologyBaseMarkerSizeForZoom(int zoom)
{
    return qBound<qreal>(10.0, 10.0 + (zoom - 16) * 10.0, 40.0);
}

inline qreal networkSymbologyMarkerSizeForZoom(int zoom, int node_size_percent)
{
    return qMax<qreal>(
        5.0,
        networkSymbologyBaseMarkerSizeForZoom(zoom)
            * networkSymbologyNodeSizeScale(node_size_percent));
}

inline qreal networkSymbologyJunctionDotDiameterForZoom(int zoom, int node_size_percent)
{
    const qreal base_diameter = qBound<qreal>(
        8.0, networkSymbologyBaseMarkerSizeForZoom(zoom) * 0.3, 12.0);
    return qMax<qreal>(
        4.0, base_diameter * networkSymbologyNodeSizeScale(node_size_percent));
}

inline QColor networkSymbologyInterpolatedRampColor(double fraction)
{
    static const std::array<QColor, NetworkSymbologyRampColorCount> RampColors = {{
        QColor(QStringLiteral("#440154")),
        QColor(QStringLiteral("#443983")),
        QColor(QStringLiteral("#31688e")),
        QColor(QStringLiteral("#21918c")),
        QColor(QStringLiteral("#35b779")),
        QColor(QStringLiteral("#90d743")),
        QColor(QStringLiteral("#fde725"))
    }};

    const double limited_fraction = qBound(0.0, fraction, 1.0);
    const double scaled = limited_fraction * (RampColors.size() - 1);
    const int left_index = qMin(int(RampColors.size()) - 1, int(std::floor(scaled)));
    const int right_index = qMin(int(RampColors.size()) - 1, left_index + 1);
    const double ratio = scaled - left_index;
    const QColor &left = RampColors.at(left_index);
    const QColor &right = RampColors.at(right_index);
    return QColor(
        qRound(left.red() + (right.red() - left.red()) * ratio),
        qRound(left.green() + (right.green() - left.green()) * ratio),
        qRound(left.blue() + (right.blue() - left.blue()) * ratio));
}

inline QColor networkSymbologyRampColor(double fraction)
{
    const double limited_fraction = qBound(0.0, fraction, 1.0);
    const int bucket = qRound(limited_fraction * (NetworkSymbologyColorBucketCount - 1));
    return networkSymbologyInterpolatedRampColor(
        double(bucket) / double(NetworkSymbologyColorBucketCount - 1));
}

inline QRgb networkSymbologyColor(double value, double minimum, double maximum)
{
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum))
        return networkSymbologyUnavailableColor();
    if (minimum == maximum)
        return networkSymbologyRampColor(0.5).rgb();
    return networkSymbologyRampColor((value - minimum) / (maximum - minimum)).rgb();
}

#endif // NETWORK_SYMBOLOGY_RENDERING_H
