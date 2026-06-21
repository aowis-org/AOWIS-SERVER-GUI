#ifndef INTERFACE_SERVER_MAP_H
#define INTERFACE_SERVER_MAP_H

#include <QObject>

class InterfaceServerMap : public QObject
{
    Q_OBJECT
public:
    explicit InterfaceServerMap(QObject *parent = nullptr)
        : QObject(parent)
    {
        
    }
    
    virtual void requestTile(const QString &endpoint, const QString &key, int x, int y) = 0;

signals:
    void signalTileReceived(const QString &key, QPixmap *pix);
};

#endif // INTERFACE_SERVER_MAP_H
