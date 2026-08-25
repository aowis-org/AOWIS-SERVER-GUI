#ifndef INTERFACE_SERVER_MAP_H
#define INTERFACE_SERVER_MAP_H

#include <QByteArray>
#include <QObject>
#include <QString>

class InterfaceServerMap : public QObject
{
    Q_OBJECT

public:
    explicit InterfaceServerMap(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    virtual void requestTile(const QString &endpoint, const QString &key, int x, int y) = 0;
    virtual void requestTerrainTile(const QString &endpoint, const QString &key)
    {
        Q_UNUSED(endpoint)
        emit signalTerrainTileFailed(
            key, QStringLiteral("Terrain transport is not available in this map-server mode"));
    }
    virtual void deleteTiles(quint64 request_id, const QString &provider, int zoom,
                             int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max) = 0;

signals:
    void signalTileDataReceived(const QString &key, const QByteArray &data);
    void signalTileFailed(const QString &key);
    void signalTerrainTileDataReceived(const QString &key, const QByteArray &data);
    void signalTerrainTileFailed(const QString &key, const QString &error);
    void signalTilesDeleted(quint64 request_id);
    void signalTileDeletionFailed(quint64 request_id, const QString &error);
};

#endif // INTERFACE_SERVER_MAP_H
