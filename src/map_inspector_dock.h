#ifndef MAP_INSPECTOR_DOCK_H
#define MAP_INSPECTOR_DOCK_H

#include <QDockWidget>

class MapInspectorDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit MapInspectorDock(QWidget *parent = nullptr);
};

#endif // MAP_INSPECTOR_DOCK_H
