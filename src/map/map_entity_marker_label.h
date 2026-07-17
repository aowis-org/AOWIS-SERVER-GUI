#ifndef MAP_ENTITY_MARKER_LABEL_H
#define MAP_ENTITY_MARKER_LABEL_H

#include <QLabel>
#include <QPointer>
#include <QMessageBox>

class QMouseEvent;
class QContextMenuEvent;

class MapEntityMarkerLabel : public QLabel
{
    Q_OBJECT
    
public:
    explicit MapEntityMarkerLabel(QWidget *parent = nullptr);
    
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    
signals:
    void signalClicked(MapEntityMarkerLabel *label);
    void signalMoveRequested(MapEntityMarkerLabel *label);
    void signalDeleteRequested(MapEntityMarkerLabel *label);
};

#endif // MAP_ENTITY_MARKER_LABEL_H
