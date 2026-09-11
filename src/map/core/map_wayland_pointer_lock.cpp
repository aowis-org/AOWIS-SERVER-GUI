#include "map/core/map_wayland_pointer_lock.h"

#include <QGuiApplication>
#include <QLibrary>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

#ifndef AOWIS_HAS_WAYLAND_POINTER_LOCK
#define AOWIS_HAS_WAYLAND_POINTER_LOCK 0
#endif

#if AOWIS_HAS_WAYLAND_POINTER_LOCK
#include <qpa/qplatformnativeinterface.h>

namespace
{
struct WaylandProxy;

struct WaylandMessage
{
    const char *name;
    const char *signature;
    const struct WaylandInterface **types;
};

struct WaylandInterface
{
    const char *name;
    int version;
    int method_count;
    const WaylandMessage *methods;
    int event_count;
    const WaylandMessage *events;
};

union WaylandArgument
{
    int32_t i;
    uint32_t u;
    int32_t f;
    const char *s;
    WaylandProxy *o;
    uint32_t n;
    void *a;
    int32_t h;
};

using ProxyMarshalFlagsFunction = WaylandProxy *(*)(
    WaylandProxy *, uint32_t, const WaylandInterface *, uint32_t, uint32_t, ...);
using ProxyMarshalArrayFlagsFunction = WaylandProxy *(*)(
    WaylandProxy *, uint32_t, const WaylandInterface *, uint32_t, uint32_t,
    WaylandArgument *);
using ProxyAddListenerFunction = int (*)(WaylandProxy *, void (**)(void), void *);
using ProxyGetVersionFunction = uint32_t (*)(WaylandProxy *);
using ProxyDestroyFunction = void (*)(WaylandProxy *);
using DisplayRoundtripFunction = int (*)(void *);

constexpr uint32_t MarshalFlagDestroy = 1U;
constexpr uint32_t DisplayGetRegistryOpcode = 1U;
constexpr uint32_t RegistryBindOpcode = 0U;
constexpr uint32_t SurfaceCommitOpcode = 6U;
constexpr uint32_t RelativePointerManagerDestroyOpcode = 0U;
constexpr uint32_t RelativePointerManagerGetRelativePointerOpcode = 1U;
constexpr uint32_t RelativePointerDestroyOpcode = 0U;
constexpr uint32_t PointerConstraintsDestroyOpcode = 0U;
constexpr uint32_t PointerConstraintsLockPointerOpcode = 1U;
constexpr uint32_t LockedPointerDestroyOpcode = 0U;
constexpr uint32_t LockedPointerSetCursorPositionHintOpcode = 1U;
constexpr uint32_t PointerConstraintLifetimePersistent = 2U;

const WaylandInterface WaylandPointerInterface = {
    "wl_pointer", 9, 0, nullptr, 0, nullptr};
const WaylandInterface WaylandSurfaceInterface = {
    "wl_surface", 6, 0, nullptr, 0, nullptr};
const WaylandInterface WaylandRegionInterface = {
    "wl_region", 1, 0, nullptr, 0, nullptr};

extern const WaylandInterface RelativePointerInterface;
extern const WaylandInterface LockedPointerInterface;
extern const WaylandInterface ConfinedPointerInterface;

const WaylandInterface *RelativePointerManagerGetTypes[] = {
    &RelativePointerInterface,
    &WaylandPointerInterface
};
const WaylandMessage RelativePointerManagerMethods[] = {
    {"destroy", "", nullptr},
    {"get_relative_pointer", "no", RelativePointerManagerGetTypes}
};
const WaylandInterface RelativePointerManagerInterface = {
    "zwp_relative_pointer_manager_v1",
    1,
    2,
    RelativePointerManagerMethods,
    0,
    nullptr
};

const WaylandMessage RelativePointerMethods[] = {
    {"destroy", "", nullptr}
};
const WaylandMessage RelativePointerEvents[] = {
    {"relative_motion", "uuffff", nullptr}
};
const WaylandInterface RelativePointerInterface = {
    "zwp_relative_pointer_v1",
    1,
    1,
    RelativePointerMethods,
    1,
    RelativePointerEvents
};

const WaylandInterface *PointerConstraintsLockTypes[] = {
    &LockedPointerInterface,
    &WaylandSurfaceInterface,
    &WaylandPointerInterface,
    &WaylandRegionInterface,
    nullptr
};
const WaylandInterface *PointerConstraintsConfineTypes[] = {
    &ConfinedPointerInterface,
    &WaylandSurfaceInterface,
    &WaylandPointerInterface,
    &WaylandRegionInterface,
    nullptr
};
const WaylandMessage PointerConstraintsMethods[] = {
    {"destroy", "", nullptr},
    {"lock_pointer", "noo?ou", PointerConstraintsLockTypes},
    {"confine_pointer", "noo?ou", PointerConstraintsConfineTypes}
};
const WaylandInterface PointerConstraintsInterface = {
    "zwp_pointer_constraints_v1",
    1,
    3,
    PointerConstraintsMethods,
    0,
    nullptr
};

const WaylandInterface *LockedPointerSetRegionTypes[] = {
    &WaylandRegionInterface
};
const WaylandMessage LockedPointerMethods[] = {
    {"destroy", "", nullptr},
    {"set_cursor_position_hint", "ff", nullptr},
    {"set_region", "?o", LockedPointerSetRegionTypes}
};
const WaylandMessage LockedPointerEvents[] = {
    {"locked", "", nullptr},
    {"unlocked", "", nullptr}
};
const WaylandInterface LockedPointerInterface = {
    "zwp_locked_pointer_v1",
    1,
    3,
    LockedPointerMethods,
    2,
    LockedPointerEvents
};

const WaylandMessage ConfinedPointerMethods[] = {
    {"destroy", "", nullptr},
    {"set_region", "?o", LockedPointerSetRegionTypes}
};
const WaylandMessage ConfinedPointerEvents[] = {
    {"confined", "", nullptr},
    {"unconfined", "", nullptr}
};
const WaylandInterface ConfinedPointerInterface = {
    "zwp_confined_pointer_v1",
    1,
    2,
    ConfinedPointerMethods,
    2,
    ConfinedPointerEvents
};

const WaylandMessage RegistryMethods[] = {
    {"bind", "usun", nullptr}
};
const WaylandMessage RegistryEvents[] = {
    {"global", "usu", nullptr},
    {"global_remove", "u", nullptr}
};
const WaylandInterface RegistryInterface = {
    "wl_registry",
    1,
    1,
    RegistryMethods,
    2,
    RegistryEvents
};

struct RegistryListener
{
    void (*global)(void *, WaylandProxy *, uint32_t, const char *, uint32_t);
    void (*global_remove)(void *, WaylandProxy *, uint32_t);
};

struct RelativePointerListener
{
    void (*relative_motion)(void *, WaylandProxy *, uint32_t, uint32_t,
                            int32_t, int32_t, int32_t, int32_t);
};

double fixedToDouble(int32_t value)
{
    return double(value) / 256.0;
}

int32_t doubleToFixed(double value)
{
    return int32_t(std::lround(value * 256.0));
}
}
#endif

