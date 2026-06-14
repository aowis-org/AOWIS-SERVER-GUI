#ifndef MAP_MODEL_H
#define MAP_MODEL_H

#include <QObject>

class MapModel : public QObject
{
    Q_OBJECT
public:
    explicit MapModel(QObject *parent = nullptr);

signals:
};

#endif // MAP_MODEL_H
