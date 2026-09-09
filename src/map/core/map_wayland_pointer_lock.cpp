#include "map/core/map_wayland_pointer_lock.h"

#include <QGuiApplication>
#include <QWindow>

#include <memory>
#include <utility>

#ifndef AOWIS_HAS_WAYLAND_POINTER_LOCK
#define AOWIS_HAS_WAYLAND_POINTER_LOCK 0
#endif

#if AOWIS_HAS_WAYLAND_POINTER_LOCK
#include <QWaylandClientExtensionTemplate>
#include <qpa/qplatformwindow_p.h>

#include "qwayland-pointer-constraints-unstable-v1.h"
#include "qwayland-relative-pointer-unstable-v1.h"

#include <wayland-client-protocol.h>

namespace
{
class PointerConstraints final
    : public QWaylandClientExtensionTemplate<PointerConstraints>
    , public QtWayland::zwp_pointer_constraints_v1
{
public:
    PointerConstraints()
        : QWaylandClientExtensionTemplate<PointerConstraints>(1)
    {
    }

    ~PointerConstraints()
    {
        if (isActive())
            destroy();
    }
};

class RelativePointerManager final
    : public QWaylandClientExtensionTemplate<RelativePointerManager>
    , public QtWayland::zwp_relative_pointer_manager_v1
{
public:
    RelativePointerManager()
        : QWaylandClientExtensionTemplate<RelativePointerManager>(1)
    {
    }

    ~RelativePointerManager()
    {
        if (isActive())
            destroy();
    }
};

class RelativePointer final : public QtWayland::zwp_relative_pointer_v1
{
public:
    RelativePointer(::zwp_relative_pointer_v1 *pointer,
                    MapWaylandPointerLock::RelativeMotionHandler handler)
        : QtWayland::zwp_relative_pointer_v1(pointer),
          handler(std::move(handler))
    {
    }

    ~RelativePointer()
    {
        destroy();
    }

private:
    MapWaylandPointerLock::RelativeMotionHandler handler;

    void zwp_relative_pointer_v1_relative_motion(
        uint32_t, uint32_t,
        wl_fixed_t dx, wl_fixed_t dy,
        wl_fixed_t, wl_fixed_t) override
    {
        if (this->handler)
        {
            this->handler(QPointF(
                wl_fixed_to_double(dx),
                wl_fixed_to_double(dy)));
        }
    }
};

class LockedPointer final : public QtWayland::zwp_locked_pointer_v1
{
public:
    explicit LockedPointer(::zwp_locked_pointer_v1 *pointer)
        : QtWayland::zwp_locked_pointer_v1(pointer)
    {
    }
};
}
#endif

class MapWaylandPointerLock::Private
{
public:
#if AOWIS_HAS_WAYLAND_POINTER_LOCK
    PointerConstraints pointer_constraints;
    RelativePointerManager relative_pointer_manager;
    std::unique_ptr<RelativePointer> relative_pointer;
    std::unique_ptr<LockedPointer> locked_pointer;
    wl_surface *surface = nullptr;
#endif
    QPointF restore_surface_position;
};

MapWaylandPointerLock::MapWaylandPointerLock()
    : d(new Private)
{
}

MapWaylandPointerLock::~MapWaylandPointerLock()
{
    unlock(false);
    delete this->d;
    this->d = nullptr;
}

bool MapWaylandPointerLock::isWaylandPlatform() const
{
    return QGuiApplication::platformName().startsWith(
        QStringLiteral("wayland"), Qt::CaseInsensitive);
}

bool MapWaylandPointerLock::lock(
    QWindow *window,
    const QPointF &restore_surface_position,
    RelativeMotionHandler relative_motion_handler)
{
    unlock(false);

    if (!isWaylandPlatform() || window == nullptr)
        return false;

#if AOWIS_HAS_WAYLAND_POINTER_LOCK
    if (!this->d->pointer_constraints.isActive()
        || !this->d->relative_pointer_manager.isActive())
    {
        return false;
    }

    window->create();

    QNativeInterface::QWaylandApplication *wayland_application =
        qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    QNativeInterface::Private::QWaylandWindow *wayland_window =
        window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    if (wayland_application == nullptr || wayland_window == nullptr)
        return false;

    wl_pointer *pointer = wayland_application->pointer();
    wl_surface *surface = wayland_window->surface();
    if (pointer == nullptr || surface == nullptr)
        return false;

    ::zwp_relative_pointer_v1 *relative_pointer =
        this->d->relative_pointer_manager.get_relative_pointer(pointer);
    if (relative_pointer == nullptr)
        return false;

    this->d->relative_pointer = std::make_unique<RelativePointer>(
        relative_pointer, std::move(relative_motion_handler));

    ::zwp_locked_pointer_v1 *locked_pointer = this->d->pointer_constraints.lock_pointer(
        surface,
        pointer,
        nullptr,
        PointerConstraints::lifetime_persistent);
    if (locked_pointer == nullptr)
    {
        this->d->relative_pointer.reset();
        return false;
    }

    this->d->locked_pointer = std::make_unique<LockedPointer>(locked_pointer);
    this->d->surface = surface;
    this->d->restore_surface_position = restore_surface_position;
    return true;
#else
    Q_UNUSED(restore_surface_position)
    Q_UNUSED(relative_motion_handler)
    return false;
#endif
}

void MapWaylandPointerLock::unlock(bool restore_cursor_position)
{
#if AOWIS_HAS_WAYLAND_POINTER_LOCK
    if (this->d->locked_pointer)
    {
        if (restore_cursor_position && this->d->surface != nullptr)
        {
            this->d->locked_pointer->set_cursor_position_hint(
                wl_fixed_from_double(this->d->restore_surface_position.x()),
                wl_fixed_from_double(this->d->restore_surface_position.y()));
            wl_surface_commit(this->d->surface);
        }

        this->d->locked_pointer->destroy();
        this->d->locked_pointer.reset();
    }

    this->d->relative_pointer.reset();
    this->d->surface = nullptr;
#else
    Q_UNUSED(restore_cursor_position)
#endif
}

bool MapWaylandPointerLock::isLocked() const
{
#if AOWIS_HAS_WAYLAND_POINTER_LOCK
    return this->d->locked_pointer != nullptr;
#else
    return false;
#endif
}