class MapWaylandPointerLock::Private
{
public:
#if AOWIS_HAS_WAYLAND_POINTER_LOCK
    QLibrary wayland_client_library;
    ProxyMarshalFlagsFunction proxy_marshal_flags = nullptr;
    ProxyMarshalArrayFlagsFunction proxy_marshal_array_flags = nullptr;
    ProxyAddListenerFunction proxy_add_listener = nullptr;
    ProxyGetVersionFunction proxy_get_version = nullptr;
    ProxyDestroyFunction proxy_destroy = nullptr;
    DisplayRoundtripFunction display_roundtrip = nullptr;

    WaylandProxy *display = nullptr;
    WaylandProxy *pointer_constraints = nullptr;
    WaylandProxy *relative_pointer_manager = nullptr;
    WaylandProxy *relative_pointer = nullptr;
    WaylandProxy *locked_pointer = nullptr;
    WaylandProxy *surface = nullptr;
    MapWaylandPointerLock::RelativeMotionHandler relative_motion_handler;
#endif
    QPointF restore_surface_position;

#if AOWIS_HAS_WAYLAND_POINTER_LOCK
    bool loadWaylandClientRuntime()
    {
        if (this->wayland_client_library.isLoaded())
        {
            return this->proxy_marshal_flags != nullptr
                && this->proxy_marshal_array_flags != nullptr
                && this->proxy_add_listener != nullptr
                && this->proxy_get_version != nullptr
                && this->proxy_destroy != nullptr
                && this->display_roundtrip != nullptr;
        }

        this->wayland_client_library.setFileName(QStringLiteral("wayland-client"));
        if (!this->wayland_client_library.load())
        {
            this->wayland_client_library.setFileName(QStringLiteral("libwayland-client.so.0"));
            if (!this->wayland_client_library.load())
                return false;
        }

        this->proxy_marshal_flags = reinterpret_cast<ProxyMarshalFlagsFunction>(
            this->wayland_client_library.resolve("wl_proxy_marshal_flags"));
        this->proxy_marshal_array_flags = reinterpret_cast<ProxyMarshalArrayFlagsFunction>(
            this->wayland_client_library.resolve("wl_proxy_marshal_array_flags"));
        this->proxy_add_listener = reinterpret_cast<ProxyAddListenerFunction>(
            this->wayland_client_library.resolve("wl_proxy_add_listener"));
        this->proxy_get_version = reinterpret_cast<ProxyGetVersionFunction>(
            this->wayland_client_library.resolve("wl_proxy_get_version"));
        this->proxy_destroy = reinterpret_cast<ProxyDestroyFunction>(
            this->wayland_client_library.resolve("wl_proxy_destroy"));
        this->display_roundtrip = reinterpret_cast<DisplayRoundtripFunction>(
            this->wayland_client_library.resolve("wl_display_roundtrip"));

        return this->proxy_marshal_flags != nullptr
            && this->proxy_marshal_array_flags != nullptr
            && this->proxy_add_listener != nullptr
            && this->proxy_get_version != nullptr
            && this->proxy_destroy != nullptr
            && this->display_roundtrip != nullptr;
    }

