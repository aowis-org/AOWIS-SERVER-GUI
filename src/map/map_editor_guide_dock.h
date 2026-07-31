#ifndef MAP_EDITOR_GUIDE_DOCK_H
#define MAP_EDITOR_GUIDE_DOCK_H

#include <QDockWidget>

class MapEditorGuideDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit MapEditorGuideDock(QWidget *parent = nullptr);

public slots:
    void setMapEditorActive(bool active);
};

#endif // MAP_EDITOR_GUIDE_DOCK_H
