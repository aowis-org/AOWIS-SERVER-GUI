#ifndef GPS_PROVIDER_H
#define GPS_PROVIDER_H

#include <QObject>
#include <QTimer>
#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <QGeoPositionInfo>

class QSerialPort;
class QSerialPortInfo;
class QNmeaPositionInfoSource;
class QTcpSocket;

class GpsProvider : public QObject
{
    Q_OBJECT
    
public:
    explicit GpsProvider(QObject *parent = nullptr);
    
    // Default mode. Since xgps works for you, this starts gpsd.
    void start();
    
    // gpsd backend: reads GPS data from localhost:2947
    void startGpsd(const QString &host = QStringLiteral("127.0.0.1"),
                   quint16 port = 2947);
    
    // direct serial backend: opens /dev/ttyACM0, /dev/ttyUSB0, COM3, etc.
    void startSerial();
    
    void stop();
    
    void setPreferredPortName(const QString &portName);
    
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
    void handleRetryTimer();
    
    // serial/NMEA backend
    void tryConnectGps();
    void cleanupSerialGps();
    bool looksLikeGpsPort(const QSerialPortInfo &info) const;
    
    // gpsd backend
    void connectGpsd();
    void readGpsdData();
    void cleanupGpsd();
    
    void cleanupGps();
    
private:
    bool wanted = false;
    Backend backend = Backend::None;
    
    QString preferredPortName;
    
    QTimer retryTimer;
    
    // serial/NMEA backend
    QSerialPort *serial = nullptr;
    QNmeaPositionInfoSource *nmeaSource = nullptr;
    
    // gpsd backend
    QString gpsdHost = QStringLiteral("127.0.0.1");
    quint16 gpsdPort = 2947;
    QTcpSocket *gpsdSocket = nullptr;
    QByteArray gpsdBuffer;
};

#endif // GPS_PROVIDER_H
