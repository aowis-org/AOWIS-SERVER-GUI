#ifndef MAP_VECTOR_DOCUMENT_H
#define MAP_VECTOR_DOCUMENT_H

#include <QBrush>
#include <QImage>
#include <QList>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

class QPainter;

class MapVectorDocument
{
public:
    void addStroke(QPainterPath path, const QPen &pen);
    void addFill(QPainterPath path, const QBrush &brush);
    void addImage(const QImage &image, const QRectF &target_rect,
                  const QRectF &source_rect = QRectF());
    void paint(QPainter &painter) const;
    bool isEmpty() const;

private:
    enum class CommandType
    {
        Stroke,
        Fill,
        Image
    };

    struct Command
    {
        CommandType type = CommandType::Stroke;
        QPainterPath path;
        QPen pen;
        QBrush brush;
        QImage image;
        QRectF target_rect;
        QRectF source_rect;
    };

    QList<Command> commands;
};

#endif // MAP_VECTOR_DOCUMENT_H