    void destroyProtocolObject(WaylandProxy *&proxy, uint32_t destroy_opcode)
    {
        if (proxy == nullptr || this->proxy_marshal_flags == nullptr)
            return;

        this->proxy_marshal_flags(
            proxy,
            destroy_opcode,
            nullptr,
            this->proxy_get_version(proxy),
            MarshalFlagDestroy);
        proxy = nullptr;
    }

    WaylandProxy *bindGlobal(WaylandProxy *registry,
                             uint32_t name,
                             uint32_t advertised_version,
                             const WaylandInterface *interface)
    {
        const uint32_t version = std::min<uint32_t>(
            advertised_version, uint32_t(interface->version));
        return this->proxy_marshal_flags(
            registry,
            RegistryBindOpcode,
            interface,
            version,
            0U,
            name,
            interface->name,
            version,
            nullptr);
    }

    static void registryGlobal(void *data,
                               WaylandProxy *registry,
                               uint32_t name,
                               const char *interface_name,
                               uint32_t version)
    {
        Private *self = static_cast<Private *>(data);
        if (self == nullptr || interface_name == nullptr)
            return;

        if (std::strcmp(interface_name, PointerConstraintsInterface.name) == 0
            && self->pointer_constraints == nullptr)
        {
            self->pointer_constraints = self->bindGlobal(
                registry, name, version, &PointerConstraintsInterface);
        }
        else if (std::strcmp(interface_name, RelativePointerManagerInterface.name) == 0
                 && self->relative_pointer_manager == nullptr)
        {
            self->relative_pointer_manager = self->bindGlobal(
                registry, name, version, &RelativePointerManagerInterface);
        }
    }

    static void registryGlobalRemove(void *, WaylandProxy *, uint32_t)
    {
    }

    bool ensureProtocolManagers()
    {
        if (this->pointer_constraints != nullptr
            && this->relative_pointer_manager != nullptr)
        {
            return true;
        }

        if (this->display == nullptr || !loadWaylandClientRuntime())
            return false;

        WaylandProxy *registry = this->proxy_marshal_flags(
            this->display,
            DisplayGetRegistryOpcode,
            &RegistryInterface,
            this->proxy_get_version(this->display),
            0U,
            nullptr);
        if (registry == nullptr)
            return false;

        static RegistryListener registry_listener = {
            &Private::registryGlobal,
            &Private::registryGlobalRemove
        };
        if (this->proxy_add_listener(
                registry,
                reinterpret_cast<void (**)(void)>(&registry_listener),
                this) != 0)
        {
            this->proxy_destroy(registry);
            return false;
        }

        const int roundtrip_result = this->display_roundtrip(this->display);
        this->proxy_destroy(registry);
        if (roundtrip_result < 0)
            return false;

        return this->pointer_constraints != nullptr
            && this->relative_pointer_manager != nullptr;
    }

    static void relativeMotion(void *data,
                               WaylandProxy *,
                               uint32_t,
                               uint32_t,
                               int32_t dx,
                               int32_t dy,
                               int32_t,
                               int32_t)
    {
        Private *self = static_cast<Private *>(data);
        if (self != nullptr && self->relative_motion_handler)
        {
            self->relative_motion_handler(QPointF(
                fixedToDouble(dx),
                fixedToDouble(dy)));
        }
    }

    void shutdown()
    {
        this->destroyProtocolObject(
            this->locked_pointer, LockedPointerDestroyOpcode);
        this->destroyProtocolObject(
            this->relative_pointer, RelativePointerDestroyOpcode);
        this->destroyProtocolObject(
            this->pointer_constraints, PointerConstraintsDestroyOpcode);
        this->destroyProtocolObject(
            this->relative_pointer_manager, RelativePointerManagerDestroyOpcode);
        this->surface = nullptr;
        this->display = nullptr;
        this->relative_motion_handler = MapWaylandPointerLock::RelativeMotionHandler();
        if (this->wayland_client_library.isLoaded())
            this->wayland_client_library.unload();
    }
#endif
};

