#include "map_entity_marker_label.h"

#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QCursor>
#include <QGraphicsDropShadowEffect>
#include <QColor>
#include <QPainter>

namespace
{
    class StrongGlowEffect final : public QGraphicsEffect
    {
    public:
        explicit StrongGlowEffect(const QColor &color, QObject *parent = nullptr)
            : QGraphicsEffect(parent), color(color)
        {
        }
        
    protected:
        QRectF boundingRectFor(const QRectF &sourceRect) const override
        {
            return sourceRect.adjusted(-8.0, -8.0, 8.0, 8.0);
        }
        
        void draw(QPainter *painter) override
        {
            QPoint offset;
            QPixmap source = sourcePixmap(Qt::LogicalCoordinates, &offset, QGraphicsEffect::PadToEffectiveBoundingRect);
            if (source.isNull())
                return;
            
            QPixmap glow(source.size());
            glow.fill(Qt::transparent);
            
            QPainter glow_painter(&glow);
            glow_painter.drawPixmap(0, 0, source);
            glow_painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            glow_painter.fillRect(glow.rect(), this->color);
            glow_painter.end();
            
            static const QPoint glow_offsets[] = {
                QPoint(-6, 0), QPoint(6, 0), QPoint(0, -6), QPoint(0, 6),
                QPoint(-5, -3), QPoint(-5, 3), QPoint(5, -3), QPoint(5, 3),
                QPoint(-3, -5), QPoint(-3, 5), QPoint(3, -5), QPoint(3, 5),
                QPoint(-4, 0), QPoint(4, 0), QPoint(0, -4), QPoint(0, 4),
                QPoint(-3, -3), QPoint(-3, 3), QPoint(3, -3), QPoint(3, 3),
                QPoint(-2, 0), QPoint(2, 0), QPoint(0, -2), QPoint(0, 2),
                QPoint(-2, -2), QPoint(-2, 2), QPoint(2, -2), QPoint(2, 2)
            };
            
            painter->save();
            painter->setOpacity(0.8);
            for (const QPoint &glow_offset : glow_offsets)
                painter->drawPixmap(offset + glow_offset, glow);
            painter->restore();
            painter->drawPixmap(offset, source);
        }
        
    private:
        QColor color;
    };
    
    void applyHighlightGlow(QLabel *label, const QColor &color)
    {
        label->setGraphicsEffect(nullptr);
        label->setGraphicsEffect(new StrongGlowEffect(color, label));
    }
}

MapEntityMarkerLabel::MapEntityMarkerLabel(QWidget *parent)
    : QLabel(parent)
{
    this->setCursor(Qt::PointingHandCursor);
    this->setContextMenuPolicy(Qt::DefaultContextMenu);
    this->setAttribute(Qt::WA_NoMousePropagation, true);
}

void MapEntityMarkerLabel::setHighlightSelected()
{
    applyHighlightGlow(this, QColor(0, 190, 255, 255));
}

void MapEntityMarkerLabel::setHighlightError()
{
    applyHighlightGlow(this, QColor(255, 0, 0, 255));
}

void MapEntityMarkerLabel::clearHighlight()
{
    this->setGraphicsEffect(nullptr);
}

void MapEntityMarkerLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit signalClicked(this);
    
    event->accept();
}

void MapEntityMarkerLabel::mouseMoveEvent(QMouseEvent *event)
{
    event->accept();
}

void MapEntityMarkerLabel::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
}

void MapEntityMarkerLabel::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit signalClicked(this);
    
    event->accept();
}

void MapEntityMarkerLabel::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    
    QAction *action_move = menu->addAction("Move");
    QAction *action_delete = menu->addAction("Delete");
    
    connect(action_move, &QAction::triggered, this, [this]()
    {
        emit signalMoveRequested(this);
    });
    
    connect(action_delete, &QAction::triggered, this, [this]()
    {
        QPointer<MapEntityMarkerLabel> label_this(this);
        
        QMessageBox *box = new QMessageBox(this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        
        box->setIcon(QMessageBox::Question);
        box->setWindowTitle("Delete entity");
        box->setText("Do you really want to delete this entity?");
        box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box->setDefaultButton(QMessageBox::No);
        
        connect(box, &QMessageBox::buttonClicked, this,
        [label_this, box](QAbstractButton *button)
        {
            if (!label_this)
                return;
            
            if (box->standardButton(button) == QMessageBox::Yes)
            {
                emit label_this->signalDeleteRequested(label_this);
            }
        });
        
        box->open();
    });
    
    menu->popup(event->globalPos());
    
    event->accept();
}
