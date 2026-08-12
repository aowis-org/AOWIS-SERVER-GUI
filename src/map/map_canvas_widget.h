#ifndef MAP_CANVAS_WIDGET_H
#define MAP_CANVAS_WIDGET_H

#include <QPointer>
#include <QPointF>
#include <QWidget>

#include "map_editor_renderer.h"
#include "map_model.h"
#include "map_widget.h"
#include "map_canvas_entities.h"

#include "../hydraulic_data.h"

class MapEditorController;
class QFocusEvent;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

class MapCanvasWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MapCanvasWidget(MapModel *map_model, MapWidget *map, HydraulicData *hydraulic_data,
                             QWidget *parent = nullptr);

    void setEditorController(MapEditorController *editor_controller);
    MapCanvasEntities *mapCanvasEntities() const;
    int backgroundOpacity() const;
    MapEditorVisualState visualState() const;
    MapEditorViewportRenderState viewportRenderState() const;
    void requestRenderUpdate();

public slots:
    void setBackgroundOpacity(int opacity);
    void setIconSizePercent(int size_percent);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void applyControllerState();

    MapModel *map_model = nullptr;
    MapWidget *map = nullptr;
    HydraulicData *hydraulic_data = nullptr;
    MapEditorRenderer map_editor_renderer;
    MapCanvasEntities *map_canvas_entities = nullptr;
    QPointer<MapEditorController> editor_controller;
    QPointF last_pointer_position;
    bool last_pointer_position_valid = false;

    // 0 = transparent, 100 = fully system background
    int map_background_opacity = 0;
};

#endif // MAP_CANVAS_WIDGET_H
