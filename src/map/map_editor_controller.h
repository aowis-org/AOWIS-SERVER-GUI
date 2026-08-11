#ifndef MAP_EDITOR_CONTROLLER_H
#define MAP_EDITOR_CONTROLLER_H

#include <QObject>
#include <QPointF>
#include <QRect>
#include <QSize>

#include <optional>

#include "../_enums_structs.h"
#include "map_models.h"

class MapCanvasEntities;
class MapModel;
class QTimer;

class MapEditorController : public QObject
{
    Q_OBJECT

public:
    struct TileSelectionRange
    {
        bool valid = false;
        int zoom = 0;
        int tile_x_min = 0;
        int tile_x_max = -1;
        int tile_y_min = 0;
        int tile_y_max = -1;
    };

    struct TileSelectionOverlay
    {
        bool visible = false;
        int zoom = 0;
        int tile_x_min = 0;
        int tile_x_max = -1;
        int tile_y_min = 0;
        int tile_y_max = -1;
    };

    explicit MapEditorController(MapModel *map_model, MapCanvasEntities *map_canvas_entities,
                                 QObject *parent = nullptr);

    void startEntityPositioning(InfrastructureEntity tool);
    void stopEntityPositioning();
    void deleteSelectedEntities();

    void startRectangleSelection(bool oneshot, bool interact_with_entities = true);
    void cancelRectangleSelection();
    void clearTileSelectionOverlay();

    bool rectangleSelectionActive() const;
    bool rectangleDragging() const;
    QRect currentSelectionRect(const QSize &viewport_size) const;
    const TileSelectionOverlay &tileSelectionOverlay() const;
    TileSelectionRange tileSelectionRange(int zoom) const;
    std::optional<Qt::CursorShape> cursorShape() const;

    bool keyPress(Qt::Key key);
    bool mousePress(const QPointF &position, const QPoint &global_position,
                    Qt::MouseButton button, const QSize &viewport_size);
    bool mouseMove(const QPointF &position, const QSize &viewport_size,
                   bool allow_entity_interaction);
    bool mouseRelease(const QPointF &position, Qt::MouseButton button,
                      const QSize &viewport_size);

signals:
    void signalStateChanged();
    void signalFocusRequested();
    void signalRectangleSelected(const CoordinateWGS84Rect &rect);
    void signalRectangleSelectionCanceled();
    void signalEntitySelectionChanged(bool selected);

private:
    void beginRectangleDrag(const QPointF &position, const QSize &viewport_size);
#ifdef Q_OS_WASM
    void scheduleRectangleSelectionUpdate(const QRect &selected_rect);
    void applyPendingRectangleSelection();
#endif
    void setCursorShape(std::optional<Qt::CursorShape> cursor_shape);
    bool clearTileSelectionOverlayState();
    CoordinateWGS84Rect selectionRectWgs84(const QRect &selected_rect,
                                           const QSize &viewport_size) const;
    CoordinateWGS84Rect tileSelectionRectWgs84() const;
    void updateTileSelectionOverlay(const QRect &selected_rect,
                                    const QSize &viewport_size);

    MapModel *map_model = nullptr;
    MapCanvasEntities *map_canvas_entities = nullptr;

    bool is_rectangle_selection_oneshot = true;
    bool rectangle_selection_active = false;
    bool rectangle_dragging = false;
    bool rectangle_selection_interacts_with_entities = true;
    CoordinateWGS84 rectangle_start_wgs84;
    CoordinateWGS84 rectangle_current_wgs84;
    TileSelectionOverlay tile_selection_overlay;
    std::optional<Qt::CursorShape> cursor_shape;
#ifdef Q_OS_WASM
    QTimer *rectangle_selection_timer = nullptr;
    QRect pending_rectangle_selection_rect;
    bool rectangle_selection_update_pending = false;
#endif
};

#endif // MAP_EDITOR_CONTROLLER_H
