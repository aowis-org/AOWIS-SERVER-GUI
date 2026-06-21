#ifndef INTERFACE_SERVER_MAP_STANDALONE_H
#define INTERFACE_SERVER_MAP_STANDALONE_H

#include <QObject>

class InterfaceServerMapStandalone : public QObject
{
    Q_OBJECT
public:
    explicit InterfaceServerMapStandalone(QObject *parent = nullptr);

signals:
};

#endif // INTERFACE_SERVER_MAP_STANDALONE_H
