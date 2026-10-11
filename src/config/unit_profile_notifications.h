#pragma once

#include <QObject>
#include <QPointer>
#include <QMap>
#include <functional>
#include <utility>

// GUI-only notification: profile data and the active selection remain in QSettings.
class UnitProfileNotifications : public QObject
{
public:
    static UnitProfileNotifications &instance()
    {
        static UnitProfileNotifications notifications;
        return notifications;
    }

    [[nodiscard]] qsizetype subscriberCount() const { return this->listeners.size(); }

    void subscribe(QObject *owner, std::function<void()> refresh)
    {
        if (!owner || owner == this || !refresh) return;
        const quint64 id = ++this->next_subscription_id;
        this->listeners.insert(id, Listener{owner, std::move(refresh)});
        QObject::connect(owner, &QObject::destroyed, this, [this, id] {
            this->listeners.remove(id);
        });
    }

    void publish(QObject *source)
    {
        // Snapshot iteration stays valid if callbacks destroy or add owners.
        const QMap<quint64, Listener> snapshot = this->listeners;
        for (QMap<quint64, Listener>::const_iterator it = snapshot.cbegin();
             it != snapshot.cend(); ++it) {
            const Listener &listener = it.value();
            if (listener.owner && listener.owner != source)
                listener.refresh();
        }
    }

private:
    struct Listener {
        QPointer<QObject> owner;
        std::function<void()> refresh;
    };
    quint64 next_subscription_id = 0;
    QMap<quint64, Listener> listeners;
};
