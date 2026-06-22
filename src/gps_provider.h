#ifndef GPS_PROVIDER_H
#define GPS_PROVIDER_H

#include <QObject>
#include <QGeoPositionInfoSource>
#include <QGeoPositionInfo>
#include <QGeoCoordinate>

#include <QDebug>

#pragma once

#include <QObject>
#include <QTimer>
#include <QGeoPositionInfo>
#include <QSerialPortInfo>

class QSerialPort;
class QNmeaPositionInfoSource;

class GpsProvider : public QObject
{
    Q_OBJECT
    
public:
    explicit GpsProvider(QObject *parent = nullptr);
    
    void start();
    void stop();
    
    void setPreferredPortName(const QString &portName);
    
signals:
    void positionChanged(const QGeoPositionInfo &info);
    void gpsConnected(const QString &portName);
    void gpsDisconnected();
    void statusMessage(const QString &message);
    
private:
    void tryConnectGps();
    void cleanupGps();
    bool looksLikeGpsPort(const QSerialPortInfo &info) const;
    
private:
    bool wanted = false;
    
    QString preferredPortName;
    
    QTimer retryTimer;
    
    QSerialPort *serial = nullptr;
    QNmeaPositionInfoSource *nmeaSource = nullptr;
};

#endif // GPS_PROVIDER_H
