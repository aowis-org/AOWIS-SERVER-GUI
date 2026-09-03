#include "map/render/map_vector_document.h"

#include <QPainter>

#include <utility>

void MapVectorDocument::addStroke(QPainterPath path, const QPen &pen)
{
    if (path.isEmpty() || pen.style() == Qt::NoPen)
        return;

    Command command;
    command.type = CommandType::Stroke;
    command.path = std::move(path);
    command.pen = pen;
    this->commands.append(std::move(command));
}

void MapVectorDocument::addFill(QPainterPath path, const QBrush &brush)
{
    if (path.isEmpty() || brush.style() == Qt::NoBrush)
        return;

    Command command;
    command.type = CommandType::Fill;
    command.path = std::move(path);
    command.brush = brush;
    this->commands.append(std::move(command));
}

void MapVectorDocument::addImage(const QImage &image, const QRectF &target_rect,
                                 const QRectF &source_rect)
{
    if (image.isNull() || target_rect.isEmpty())
        return;

    Command command;
    command.type = CommandType::Image;
    command.image = image;
    command.target_rect = target_rect;
    command.source_rect = source_rect.isEmpty() ? QRectF(image.rect()) : source_rect;
    this->commands.append(std::move(command));
}

void MapVectorDocument::paint(QPainter &painter) const
{
    painter.save();
    for (const Command &command : this->commands)
    {
        switch (command.type)
        {
            case CommandType::Stroke:
                painter.setPen(command.pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(command.path);
                break;
            case CommandType::Fill:
                painter.setPen(Qt::NoPen);
                painter.setBrush(command.brush);
                painter.drawPath(command.path);
                break;
            case CommandType::Image:
                painter.drawImage(command.target_rect, command.image, command.source_rect);
                break;
        }
    }
    painter.restore();
}

bool MapVectorDocument::isEmpty() const
{
    return this->commands.isEmpty();
}
