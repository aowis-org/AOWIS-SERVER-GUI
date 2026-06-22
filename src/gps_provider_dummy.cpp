#include "gps_provider_dummy.h"

GpsProvider::GpsProvider(QObject *parent)
    : QObject(parent)
{
}

void GpsProvider::start()
{
    emit statusMessage("GPS not available in WASM build.");
    emit gpsDisconnected();
}

void GpsProvider::startGpsd(const QString &host, quint16 port)
{
    Q_UNUSED(host)
    Q_UNUSED(port)
    
    emit statusMessage("gpsd not available in WASM build.");
    emit gpsDisconnected();
}

void GpsProvider::startSerial()
{
    emit statusMessage("Serial GPS not available in WASM build.");
    emit gpsDisconnected();
}

void GpsProvider::stop()
{
    emit gpsDisconnected();
}

void GpsProvider::setPreferredPortName(const QString &portName)
{
    Q_UNUSED(portName)
}

void GpsProvider::handleRetryTimer()
{
}

void GpsProvider::tryConnectGps()
{
}

void GpsProvider::cleanupSerialGps()
{
}

bool GpsProvider::looksLikeGpsPort(const QSerialPortInfo &info) const
{
    Q_UNUSED(info)
    return false;
}

void GpsProvider::connectGpsd()
{
}

void GpsProvider::readGpsdData()
{
}

void GpsProvider::cleanupGpsd()
{
}

void GpsProvider::cleanupGps()
{
}
