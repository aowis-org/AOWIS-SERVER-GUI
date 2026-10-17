#ifndef MAP_WAYLAND_POINTER_LOCK_H
#define MAP_WAYLAND_POINTER_LOCK_H

#include <QPointF>
#include <QVector>

#include <functional>

class QWindow;

// Unbounded orbit motion on Wayland, where clients may not warp the cursor:
// zwp_pointer_constraints_v1 locks the pointer in place and
// zwp_relative_pointer_v1 delivers the raw motion deltas (also at a monitor
// edge, where the absolute pointer would stop).
//
// A lock is requested on every target surface at once. Compositors differ in
// which surface's constraint they honour while the pointer is over an
// embedded native child window (the bgfx map is a wl_subsurface): KWin and
// older mutter only consider the window's main surface, newer mutter (MR
// 2681) the subsurface under the pointer. Each compositor activates one of
// them; the others stay inactive and are destroyed with it.
class MapWaylandPointerLock
{
public:
    using RelativeMotionHandler = std::function<void(const QPointF &)>;

    struct Target
    {
        QWindow *window = nullptr;
        // Surface-local position the pointer returns to when the lock ends
        // (cursor position hint of the lock that was active).
        QPointF restore_surface_position;
    };

    MapWaylandPointerLock();
    ~MapWaylandPointerLock();

    bool isWaylandPlatform() const;
    // Locks the pointer on the targets (same window twice: once) and
    // delivers relative motion. False when nothing could be requested.
    bool lock(const QVector<Target> &targets, RelativeMotionHandler relative_motion_handler);
    void unlock(bool restore_cursor_position = true);
    // Lock requested (relative motion is delivered); the compositor may
    // activate it only later, or never.
    bool isLocked() const;

private:
    class Private;
    Private *d = nullptr;
};

#endif // MAP_WAYLAND_POINTER_LOCK_H
