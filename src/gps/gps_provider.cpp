#include "gps/gps_provider.h"

#include <QSerialPort>
#include <QSerialPortInfo>

#include <QNmeaPositionInfoSource>
#include <QGeoPositionInfoSource>
#include <QGeoCoordinate>

#include <QTcpSocket>
#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDateTime>

#include <QDebug>

GpsProvider::GpsProvider(QObject *parent)
    : QObject(parent)
{
    retryTimer.setInterval(2000);
    retryTimer.setSingleShot(false);
    
    connect(&retryTimer, &QTimer::timeout,
            this, &GpsProvider::handleRetryTimer);
}

void GpsProvider::start()
{
    // Since xgps works on your system, gpsd is the better default.
    startGpsd();
}

void GpsProvider::startGpsd(const QString &host, quint16 port)
{
    wanted = true;
    backend = Backend::Gpsd;
    
    gpsdHost = host;
    gpsdPort = port;
    
    cleanupGps();
    
    if (!retryTimer.isActive())
        retryTimer.start();
    
    connectGpsd();
}

void GpsProvider::startSerial()
{
    wanted = true;
    backend = Backend::Serial;
    
    cleanupGps();
    
    if (!retryTimer.isActive())
        retryTimer.start();
    
    tryConnectGps();
}

void GpsProvider::stop()
{
    wanted = false;
    backend = Backend::None;
    
    retryTimer.stop();
    cleanupGps();
    
    emit statusMessage(QStringLiteral("GPS stopped"));
}

void GpsProvider::setPreferredPortName(const QString &portName)
{
    preferredPortName = portName;
}

void GpsProvider::handleRetryTimer()
{
    if (!wanted)
        return;
    
    switch (backend) {
    case Backend::Gpsd:
        connectGpsd();
        break;
        
    case Backend::Serial:
        tryConnectGps();
        break;
        
    case Backend::None:
        break;
    }
}

void GpsProvider::connectGpsd()
{
    if (!wanted || backend != Backend::Gpsd)
        return;
    
    if (gpsdSocket) {
        const auto state = gpsdSocket->state();
        
        if (state == QAbstractSocket::ConnectedState ||
            state == QAbstractSocket::ConnectingState) {
            return;
        }
        
        cleanupGpsd();
    }
    
    gpsdSocket = new QTcpSocket(this);
    
    connect(gpsdSocket, &QTcpSocket::connected,
            this, [this]() {
                emit gpsConnected(QStringLiteral("gpsd://%1:%2")
                                      .arg(gpsdHost)
                                      .arg(gpsdPort));
                
                emit statusMessage(QStringLiteral("Connected to gpsd at %1:%2")
                                       .arg(gpsdHost)
                                       .arg(gpsdPort));
                
                gpsdSocket->write("?WATCH={\"enable\":true,\"json\":true};\n");
                gpsdSocket->flush();
            });
    
    connect(gpsdSocket, &QTcpSocket::readyRead,
            this, &GpsProvider::readGpsdData);
    
    connect(gpsdSocket, &QTcpSocket::errorOccurred,
            this, [this](QAbstractSocket::SocketError) {
                if (!gpsdSocket)
                    return;
                
                emit statusMessage(QStringLiteral("gpsd error: ")
                                   + gpsdSocket->errorString());
                
                cleanupGpsd();
                
                if (wanted && backend == Backend::Gpsd && !retryTimer.isActive())
                    retryTimer.start();
            });
    
    connect(gpsdSocket, &QTcpSocket::disconnected,
            this, [this]() {
                emit statusMessage(QStringLiteral("gpsd disconnected"));
                
                cleanupGpsd();
                
                if (wanted && backend == Backend::Gpsd && !retryTimer.isActive())
                    retryTimer.start();
            });
    
    emit statusMessage(QStringLiteral("Connecting to gpsd at %1:%2...")
                           .arg(gpsdHost)
                           .arg(gpsdPort));
    
    gpsdSocket->connectToHost(gpsdHost, gpsdPort);
}

