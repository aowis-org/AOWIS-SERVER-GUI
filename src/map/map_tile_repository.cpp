#include "map_tile_repository.h"

#include "../interface_server_map_rest.h"

#ifdef AOWIS_STANDALONE
#include "../interface_server_map_standalone.h"
#endif

MapTileRepository::MapTileRepository(QObject *parent)
    : QObject(parent),
    cache(2000)
{
    this->initServerMapInterface();
}

QPixmap *MapTileRepository::tile(const QString &key) const
{
    return this->cache.object(key);
}

void MapTileRepository::requestTile(const QString &endpoint, const QString &key, int x, int y)
{
    if (this->cache.contains(key) || this->tiles_pending.contains(key) || !this->interface_map)
        return;

    this->tiles_pending.insert(key);
    this->interface_map->requestTile(endpoint, key, x, y);
}

void MapTileRepository::setMapServerMode(MapServerMode mode)
{
    if (this->map_server_mode == mode)
        return;

    this->map_server_mode = mode;
    this->initServerMapInterface();
}

void MapTileRepository::initServerMapInterface()
{
    if (this->interface_map)
    {
        this->interface_map->disconnect(this);
        this->interface_map->deleteLater();
        this->interface_map = nullptr;
    }

    this->tiles_pending.clear();

    if (this->map_server_mode == MapServerMode::REST)
    {
        this->interface_map = new InterfaceServerMapREST(this);
    }
    else if (this->map_server_mode == MapServerMode::Standalone)
    {
#ifdef AOWIS_STANDALONE
        this->interface_map = new InterfaceServerMapStandalone(this);
#endif
    }

    if (!this->interface_map)
        return;

    connect(this->interface_map, &InterfaceServerMap::signalTileReceived, this, &MapTileRepository::tileReceived);
    connect(this->interface_map, &InterfaceServerMap::signalTileFailed, this, [this](const QString &key)
    {
        this->tiles_pending.remove(key);
        emit signalTileFailed(key);
    });
}

void MapTileRepository::tileReceived(const QString &key, QPixmap *pixmap)
{
    this->tiles_pending.remove(key);

    if (!pixmap)
    {
        emit signalTileFailed(key);
        return;
    }

    this->cache.insert(key, pixmap);
    emit signalTileAvailable(key);
}
