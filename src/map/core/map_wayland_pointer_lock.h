#ifndef MAP_WAYLAND_POINTER_LOCK_H
#define MAP_WAYLAND_POINTER_LOCK_H

#include <QPointF>

#include <functional>

class QWindow;

class MapWaylandPointerLock
{
public:
    using RelativeMotionHandler = std::function<void(const QPointF &)>;

    MapWaylandPointerLock();
    ~MapWaylandPointerLock();

    bool isWaylandPlatform() const;
    bool lock(QWindow *window,
              const QPointF &restore_surface_position,
              RelativeMotionHandler relative_motion_handler);
    void unlock(bool restore_cursor_position = true);
    bool isLocked() const;

private:
    class Private;
    Private *d = nullptr;
};

#endif // MAP_WAYLAND_POINTER_LOCK_H
