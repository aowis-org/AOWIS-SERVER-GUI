#include "map_entity_marker_label.h"

#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QCursor>

MapEntityMarkerLabel::MapEntityMarkerLabel(QWidget *parent)
    : QLabel(parent)
{
    setCursor(Qt::PointingHandCursor);
    setContextMenuPolicy(Qt::DefaultContextMenu);
}

void MapEntityMarkerLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        emit signalClicked(this);
        event->accept();
        return;
    }
    
    QLabel::mousePressEvent(event);
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
