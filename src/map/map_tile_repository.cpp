#include "map_tile_repository.h"

#include "../interface_server_map_rest.h"

#ifdef AOWIS_STANDALONE
#include "../interface_server_map_standalone.h"
#endif

#include <QDateTime>
#include <QList>
#include <QTimer>

namespace
{
constexpr int TileCacheMaximumCostKiB = 512 * 1024;
constexpr qint64 TileRetryInitialDelayMs = 1000;
constexpr qint64 TileRetryMaximumDelayMs = 30000;

int positiveModulo(int value, int divisor)
{
    const int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

bool tileXInsideRange(int tile_x, int tile_count, int tile_x_min, int tile_x_max)
{
    const qint64 range_width = qint64(tile_x_max) - tile_x_min + 1;
    if (range_width >= tile_count)
        return true;

    const int first_wrapped_x = positiveModulo(tile_x_min, tile_count);
    const int offset = positiveModulo(tile_x - first_wrapped_x, tile_count);
    return offset < range_width;
}
}

MapTileRepository::MapTileRepository(QObject *parent)
    : QObject(parent),
    cache(TileCacheMaximumCostKiB)
{
    initServerMapInterface();
}

const QPixmap *MapTileRepository::tile(const QString &key) const
{
    return this->cache.object(key);
}

void MapTileRepository::requestTile(const QString &endpoint, const QString &key, int x, int y)
{
    if (this->cache.contains(key) || this->tiles_pending.contains(key) || !this->interface_map)
        return;

    const QHash<QString, TileFailure>::const_iterator failure = this->tile_failures.constFind(key);
    if (failure != this->tile_failures.constEnd() &&
        QDateTime::currentMSecsSinceEpoch() < failure->retry_after_msecs)
    {
        return;
    }

    this->tiles_pending.insert(key);
    this->interface_map->requestTile(endpoint, key, x, y);
}

void MapTileRepository::deleteTiles(const QString &key_prefix, int tile_count, int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max)
{
    if (tile_count <= 0 || tile_x_min > tile_x_max || tile_y_min > tile_y_max)
        return;

    QSet<QString> candidate_keys = this->tiles_pending;
    const QList<QString> cached_keys = this->cache.keys();
    for (const QString &key : cached_keys)
        candidate_keys.insert(key);

    const QList<QString> failed_keys = this->tile_failures.keys();
    for (const QString &key : failed_keys)
        candidate_keys.insert(key);

    for (const QString &key : candidate_keys)
    {
        if (!key.startsWith(key_prefix))
            continue;

        const QString coordinates = key.mid(key_prefix.size());
        const qsizetype separator_index = coordinates.indexOf('/');
        if (separator_index <= 0 || coordinates.indexOf('/', separator_index + 1) >= 0)
            continue;

        bool tile_x_valid = false;
        bool tile_y_valid = false;
        const int tile_x = coordinates.left(separator_index).toInt(&tile_x_valid);
        const int tile_y = coordinates.mid(separator_index + 1).toInt(&tile_y_valid);
        if (!tile_x_valid || !tile_y_valid ||
            tile_y < tile_y_min || tile_y > tile_y_max ||
            !tileXInsideRange(tile_x, tile_count, tile_x_min, tile_x_max))
        {
            continue;
        }

        this->cache.remove(key);
        this->tile_failures.remove(key);

        if (this->tiles_pending.contains(key))
            this->tiles_invalidated_while_pending.insert(key);
        else
            this->tiles_invalidated_while_pending.remove(key);
    }
}

void MapTileRepository::setMapServerMode(MapServerMode mode)
{
    if (this->map_server_mode == mode)
        return;

    this->map_server_mode = mode;
    initServerMapInterface();
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
    this->tiles_invalidated_while_pending.clear();
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

    connect(this->interface_map, &InterfaceServerMap::signalTileReceived,
            this, &MapTileRepository::tileReceived);
    connect(this->interface_map, &InterfaceServerMap::signalTileFailed,
            this, &MapTileRepository::tileFailed);
}

void MapTileRepository::tileReceived(const QString &key, const QPixmap &pixmap)
{
    this->tiles_pending.remove(key);

    if (this->tiles_invalidated_while_pending.remove(key))
    {
        this->tile_failures.remove(key);
        emit signalTileRetryReady(key);
        return;
    }

    if (pixmap.isNull())
    {
        tileFailed(key);
        return;
    }

    const qint64 cost_bytes = qint64(pixmap.width()) * pixmap.height() *
                              qMax(1, pixmap.depth()) / 8;
    const qint64 cost_kib_rounded = (cost_bytes + 1023) / 1024;
    const int cost_kib = qMax(
        1, int(qMin<qint64>(cost_kib_rounded, TileCacheMaximumCostKiB)));

    if (!this->cache.insert(key, new QPixmap(pixmap), cost_kib))
    {
        tileFailed(key);
        return;
    }

    this->tile_failures.remove(key);
    emit signalTileAvailable(key);
}

void MapTileRepository::tileFailed(const QString &key)
{
    this->tiles_pending.remove(key);

    if (this->tiles_invalidated_while_pending.remove(key))
    {
        this->tile_failures.remove(key);
        emit signalTileRetryReady(key);
        return;
    }

    TileFailure failure = this->tile_failures.value(key);
    failure.count = qMin(failure.count + 1, 6);

    const qint64 delay_multiplier = qint64(1) << (failure.count - 1);
    const qint64 retry_delay = qMin(
        TileRetryInitialDelayMs * delay_multiplier, TileRetryMaximumDelayMs);
    failure.retry_after_msecs = QDateTime::currentMSecsSinceEpoch() + retry_delay;

    this->tile_failures.insert(key, failure);
    const qint64 retry_after_msecs = failure.retry_after_msecs;
    QTimer::singleShot(int(retry_delay), this, [this, key, retry_after_msecs]
    {
        const QHash<QString, TileFailure>::const_iterator current_failure =
            this->tile_failures.constFind(key);
        if (current_failure == this->tile_failures.constEnd() ||
            current_failure->retry_after_msecs != retry_after_msecs)
        {
            return;
        }

        emit signalTileRetryReady(key);
    });
}
