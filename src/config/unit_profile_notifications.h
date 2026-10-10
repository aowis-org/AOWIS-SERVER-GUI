#pragma once

#include <QObject>
#include <QPointer>
#include <QVector>
#include <functional>

// GUI-only notification: profile data and the active selection remain in QSettings.
class UnitProfileNotifications
{
public:
    static UnitProfileNotifications &instance()
    {
        static UnitProfileNotifications notifications;
        return notifications;
    }

    void subscribe(QObject *owner, std::function<void()> refresh)
    {
        this->listeners.append({owner, std::move(refresh)});
    }

    void publish(QObject *source)
    {
        // Use a snapshot because listeners may be changed during callbacks.
        const QVector<Listener> snapshot = this->listeners;
        for (const Listener &listener : snapshot) {
            if (listener.owner && listener.owner != source)
                listener.refresh();
        }
        for (qsizetype i = this->listeners.size(); i > 0; --i) {
            if (!this->listeners.at(i - 1).owner)
                this->listeners.removeAt(i - 1);
        }
    }

private:
    struct Listener {
        QPointer<QObject> owner;
        std::function<void()> refresh;
    };
    QVector<Listener> listeners;
};
