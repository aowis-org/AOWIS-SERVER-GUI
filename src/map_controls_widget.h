#ifndef MAP_CONTROLS_WIDGET_H
#define MAP_CONTROLS_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QVBoxLayout>
#include <QGroupBox>

#include "map_widget.h"

class MapControlsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapControlsWidget(MapWidget *map, QWidget *parent = nullptr);
    
private:
    QVBoxLayout *layout;
    
    MapWidget *map;
    
    void addGroupMapControls();
    void addGroupNodeVisuals();
    void addGroupLinkVisuals();
    
    void makeGroupCollapsable(QGroupBox *group);    
    
signals:
    void mapZoomIn();
    void mapZoomOut();
};

#endif // MAP_CONTROLS_WIDGET_H
