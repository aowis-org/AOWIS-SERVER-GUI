#ifndef MAP_PROVIDER_CATALOG_H
#define MAP_PROVIDER_CATALOG_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QHash>
#include <QString>
#include <QVector>

struct MapProviderEntry
{
    QString id;
    QString name;
    QString url;
    int minimum_zoom = 0;
    int maximum_zoom = 19;
    bool enabled = true;
    bool remote_fetch_enabled = true;
    QString coverage;
};

// Server owns provider definitions; the GUI owns each user's fallback choices.
class MapProviderCatalog : public QObject
{
    Q_OBJECT
public:
    explicit MapProviderCatalog(QObject *parent = nullptr);
    const QVector<MapProviderEntry> &providers() const;
    bool serverLoaded() const;
    const MapProviderEntry *provider(const QString &id) const;
    QString fallback(const QString &provider_id) const;
    bool setFallback(const QString &provider_id, const QString &fallback_id);
    QString sourceForZoom(const QString &selected_id, int zoom) const;
    int maximumSourceZoom(const QString &selected_id) const;
    void refresh();
    void saveProviders(const QVector<MapProviderEntry> &entries);
    static QString builtinId(int provider_enum);
signals:
    void catalogChanged();
    void fallbackChanged();
    void errorOccurred(const QString &message);
    void providersSaved();
private:
    QVector<MapProviderEntry> entries;
    QHash<QString, QString> fallback_cache;
    bool server_loaded = false;
    QNetworkAccessManager network_manager;
};

MapProviderCatalog &mapProviderCatalog();

#endif
