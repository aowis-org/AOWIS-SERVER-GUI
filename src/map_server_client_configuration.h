#ifndef MAP_SERVER_CLIENT_CONFIGURATION_H
#define MAP_SERVER_CLIENT_CONFIGURATION_H

#include <QByteArray>
#include <QString>

struct MapServerClientConfiguration
{
    QString base_url;
    QByteArray api_key;
    QByteArray delete_api_key;
};

const MapServerClientConfiguration &mapServerClientConfiguration();

#endif // MAP_SERVER_CLIENT_CONFIGURATION_H
