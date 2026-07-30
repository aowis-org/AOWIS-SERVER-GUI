#include "map_tile_repository.h"

#include "../interface_server_map_rest.h"

#ifdef AOWIS_STANDALONE
#include "../interface_server_map_standalone.h"
#endif

#include <QDateTime>
#include <QTimer>

namespace
{
constexpr int TileCacheMaximumCostKiB = 512 * 1024;
constexpr qint64 TileRetryInitialDelayMs = 1000;
constexpr qint64 TileRetryMaximumDelayMs = 30000;
}

MapTileRepository::MapTileRepository(QObject *parent)
    : QObject(parent),
    cache(TileCacheMaximumCostKiB)
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

    const auto failure = this->tile_failures.constFind(key);
    if (failure != this->tile_failures.constEnd())
    {
        if (QDateTime::currentMSecsSinceEpoch() < failure->retry_after_msecs)
            return;
    }

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
    this->tile_failures.clear();

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
    connect(this->interface_map, &InterfaceServerMap::signalTileFailed, this, &MapTileRepository::tileFailed);
}

void MapTileRepository::tileReceived(const QString &key, QPixmap *pixmap)
{
    this->tiles_pending.remove(key);

    if (!pixmap || pixmap->isNull())
    {
        delete pixmap;
        this->tileFailed(key);
        return;
    }

    const qint64 cost_bytes = qint64(pixmap->width()) * pixmap->height() * qMax(1, pixmap->depth()) / 8;
    const qint64 cost_kib_rounded = (cost_bytes + 1023) / 1024;
    const int cost_kib = qMax(1, int(qMin<qint64>(cost_kib_rounded, TileCacheMaximumCostKiB)));

    if (!this->cache.insert(key, pixmap, cost_kib))
    {
        this->tileFailed(key);
        return;
    }

    this->tile_failures.remove(key);
    emit signalTileAvailable(key);
}

void MapTileRepository::tileFailed(const QString &key)
{
    this->tiles_pending.remove(key);

    TileFailure failure = this->tile_failures.value(key);
    failure.count = qMin(failure.count + 1, 6);

    const qint64 delay_multiplier = qint64(1) << (failure.count - 1);
    const qint64 retry_delay = qMin(TileRetryInitialDelayMs * delay_multiplier, TileRetryMaximumDelayMs);
    failure.retry_after_msecs = QDateTime::currentMSecsSinceEpoch() + retry_delay;

    this->tile_failures.insert(key, failure);
    emit signalTileFailed(key);

    const qint64 retry_after_msecs = failure.retry_after_msecs;
    QTimer::singleShot(int(retry_delay), this, [this, key, retry_after_msecs]
    {
        const auto current_failure = this->tile_failures.constFind(key);
        if (current_failure == this->tile_failures.constEnd() || current_failure->retry_after_msecs != retry_after_msecs)
            return;

        emit signalTileRetryReady(key);
    });
}
