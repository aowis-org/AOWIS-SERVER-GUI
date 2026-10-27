#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>

// One revision card owns its fixed two-line header and zero or more branch
// HEAD decorations. Only the badge area grows as references are added; the
// title and the hash must never move down into it.
namespace ProjectRevisionCardGeometry
{
constexpr double width = 224.0;
constexpr double base_height = 84.0;
constexpr double head_row_pitch = 24.0;
constexpr double first_head_top = 68.0;
constexpr double head_row_height = 20.0;

inline double height(int head_count)
{
    return base_height + head_row_pitch * (head_count > 0 ? head_count : 0);
}

inline QRectF bounds(const QPointF &center, int head_count)
{
    const double card_height = height(head_count);
    return QRectF(center.x() - width / 2.0, center.y() - card_height / 2.0,
                  width, card_height);
}

inline QRectF titleRect(const QRectF &card)
{
    return QRectF(card.left() + 16.0, card.top() + 10.0,
                  card.width() - 28.0, 24.0);
}

inline QRectF revisionIdRect(const QRectF &card)
{
    return QRectF(card.left() + 16.0, card.top() + 38.0,
                  card.width() - 28.0, 20.0);
}

inline QRectF headRect(const QRectF &card, int index)
{
    return QRectF(card.left() + 14.0,
                  card.top() + first_head_top + head_row_pitch * index,
                  card.width() - 28.0, head_row_height);
}

// Project-backed data commonly labels a revision "Revision <short hash>".
// The second header line already contains that ID, so display it only once.
// Keep other descriptive titles (including translated ones) intact.
inline QString distinctTitle(const QString &title, const QString &short_id)
{
    if (!short_id.isEmpty() && title.endsWith(short_id))
    {
        const QString trimmed = title.left(title.size() - short_id.size()).trimmed();
        if (!trimmed.isEmpty())
            return trimmed;
    }
    return title;
}
}
