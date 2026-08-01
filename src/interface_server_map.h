#ifndef INTERFACE_SERVER_MAP_H
#define INTERFACE_SERVER_MAP_H

#include <QObject>
#include <QPixmap>
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
    virtual void deleteTiles(quint64 request_id, const QString &provider, int zoom,
                             int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max) = 0;

signals:
    void signalTileReceived(const QString &key, const QPixmap &pixmap);
    void signalTileFailed(const QString &key);
    void signalTilesDeleted(quint64 request_id);
    void signalTileDeletionFailed(quint64 request_id, const QString &error);
};

#endif // INTERFACE_SERVER_MAP_H
