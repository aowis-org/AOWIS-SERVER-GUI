#ifndef INTERFACE_SERVER_MAP_REST_H
#define INTERFACE_SERVER_MAP_REST_H

#include <QObject>
#include <QPixmap>

#include "interface_server_map.h"
#include "rest_client.h"

#include <QDebug>

class InterfaceServerMapREST : public InterfaceServerMap
{
    Q_OBJECT
public:
    explicit InterfaceServerMapREST(QObject *parent = nullptr);
    
    void requestTile(const QString &endpoint, const QString &key, int x, int y) override;
    
private:
    RESTClient *rest = nullptr;
    void initRestConnection();
    
    QSet<QString> rest_pending;
    
signals:
    
};

#endif // INTERFACE_SERVER_MAP_REST_H
