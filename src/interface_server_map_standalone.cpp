#include "interface_server_map_standalone.h"



InterfaceServerMapStandalone::InterfaceServerMapStandalone(QObject *parent)
    : InterfaceServerMap(parent),
    map_tiles(new MapTiles(this))
{
    connect(this->map_tiles, &MapTiles::tileReady, this, [this](const QString &key, const QByteArray &data)
            {
                if (data.isEmpty())
                {
                    qWarning() << "Downloaded tile is empty:" << key;
                    return;
                }
                
                QPixmap pix;
                
                if (!pix.loadFromData(data))
                {
                    qWarning() << "Downloaded tile decode failed:" << key;
                    return;
                }
                
                QPixmap *result = new QPixmap(pix);
                
                QTimer::singleShot(0, this, [this, key, result]()
                                   {
                                       emit signalTileReceived(key, result);
                                   });
            },
            Qt::QueuedConnection);
    
    connect(this->map_tiles, &MapTiles::tileFailed, this, [this](const QString &key)
            {
                emit signalTileFailed(key);
            });
}

void InterfaceServerMapStandalone::requestTile(QString endpoint, const QString &key, int x, int y)
{
    endpoint.remove(".png");
    
    QStringList parts = endpoint.split("/", Qt::SkipEmptyParts);
    
    if (parts.size() < 2)
    {
        qWarning() << "Invalid endpoint:" << endpoint;
        return;
    }
    
    QString provider = parts[0];
    int zoom = parts[1].toInt();
    
    QByteArray data = this->map_tiles->getTile(provider, zoom, x, y, key);
    
    if (data.isEmpty())
    {
        return;
    }
    
    QPixmap pix;
    if (!pix.loadFromData(data))
    {
        qWarning() << "Tile decode failed:" << key;
        return;
    }
    
    QPixmap *result = new QPixmap(pix);
    
    QTimer::singleShot(0, this, [this, key, result]()
                       {
                           emit signalTileReceived(key, result);
                       });
}


