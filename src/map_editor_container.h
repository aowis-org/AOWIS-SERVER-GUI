#ifndef MAP_EDITOR_CONTAINER_H
#define MAP_EDITOR_CONTAINER_H

#include <QObject>
#include <QWidget>

class MapEditorContainer : public QWidget
{
    Q_OBJECT
public:
    explicit MapEditorContainer(QWidget *parent = nullptr);

signals:
};

#endif // MAP_EDITOR_CONTAINER_H
