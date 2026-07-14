#ifndef ENTITY_MAP_LEGEND_DOCK_H
#define ENTITY_MAP_LEGEND_DOCK_H

#include <QWidget>
#include <QDockWidget>

#include <QVBoxLayout>

#include <QDebug>

#include "../_enums_structs.h"
#include "../_sizes.h"

class EntityMapLegendDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit EntityMapLegendDock(QWidget *parent = nullptr);
    
public slots:
    void showMapLegendNode(VisualNode visual_node);
    void showMapLegendLink(VisualLink visual_link);
    
private:
    VisualNode visual_node;
    VisualLink visual_link;
    
    void setVisibility();
    
signals:
    
};

#endif // ENTITY_MAP_LEGEND_DOCK_H
