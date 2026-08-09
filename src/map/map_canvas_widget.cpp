#include "map_canvas_widget.h"

#include "map_editor_controller.h"

#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

MapCanvasWidget::MapCanvasWidget(MapModel *map_model, MapWidget *map,
                                 HydraulicData *hydraulic_data, QWidget *parent)
    : QWidget(parent), map_model(map_model), map(map), hydraulic_data(hydraulic_data),
      map_editor_renderer(map_model, this),
      map_canvas_entities(new MapCanvasEntities(map_model, hydraulic_data, this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);

    connect(this->map_model, &MapModel::zoomChanged, this, [this]
    {
        requestRenderUpdate();
    });
    connect(this->map_model, &MapModel::centerChangedWGS84, this, [this]
    {
        requestRenderUpdate();
    });

    if (this->hydraulic_data)
    {
        connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, [this]
        {
            requestRenderUpdate();
        });
        connect(this->hydraulic_data, &HydraulicData::signalNetworkGeometryChanged,
                this, [this](quint64)
        {
            requestRenderUpdate();
        });
    }
}

void MapCanvasWidget::setEditorController(MapEditorController *editor_controller)
{
    if (this->editor_controller == editor_controller)
        return;

    if (this->editor_controller)
        disconnect(this->editor_controller, nullptr, this, nullptr);

    this->editor_controller = editor_controller;
    if (this->editor_controller)
    {
        connect(this->editor_controller, &MapEditorController::signalStateChanged,
                this, &MapCanvasWidget::applyControllerState);
        connect(this->editor_controller, &MapEditorController::signalFocusRequested,
                this, [this]
        {
            setFocus(Qt::OtherFocusReason);
        });
    }

    applyControllerState();
}

MapCanvasEntities *MapCanvasWidget::mapCanvasEntities() const
{
    return this->map_canvas_entities;
}

int MapCanvasWidget::backgroundOpacity() const
{
    return this->map_background_opacity;
}

MapEditorVisualState MapCanvasWidget::visualState() const
{
    return this->map_canvas_entities->visualState();
}

void MapCanvasWidget::setBackgroundOpacity(int opacity)
{
    opacity = qBound(0, opacity, 100);
    if (this->map_background_opacity == opacity)
        return;

    this->map_background_opacity = opacity;
    requestRenderUpdate();
}

void MapCanvasWidget::setIconSizePercent(int size_percent)
{
    this->map_canvas_entities->setIconSizePercent(size_percent);
}

void MapCanvasWidget::applyControllerState()
{
    if (!this->editor_controller)
    {
        unsetCursor();
        requestRenderUpdate();
        return;
    }

    const std::optional<Qt::CursorShape> cursor_shape = this->editor_controller->cursorShape();
    if (cursor_shape.has_value())
        setCursor(cursor_shape.value());
    else
        unsetCursor();

    requestRenderUpdate();
}

void MapCanvasWidget::requestRenderUpdate()
{
#ifndef Q_OS_WASM
    update();
#endif
}

void MapCanvasWidget::paintEvent(QPaintEvent *event)
{
#ifdef Q_OS_WASM
    Q_UNUSED(event)
    return;
#else
    static const NetworkRenderSnapshot empty_network_snapshot;
    const NetworkRenderSnapshot &network_snapshot = this->hydraulic_data
        ? this->hydraulic_data->networkRenderSnapshot()
        : empty_network_snapshot;

    QPainter painter(this);
    this->map_editor_renderer.paint(
        painter, *event, network_snapshot, this->map_canvas_entities->visualState(),
        viewportRenderState());

    static const QPixmap crosshair_pixmap =
        QPixmap(QStringLiteral(":/icon/crosshair.png")).scaled(
            QSize(40, 40), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!crosshair_pixmap.isNull())
    {
        const QPoint crosshair_position(
            (width() - crosshair_pixmap.width()) / 2,
            (height() - crosshair_pixmap.height()) / 2);
        painter.drawPixmap(crosshair_position, crosshair_pixmap);
    }
#endif
}

MapEditorViewportRenderState MapCanvasWidget::viewportRenderState() const
{
    MapEditorViewportRenderState state;
    state.background_opacity = this->map_background_opacity;

    if (!this->editor_controller)
        return state;

    const MapEditorController::TileSelectionOverlay &overlay =
        this->editor_controller->tileSelectionOverlay();
    if (overlay.visible)
    {
        const MapEditorController::TileSelectionRange range =
            this->editor_controller->tileSelectionRange(this->map_model->zoom());
        if (range.valid)
        {
            state.tile_selection_visible = true;
            state.tile_x_min = range.tile_x_min;
            state.tile_x_max = range.tile_x_max;
            state.tile_y_min = range.tile_y_min;
            state.tile_y_max = range.tile_y_max;
        }
    }

    state.rectangle_selection_visible =
        this->editor_controller->rectangleSelectionActive() &&
        this->editor_controller->rectangleDragging();
    if (state.rectangle_selection_visible)
    {
        state.rectangle_selection =
            this->editor_controller->currentSelectionRect(size());
    }

    return state;
}

void MapCanvasWidget::keyPressEvent(QKeyEvent *event)
{
    if (this->editor_controller && this->editor_controller->keyPress(Qt::Key(event->key())))
    {
        event->accept();
        return;
    }

    if (this->map->handleKeyPressEvent(event))
        return;

    QWidget::keyPressEvent(event);
}

void MapCanvasWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (this->map->handleKeyReleaseEvent(event))
        return;

    QWidget::keyReleaseEvent(event);
}

void MapCanvasWidget::focusOutEvent(QFocusEvent *event)
{
    this->map->clearKeyboardPanInput();
    QWidget::focusOutEvent(event);
}

void MapCanvasWidget::hideEvent(QHideEvent *event)
{
    this->map_editor_renderer.setRenderingActive(false);
    QWidget::hideEvent(event);
}

void MapCanvasWidget::mousePressEvent(QMouseEvent *event)
{
    if (this->editor_controller &&
        this->editor_controller->mousePress(event->position(), event->globalPosition().toPoint(),
                                            event->button(), size()))
    {
        event->accept();
        return;
    }

    if (this->map->handleMousePressEvent(event))
        return;

    QWidget::mousePressEvent(event);
}

void MapCanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    const bool map_handled_event = this->map->handleMouseMoveEvent(event);
    const bool editor_handled_event = this->editor_controller &&
        this->editor_controller->mouseMove(event->position(), size(), !map_handled_event);

    if (editor_handled_event)
    {
        event->accept();
        return;
    }

    if (map_handled_event)
        return;

    QWidget::mouseMoveEvent(event);
}

void MapCanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->editor_controller &&
        this->editor_controller->mouseRelease(event->position(), event->button(), size()))
    {
        event->accept();
        return;
    }

    if (this->map->handleMouseReleaseEvent(event))
        return;

    QWidget::mouseReleaseEvent(event);
}

void MapCanvasWidget::wheelEvent(QWheelEvent *event)
{
    this->map->handleWheelEvent(event);
}

void MapCanvasWidget::resizeEvent(QResizeEvent *event)
{
    this->map_canvas_entities->positionMarkers();
    QWidget::resizeEvent(event);
}

void MapCanvasWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    this->map_editor_renderer.setRenderingActive(true);
    requestRenderUpdate();
}
