#include "gps_provider.h"

#include <QSerialPort>
#include <QNmeaPositionInfoSource>
#include <QGeoPositionInfoSource>
#include <QDebug>

GpsProvider::GpsProvider(QObject *parent)
    : QObject(parent)
{
    retryTimer.setInterval(2000); // retry every 2 seconds
    retryTimer.setSingleShot(false);
    
    connect(&retryTimer, &QTimer::timeout,
            this, &GpsProvider::tryConnectGps);
}

void GpsProvider::start()
{
    wanted = true;
    
    if (!retryTimer.isActive())
        retryTimer.start();
    
    tryConnectGps();
}

void GpsProvider::stop()
{
    wanted = false;
    retryTimer.stop();
    cleanupGps();
    
    emit statusMessage("GPS stopped");
}

void GpsProvider::setPreferredPortName(const QString &portName)
{
    preferredPortName = portName;
}

void GpsProvider::tryConnectGps()
{
    if (!wanted)
        return;
    
    if (serial && serial->isOpen())
        return;
    
    const auto ports = QSerialPortInfo::availablePorts();
    
    for (const QSerialPortInfo &info : ports) {
        if (!looksLikeGpsPort(info))
            continue;
        
        auto *newSerial = new QSerialPort(this);
        newSerial->setPort(info);
        newSerial->setBaudRate(QSerialPort::Baud9600);
        newSerial->setDataBits(QSerialPort::Data8);
        newSerial->setParity(QSerialPort::NoParity);
        newSerial->setStopBits(QSerialPort::OneStop);
        newSerial->setFlowControl(QSerialPort::NoFlowControl);
        
        if (!newSerial->open(QIODevice::ReadOnly)) {
            emit statusMessage("Could not open GPS port: " + info.portName()
                               + " / " + newSerial->errorString());
            newSerial->deleteLater();
            continue;
        }
        
        auto *newNmea = new QNmeaPositionInfoSource(
            QNmeaPositionInfoSource::RealTimeMode, this);
        
        newNmea->setDevice(newSerial);
        
        connect(newNmea, &QGeoPositionInfoSource::positionUpdated,
                this, &GpsProvider::positionChanged);
        
        connect(newSerial, &QSerialPort::errorOccurred,
                this, [this](QSerialPort::SerialPortError error) {
                    if (error == QSerialPort::NoError)
                        return;
                    
                    emit statusMessage("GPS serial error: " + serial->errorString());
                    
                    cleanupGps();
                    
                    if (wanted)
                        retryTimer.start();
                });
        
        serial = newSerial;
        nmeaSource = newNmea;
        
        nmeaSource->startUpdates();
        
        emit gpsConnected(info.portName());
        emit statusMessage("GPS connected: " + info.portName());
        
        return;
    }
    
    emit statusMessage("No GPS receiver found. Waiting...");
}

void GpsProvider::cleanupGps()
{
    if (nmeaSource) {
        nmeaSource->stopUpdates();
        nmeaSource->deleteLater();
        nmeaSource = nullptr;
    }
    
    if (serial) {
        if (serial->isOpen())
            serial->close();
        
        serial->deleteLater();
        serial = nullptr;
    }
    
    emit gpsDisconnected();
}

bool GpsProvider::looksLikeGpsPort(const QSerialPortInfo &info) const
{
    if (!preferredPortName.isEmpty())
        return info.portName() == preferredPortName;
    
    const QString text =
        info.portName() + " " +
        info.description() + " " +
        info.manufacturer();
    
    const QString lower = text.toLower();
    
    return lower.contains("gps")
           || lower.contains("gnss")
           || lower.contains("u-blox")
           || lower.contains("ublox")
           || lower.contains("nmea");
}
