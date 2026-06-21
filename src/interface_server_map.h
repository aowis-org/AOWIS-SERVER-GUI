#ifndef INTERFACE_SERVER_MAP_H
#define INTERFACE_SERVER_MAP_H

#include <QObject>

class InterfaceServerMap : public QObject
{
    Q_OBJECT
public:
    explicit InterfaceServerMap(QObject *parent = nullptr)
        : QObject(parent)
    {
        
    }

signals:
};

#endif // INTERFACE_SERVER_MAP_H
