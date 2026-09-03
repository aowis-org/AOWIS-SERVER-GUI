#ifndef GPS_PROVIDER_DUMMY_H
#define GPS_PROVIDER_DUMMY_H

#include <QObject>
#include <QString>
#include <QtGlobal>

class QGeoPositionInfo;
class QSerialPortInfo;

class GpsProvider : public QObject
{
    Q_OBJECT
    
public:
    explicit GpsProvider(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    
    void start()
    {
        emit statusMessage(QStringLiteral("GPS is not available in the WASM build."));
        emit gpsDisconnected();
    }
    
    void startGpsd(const QString &host = QStringLiteral("127.0.0.1"),
                   quint16 port = 2947)
    {
        Q_UNUSED(host)
        Q_UNUSED(port)
        
        emit statusMessage(QStringLiteral("gpsd is not available in the WASM build."));
        emit gpsDisconnected();
    }
    
    void startSerial()
    {
        emit statusMessage(QStringLiteral("Serial GPS is not available in the WASM build."));
        emit gpsDisconnected();
    }
    
    void stop()
    {
        emit gpsDisconnected();
    }
    
    void setPreferredPortName(const QString &portName)
    {
        Q_UNUSED(portName)
    }
    
signals:
    void positionChanged(const QGeoPositionInfo &info);
    void gpsConnected(const QString &portName);
    void gpsDisconnected();
    void statusMessage(const QString &message);
    
private:
    enum class Backend
    {
        None,
        Gpsd,
        Serial
    };
    
private:
    void handleRetryTimer()
    {
    }
    
    void tryConnectGps()
    {
    }
    
    void cleanupSerialGps()
    {
    }
    
    bool looksLikeGpsPort(const QSerialPortInfo &info) const
    {
        Q_UNUSED(info)
        return false;
    }
    
    void connectGpsd()
    {
    }
    
    void readGpsdData()
    {
    }
    
    void cleanupGpsd()
    {
    }
    
    void cleanupGps()
    {
    }
};

#endif // GPS_PROVIDER_DUMMY_H
