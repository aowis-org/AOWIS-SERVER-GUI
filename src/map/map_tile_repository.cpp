#include "map_tile_repository.h"

#include "../interface_server_map_rest.h"

#ifdef AOWIS_STANDALONE
#include "../interface_server_map_standalone.h"
#endif

#include <QDateTime>
#include <QDebug>
#include <QList>
#include <QTimer>
#ifdef Q_OS_WIN
#include <QImage>
#include <QMetaObject>
#include <QRunnable>
#endif

namespace
{
constexpr int TileCacheMaximumCostKiB = 512 * 1024;
constexpr qint64 TileRetryInitialDelayMs = 1000;
constexpr qint64 TileRetryMaximumDelayMs = 30000;
#ifdef Q_OS_WIN
constexpr int TileRequestDispatchIntervalMs = 1;
constexpr int TileDecodeThreadCount = 2;
#endif

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
#ifdef Q_OS_WIN
    this->tile_request_timer = new QTimer(this);
    this->tile_request_timer->setSingleShot(true);
    this->tile_request_timer->setTimerType(Qt::PreciseTimer);
    this->tile_request_timer->setInterval(TileRequestDispatchIntervalMs);
    connect(this->tile_request_timer, &QTimer::timeout, this, &MapTileRepository::processTileRequestQueue);

    this->tile_decode_pool.setMaxThreadCount(TileDecodeThreadCount);
    this->tile_decode_pool.setExpiryTimeout(30000);
#endif

    initServerMapInterface();
}

MapTileRepository::~MapTileRepository()
{
#ifdef Q_OS_WIN
    this->tile_decode_pool.clear();
    this->tile_decode_pool.waitForDone();
#endif
}

const QPixmap *MapTileRepository::tile(const QString &key) const
{
    return this->cache.object(key);
}

void MapTileRepository::requestTile(const QString &endpoint, const QString &key, int x, int y)
{
    if (this->cache.contains(key) || this->tiles_pending.contains(key) ||
        !this->interface_map || tileDeletionPending(key, x, y))
    {
        return;
    }

    const QHash<QString, TileFailure>::const_iterator failure = this->tile_failures.constFind(key);
    if (failure != this->tile_failures.constEnd() &&
        QDateTime::currentMSecsSinceEpoch() < failure->retry_after_msecs)
    {
        return;
    }

    this->tiles_pending.insert(key);
#ifdef Q_OS_WIN
    PendingTileRequest request;
    request.endpoint = endpoint;
    request.x = x;
    request.y = y;

    this->tile_requests_queued.insert(key, request);
    this->tile_request_order.append(key);

    if (!this->tile_request_timer->isActive())
        this->tile_request_timer->start();
#else
    this->interface_map->requestTile(endpoint, key, x, y);
#endif
}

#ifdef Q_OS_WIN
void MapTileRepository::processTileRequestQueue()
{
    while (!this->tile_request_order.isEmpty())
    {
        const QString key = this->tile_request_order.takeLast();
        const QHash<QString, PendingTileRequest>::iterator request_iterator =
            this->tile_requests_queued.find(key);
        if (request_iterator == this->tile_requests_queued.end())
            continue;

        const PendingTileRequest request = request_iterator.value();
        this->tile_requests_queued.erase(request_iterator);

        if (!this->interface_map || this->cache.contains(key) ||
            tileDeletionPending(key, request.x, request.y))
        {
            this->tiles_pending.remove(key);
        }
        else
        {
            this->interface_map->requestTile(request.endpoint, key, request.x, request.y);
        }

        break;
    }

    if (!this->tile_request_order.isEmpty())
        this->tile_request_timer->start();
}
#endif

void MapTileRepository::deleteTiles(const QString &provider, int zoom,
                                    int tile_x_min, int tile_x_max,
                                    int tile_y_min, int tile_y_max)
{
    if (provider.isEmpty() || zoom < 0 || zoom > 30 ||
        tile_x_min > tile_x_max || tile_y_min > tile_y_max)
    {
        return;
    }

    PendingTileDeletion deletion;
    deletion.key_prefix = QString("%1/%2/").arg(provider).arg(zoom);
    deletion.tile_count = 1 << zoom;
    deletion.tile_x_min = tile_x_min;
    deletion.tile_x_max = tile_x_max;
    deletion.tile_y_min = tile_y_min;
    deletion.tile_y_max = tile_y_max;
    invalidateTiles(deletion);

    if (!this->interface_map)
    {
        emit signalTileRetryReady(deletion.key_prefix);
        return;
    }

    quint64 request_id = this->next_tile_deletion_id++;
    if (request_id == 0)
        request_id = this->next_tile_deletion_id++;

    this->tile_deletions_pending.insert(request_id, deletion);
    this->interface_map->deleteTiles(
        request_id, provider, zoom,
        tile_x_min, tile_x_max, tile_y_min, tile_y_max);
}

