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
// Persists to the same config file mapServerClientConfiguration() reads at
// startup, but does NOT update the live (cached) configuration returned by
// mapServerClientConfiguration() -- the map server client and REST/tile
// fetch code read that once at construction, so a restart is required for
// a saved change to actually take effect. Returns false if the write
// failed.
bool saveMapServerClientConfiguration(const MapServerClientConfiguration &configuration);

#endif // MAP_SERVER_CLIENT_CONFIGURATION_H