MapWaylandPointerLock::MapWaylandPointerLock()
    : d(new Private)
{
}

MapWaylandPointerLock::~MapWaylandPointerLock()
{
    unlock(false);
#if AOWIS_HAS_WAYLAND_POINTER_LOCK
    this->d->shutdown();
#endif
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
    QNativeInterface::QWaylandApplication *wayland_application =
        qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    QPlatformNativeInterface *platform_native_interface =
        QGuiApplication::platformNativeInterface();
    if (wayland_application == nullptr || platform_native_interface == nullptr)
        return false;

    window->create();

    this->d->display = reinterpret_cast<WaylandProxy *>(
        wayland_application->display());
    WaylandProxy *pointer = reinterpret_cast<WaylandProxy *>(
        wayland_application->pointer());
    this->d->surface = reinterpret_cast<WaylandProxy *>(
        platform_native_interface->nativeResourceForWindow(
            QByteArrayLiteral("surface"), window));
    if (this->d->display == nullptr || pointer == nullptr || this->d->surface == nullptr)
        return false;

    if (!this->d->ensureProtocolManagers())
        return false;

    this->d->restore_surface_position = restore_surface_position;
    this->d->relative_motion_handler = std::move(relative_motion_handler);

    // Use wl_proxy_marshal_array_flags() for constructor requests. This avoids
    // varargs ABI ambiguity entirely: every protocol argument occupies the
    // exact slot described by the Wayland signature, and libwayland replaces
    // the new_id slot with the newly allocated proxy before marshalling.
    WaylandArgument relative_pointer_arguments[2] = {};
    relative_pointer_arguments[0].o = nullptr;
    relative_pointer_arguments[1].o = pointer;
    this->d->relative_pointer = this->d->proxy_marshal_array_flags(
        this->d->relative_pointer_manager,
        RelativePointerManagerGetRelativePointerOpcode,
        &RelativePointerInterface,
        this->d->proxy_get_version(this->d->relative_pointer_manager),
        0U,
        relative_pointer_arguments);
    if (this->d->relative_pointer == nullptr)
    {
        this->d->relative_motion_handler = RelativeMotionHandler();
        return false;
    }

    static RelativePointerListener relative_pointer_listener = {
        &Private::relativeMotion
    };
    if (this->d->proxy_add_listener(
            this->d->relative_pointer,
            reinterpret_cast<void (**)(void)>(&relative_pointer_listener),
            this->d) != 0)
    {
        this->d->destroyProtocolObject(
            this->d->relative_pointer, RelativePointerDestroyOpcode);
        this->d->relative_motion_handler = RelativeMotionHandler();
        return false;
    }

    WaylandArgument lock_pointer_arguments[5] = {};
    lock_pointer_arguments[0].o = nullptr;
    lock_pointer_arguments[1].o = this->d->surface;
    lock_pointer_arguments[2].o = pointer;
    lock_pointer_arguments[3].o = nullptr;
    lock_pointer_arguments[4].u = PointerConstraintLifetimePersistent;
    this->d->locked_pointer = this->d->proxy_marshal_array_flags(
        this->d->pointer_constraints,
        PointerConstraintsLockPointerOpcode,
        &LockedPointerInterface,
        this->d->proxy_get_version(this->d->pointer_constraints),
        0U,
        lock_pointer_arguments);
    if (this->d->locked_pointer == nullptr)
    {
        this->d->destroyProtocolObject(
            this->d->relative_pointer, RelativePointerDestroyOpcode);
        this->d->relative_motion_handler = RelativeMotionHandler();
        return false;
    }

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
    if (this->d->locked_pointer != nullptr)
    {
        if (restore_cursor_position && this->d->surface != nullptr)
        {
            this->d->proxy_marshal_flags(
                this->d->locked_pointer,
                LockedPointerSetCursorPositionHintOpcode,
                nullptr,
                this->d->proxy_get_version(this->d->locked_pointer),
                0U,
                doubleToFixed(this->d->restore_surface_position.x()),
                doubleToFixed(this->d->restore_surface_position.y()));

            this->d->proxy_marshal_flags(
                this->d->surface,
                SurfaceCommitOpcode,
                nullptr,
                this->d->proxy_get_version(this->d->surface),
                0U);
        }

        this->d->destroyProtocolObject(
            this->d->locked_pointer, LockedPointerDestroyOpcode);
    }

    this->d->destroyProtocolObject(
        this->d->relative_pointer, RelativePointerDestroyOpcode);
    this->d->relative_motion_handler = RelativeMotionHandler();
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