void MapTileRepository::invalidateTiles(const PendingTileDeletion &deletion)
{
    QSet<QString> candidate_keys = this->tiles_pending;
    const QList<QString> cached_keys = this->cache.keys();
    for (const QString &key : cached_keys)
        candidate_keys.insert(key);

    const QList<QString> failed_keys = this->tile_failures.keys();
    for (const QString &key : failed_keys)
        candidate_keys.insert(key);

    for (const QString &key : candidate_keys)
    {
        if (!key.startsWith(deletion.key_prefix))
            continue;

        const QString coordinates = key.mid(deletion.key_prefix.size());
        const qsizetype separator_index = coordinates.indexOf('/');
        if (separator_index <= 0 || coordinates.indexOf('/', separator_index + 1) >= 0)
            continue;

        bool tile_x_valid = false;
        bool tile_y_valid = false;
        const int tile_x = coordinates.left(separator_index).toInt(&tile_x_valid);
        const int tile_y = coordinates.mid(separator_index + 1).toInt(&tile_y_valid);
        if (!tile_x_valid || !tile_y_valid ||
            tile_y < deletion.tile_y_min || tile_y > deletion.tile_y_max ||
            !tileXInsideRange(tile_x, deletion.tile_count,
                              deletion.tile_x_min, deletion.tile_x_max))
        {
            continue;
        }

        this->cache.remove(key);
        this->tile_failures.remove(key);

#ifdef Q_OS_WIN
        if (this->tile_requests_queued.remove(key) > 0)
        {
            this->tiles_pending.remove(key);
            this->tiles_invalidated_while_pending.remove(key);
            continue;
        }
#endif
        if (this->tiles_pending.contains(key))
            this->tiles_invalidated_while_pending.insert(key);
        else
            this->tiles_invalidated_while_pending.remove(key);
    }
}

bool MapTileRepository::tileDeletionPending(const QString &key, int x, int y) const
{
    QHash<quint64, PendingTileDeletion>::const_iterator iterator =
        this->tile_deletions_pending.constBegin();
    while (iterator != this->tile_deletions_pending.constEnd())
    {
        const PendingTileDeletion &deletion = iterator.value();
        if (key.startsWith(deletion.key_prefix) &&
            y >= deletion.tile_y_min && y <= deletion.tile_y_max &&
            tileXInsideRange(x, deletion.tile_count,
                             deletion.tile_x_min, deletion.tile_x_max))
        {
            return true;
        }

        ++iterator;
    }

    return false;
}

void MapTileRepository::finishTileDeletion(quint64 request_id, const QString &error)
{
    QHash<quint64, PendingTileDeletion>::iterator iterator =
        this->tile_deletions_pending.find(request_id);
    if (iterator == this->tile_deletions_pending.end())
        return;

    const QString retry_key = iterator.value().key_prefix;
    this->tile_deletions_pending.erase(iterator);

    if (!error.isEmpty())
        qWarning() << "Tile cache deletion failed:" << error;

    emit signalTileRetryReady(retry_key);
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

    this->tile_generation++;
#ifdef Q_OS_WIN
    this->tile_request_timer->stop();
    this->tile_requests_queued.clear();
    this->tile_request_order.clear();
#endif
    this->tiles_pending.clear();
    this->tiles_invalidated_while_pending.clear();
    this->tile_failures.clear();
    this->tile_deletions_pending.clear();

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

    connect(this->interface_map, &InterfaceServerMap::signalTileDataReceived,
            this, &MapTileRepository::tileDataReceived);
    connect(this->interface_map, &InterfaceServerMap::signalTileFailed,
            this, &MapTileRepository::tileFailed);
    connect(this->interface_map, &InterfaceServerMap::signalTilesDeleted,
            this, [this](quint64 request_id)
    {
        finishTileDeletion(request_id);
    });
    connect(this->interface_map, &InterfaceServerMap::signalTileDeletionFailed,
            this, [this](quint64 request_id, const QString &error)
    {
        finishTileDeletion(request_id, error);
    });
}

void MapTileRepository::tileDataReceived(const QString &key, const QByteArray &data)
{
    if (!this->tiles_pending.contains(key))
        return;

    if (data.isEmpty())
    {
        tileFailed(key);
        return;
    }

#ifdef Q_OS_WIN
    const quint64 generation = this->tile_generation;
    MapTileRepository *repository = this;
    this->tile_decode_pool.start(QRunnable::create([repository, key, data, generation]
    {
        QImage image;
        image.loadFromData(data);
        if (!image.isNull() && image.format() != QImage::Format_ARGB32_Premultiplied)
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

        QMetaObject::invokeMethod(repository, [repository, key, generation, image]
        {
            repository->finishTileDecode(key, generation, image);
        }, Qt::QueuedConnection);
    }));
#else
    QPixmap pixmap;
    pixmap.loadFromData(data);
    finishTileDecode(key, pixmap);
#endif
}

#ifdef Q_OS_WIN
void MapTileRepository::finishTileDecode(const QString &key, quint64 generation, const QImage &image)
{
    if (generation != this->tile_generation || !this->tiles_pending.contains(key))
        return;

    const QPixmap pixmap = QPixmap::fromImage(image);
#else
void MapTileRepository::finishTileDecode(const QString &key, const QPixmap &pixmap)
{
    if (!this->tiles_pending.contains(key))
        return;
#endif
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