void GpsProvider::readGpsdData()
{
    if (!gpsdSocket)
        return;
    
    gpsdBuffer += gpsdSocket->readAll();
    
    while (true) {
        const int newlineIndex = gpsdBuffer.indexOf('\n');
        
        if (newlineIndex < 0)
            break;
        
        const QByteArray line = gpsdBuffer.left(newlineIndex).trimmed();
        gpsdBuffer.remove(0, newlineIndex + 1);
        
        if (line.isEmpty())
            continue;
        
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        
        const QJsonObject obj = doc.object();
        
        if (obj.value(QStringLiteral("class")).toString() != QStringLiteral("TPV"))
            continue;
        
        const int mode = obj.value(QStringLiteral("mode")).toInt();
        
        // gpsd mode:
        // 1 = no fix
        // 2 = 2D fix
        // 3 = 3D fix
        if (mode < 2)
            continue;
        
        const QJsonValue latValue = obj.value(QStringLiteral("lat"));
        const QJsonValue lonValue = obj.value(QStringLiteral("lon"));
        
        if (!latValue.isDouble() || !lonValue.isDouble())
            continue;
        
        const double lat = latValue.toDouble();
        const double lon = lonValue.toDouble();
        
        QGeoCoordinate coordinate;
        
        const QJsonValue altValue = obj.value(QStringLiteral("alt"));
        
        if (altValue.isDouble()) {
            coordinate = QGeoCoordinate(lat, lon, altValue.toDouble());
        } else {
            coordinate = QGeoCoordinate(lat, lon);
        }
        
        QDateTime timestamp;
        
        const QString timeString = obj.value(QStringLiteral("time")).toString();
        
        if (!timeString.isEmpty()) {
            timestamp = QDateTime::fromString(timeString, Qt::ISODateWithMs);
            
            if (!timestamp.isValid())
                timestamp = QDateTime::fromString(timeString, Qt::ISODate);
        }
        
        if (!timestamp.isValid())
            timestamp = QDateTime::currentDateTimeUtc();
        
        QGeoPositionInfo info;
        info.setCoordinate(coordinate);
        info.setTimestamp(timestamp);
        
        const QJsonValue epxValue = obj.value(QStringLiteral("epx"));
        const QJsonValue epyValue = obj.value(QStringLiteral("epy"));
        const QJsonValue epvValue = obj.value(QStringLiteral("epv"));
        
        if (epxValue.isDouble() && epyValue.isDouble()) {
            const double horizontalAccuracy =
                qMax(epxValue.toDouble(), epyValue.toDouble());
            
            info.setAttribute(QGeoPositionInfo::HorizontalAccuracy,
                              horizontalAccuracy);
        }
        
        if (epvValue.isDouble()) {
            info.setAttribute(QGeoPositionInfo::VerticalAccuracy,
                              epvValue.toDouble());
        }
        
        emit positionChanged(info);
    }
}

void GpsProvider::cleanupGpsd()
{
    if (!gpsdSocket)
        return;
    
    QTcpSocket *oldSocket = gpsdSocket;
    gpsdSocket = nullptr;
    
    oldSocket->disconnect(this);
    oldSocket->abort();
    oldSocket->deleteLater();
    
    gpsdBuffer.clear();
}

void GpsProvider::tryConnectGps()
{
    if (!wanted || backend != Backend::Serial)
        return;
    
    if (serial && serial->isOpen())
        return;
    
    bool foundCandidate = false;
    
    const auto ports = QSerialPortInfo::availablePorts();
    
    for (const QSerialPortInfo &info : ports) {
        if (!looksLikeGpsPort(info))
            continue;
        
        foundCandidate = true;
        
        auto *newSerial = new QSerialPort(this);
        newSerial->setPort(info);
        newSerial->setBaudRate(QSerialPort::Baud9600);
        newSerial->setDataBits(QSerialPort::Data8);
        newSerial->setParity(QSerialPort::NoParity);
        newSerial->setStopBits(QSerialPort::OneStop);
        newSerial->setFlowControl(QSerialPort::NoFlowControl);
        
        if (!newSerial->open(QIODevice::ReadOnly)) {
            emit statusMessage(QStringLiteral("GPS receiver found on ")
                               + info.portName()
                               + QStringLiteral(", but could not open it: ")
                               + newSerial->errorString());
            
            newSerial->deleteLater();
            continue;
        }
        
        auto *newNmea = new QNmeaPositionInfoSource(
            QNmeaPositionInfoSource::RealTimeMode, this);
        
        newNmea->setDevice(newSerial);
        
        connect(newNmea, &QGeoPositionInfoSource::positionUpdated,
                this, &GpsProvider::positionChanged);
        
        connect(newSerial, &QSerialPort::errorOccurred,
                this, [this, newSerial](QSerialPort::SerialPortError error) {
                    if (error == QSerialPort::NoError)
                        return;
                    
                    if (newSerial != serial)
                        return;
                    
                    emit statusMessage(QStringLiteral("GPS serial error: ")
                                       + newSerial->errorString());
                    
                    cleanupGps();
                    
                    if (wanted && backend == Backend::Serial && !retryTimer.isActive())
                        retryTimer.start();
                });
        
        serial = newSerial;
        nmeaSource = newNmea;
        
        nmeaSource->startUpdates();
        
        emit gpsConnected(info.portName());
        emit statusMessage(QStringLiteral("GPS connected: ") + info.portName());
        
        return;
    }
    
    if (!foundCandidate) {
        emit statusMessage(QStringLiteral("No GPS receiver found. Waiting..."));
    } else {
        emit statusMessage(QStringLiteral("GPS receiver found, but no usable port could be opened."));
    }
}

void GpsProvider::cleanupSerialGps()
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
}

bool GpsProvider::looksLikeGpsPort(const QSerialPortInfo &info) const
{
    if (!preferredPortName.isEmpty())
        return info.portName() == preferredPortName;
    
    const QString text =
        info.portName() + QStringLiteral(" ") +
        info.description() + QStringLiteral(" ") +
        info.manufacturer();
    
    const QString lower = text.toLower();
    
    return lower.contains(QStringLiteral("gps"))
           || lower.contains(QStringLiteral("gnss"))
           || lower.contains(QStringLiteral("u-blox"))
           || lower.contains(QStringLiteral("ublox"))
           || lower.contains(QStringLiteral("nmea"))
           || lower.contains(QStringLiteral("acm"));
}

void GpsProvider::cleanupGps()
{
    const bool hadGps =
        gpsdSocket != nullptr ||
        serial != nullptr ||
        nmeaSource != nullptr;
    
    cleanupGpsd();
    cleanupSerialGps();
    
    if (hadGps)
        emit gpsDisconnected();
}
