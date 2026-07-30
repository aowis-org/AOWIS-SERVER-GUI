#include "map_widget.h"

#ifndef Q_OS_WASM
#include <QGeoCoordinate>
#endif

#include <cmath>

MapWidget::MapWidget(MapTileRepository *tile_repository, GpsProvider *gps, QWidget *parent)
    : QWidget(parent),
    gps(gps),
    m_model(new MapModel(this)),
    tile_repository(tile_repository)
{
    if (!this->tile_repository)
        this->tile_repository = new MapTileRepository(this);

    this->init();
}

MapWidget::MapWidget(MapModel *model, MapTileRepository *tile_repository, GpsProvider *gps, QWidget *parent)
    : QWidget(parent),
    gps(gps),
    m_model(model),
    tile_repository(tile_repository)
{
    if (!this->m_model)
        this->m_model = new MapModel(this);

    if (!this->tile_repository)
        this->tile_repository = new MapTileRepository(this);

    this->init();
}

MapModel *MapWidget::model() const
{
    return this->m_model;
}

void MapWidget::init()
{
    this->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &MapWidget::customContextMenuRequested, this, &MapWidget::showContextMenu);

    this->setMinimumHeight(500);
    this->setMinimumWidth(550);
    this->setContentsMargins(0, 0, 0, 0);

    this->setFocusPolicy(Qt::StrongFocus);
    this->setFocus();
    this->setMouseTracking(true);

    connect(this->m_model, &MapModel::zoomChanged, this, [this](int zoom)
    {
        emit signalZoomChanged(zoom);
        this->update();
    });

    connect(this->m_model, &MapModel::centerChangedWGS84, this, [this](CoordinateWGS84 wgs)
    {
        emit signalCoordsChangedWgs84(wgs);
        this->update();
    });

    connect(this->m_model, &MapModel::centerChangedUTM, this, &MapWidget::signalCoordsChangedUTM);
    connect(this->m_model, &MapModel::providerChanged, this, [this](MapProvider)
    {
        this->update();
    });

    connect(this->tile_repository, &MapTileRepository::signalTileAvailable, this, [this](const QString &)
    {
        this->update();
    });
    connect(this->tile_repository, &MapTileRepository::signalTileFailed, this, [this](const QString &)
    {
        this->update();
    });

    if (this->gps)
    {
        connect(this->gps, &GpsProvider::positionChanged, this, [this](const QGeoPositionInfo &info)
        {
#ifndef Q_OS_WASM
            const QGeoCoordinate coord = info.coordinate();

            this->gps_coordinate.latitude_deg = coord.latitude();
            this->gps_coordinate.longitude_deg = coord.longitude();
            this->gps_coordinate.altitude_m = coord.altitude();
#endif
        });
        connect(this->gps, &GpsProvider::statusMessage, this, [](const QString &message)
        {
            qDebug() << message;
        });
    }

    QTimer::singleShot(100, this, [this]
    {
        emit signalZoomChanged(this->m_model->zoom());

        CoordinateWGS84 wgs;
        wgs.latitude_deg = this->m_model->centerLat();
        wgs.longitude_deg = this->m_model->centerLon();
        emit signalCoordsChangedWgs84(wgs);

        GeoMetricProjection projection;
        const CoordinateUTM utm = projection.wgs84ToUtm(wgs);
        emit signalCoordsChangedUTM(utm);
    });

    this->initTimer();
}

void MapWidget::initTimer()
{
    this->m_timerPanInertia = new QTimer(this);
    this->m_timerPanInertia->setInterval(16);

    connect(this->m_timerPanInertia, &QTimer::timeout, this, [this]
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const double dt = (now - this->m_timeLastInertia) / 16.0;
        this->m_timeLastInertia = now;

        if (this->m_panVelocity.manhattanLength() < 0.1)
        {
            this->m_panVelocity = QPointF(0, 0);
            this->m_timerPanInertia->stop();
            return;
        }

        const QPointF move = this->m_panVelocity * dt;
        this->m_model->panByPixels(QPoint(move.x(), move.y()), this->size());

        const double friction_per_frame = 0.95;
        const double friction = std::pow(friction_per_frame, dt);
        this->m_panVelocity *= friction;
    });
}

void MapWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key())
    {
    case Qt::Key_Left:
    case Qt::Key_U:
        this->addPanVelocity(1, 0);
        break;

    case Qt::Key_Right:
    case Qt::Key_A:
        this->addPanVelocity(-1, 0);
        break;

    case Qt::Key_Up:
    case Qt::Key_V:
        this->addPanVelocity(0, 1);
        break;

    case Qt::Key_Down:
    case Qt::Key_I:
        this->addPanVelocity(0, -1);
        break;

    case Qt::Key_L:
    case Qt::Key_Shift:
        this->zoomIn();
        break;

    case Qt::Key_X:
    case Qt::Key_Space:
        this->zoomOut();
        break;

    default:
        QWidget::keyPressEvent(event);
        return;
    }

    event->accept();
}

void MapWidget::addPanVelocity(int x, int y)
{
    const int step = 20;

    if (x >= 1)
        this->m_panVelocity += QPointF(step, 0);
    else if (x <= -1)
        this->m_panVelocity += QPointF(-step, 0);
    else if (y >= 1)
        this->m_panVelocity += QPointF(0, step);
    else if (y <= -1)
        this->m_panVelocity += QPointF(0, -step);

    this->m_timeLastInertia = QDateTime::currentMSecsSinceEpoch();

    if (!this->m_timerPanInertia->isActive())
        this->m_timerPanInertia->start();
}

void MapWidget::panUp()
{
    this->addPanVelocity(0, 1);
}

void MapWidget::panDown()
{
    this->addPanVelocity(0, -1);
}

void MapWidget::panLeft()
{
    this->addPanVelocity(1, 0);
}

void MapWidget::panRight()
{
    this->addPanVelocity(-1, 0);
}

void MapWidget::wheelEvent(QWheelEvent *event)
{
    this->onMouseWheel(event);
}

void MapWidget::onMouseWheel(QWheelEvent *event)
{
    this->wheel_delta_accumulated += event->angleDelta().y();

    const int threshold = 120;

    if (std::abs(this->wheel_delta_accumulated) < threshold)
        return;

    const int steps = this->wheel_delta_accumulated / threshold;
    this->wheel_delta_accumulated %= threshold;

    this->m_model->zoomByAt(steps, event->position().toPoint(), this->size());
}

void MapWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        this->m_timeLastInertia = QDateTime::currentMSecsSinceEpoch();
        this->m_timerPanInertia->stop();
        this->m_panVelocity = QPointF(0, 0);
    }

    this->m_posLast = event->pos();
}

void MapWidget::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
}

void MapWidget::mouseMoveEvent(QMouseEvent *event)
{
    this->onMouseMove(event);
}

void MapWidget::onMouseMove(QMouseEvent *event)
{
    const CoordinateWGS84 wgs = this->m_model->wgs84FromScreen(event->pos(), this->size());
    emit signalCoordsChangedWgs84(wgs);

    GeoMetricProjection projection;
    const CoordinateUTM utm = projection.wgs84ToUtm(wgs);
    emit signalCoordsChangedUTM(utm);

    if (event->buttons() & Qt::LeftButton)
    {
        const QPoint delta = event->pos() - this->m_posLast;
        this->m_panVelocity = QPointF(0, 0);
        this->m_model->panByPixels(delta, this->size());
    }

    this->m_posLast = event->pos();
}

void MapWidget::zoomIn()
{
    this->m_model->zoomIn(this->size());
}

void MapWidget::zoomOut()
{
    this->m_model->zoomOut(this->size());
}

void MapWidget::changeMapProvider(MapProvider provider)
{
    this->m_model->setProvider(provider);
}

void MapWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    this->drawTiles(painter);

    const QPointF gps_point = this->m_model->screenFromWgs84(this->gps_coordinate, this->size());

    painter.setBrush(Qt::red);
    painter.drawEllipse(gps_point, 5.0, 5.0);
}

void MapWidget::drawTiles(QPainter &painter)
{
    const int tiles = this->m_model->tileCount();

    const QPointF center = this->m_model->centerTile();
    const double center_x = center.x();
    const double center_y = center.y();

    const int viewport_width = this->width();
    const int viewport_height = this->height();

    const int tiles_x = viewport_width / MapModel::TileSize + 4;
    const int tiles_y = viewport_height / MapModel::TileSize + 4;

    const int start_x = int(center_x) - tiles_x / 2;
    const int start_y = int(center_y) - tiles_y / 2;

    for (int delta_x = 0; delta_x < tiles_x; ++delta_x)
    {
        for (int delta_y = 0; delta_y < tiles_y; ++delta_y)
        {
            const int x = start_x + delta_x;
            const int y = start_y + delta_y;

            if (x < 0 || x >= tiles || y < 0 || y >= tiles)
                continue;

            const QString key = this->m_model->tileCacheKey(x, y);
            QPixmap *pixmap = this->tile_repository->tile(key);

            if (!pixmap)
            {
                this->tile_repository->requestTile(this->m_model->tileEndpoint(x, y), key, x, y);
                continue;
            }

            const int pixel_x = int((x - center_x) * MapModel::TileSize + viewport_width / 2);
            const int pixel_y = int((y - center_y) * MapModel::TileSize + viewport_height / 2);
            painter.drawPixmap(pixel_x, pixel_y, *pixmap);
        }
    }
}

void MapWidget::showContextMenu(const QPoint &pos)
{
    QMenu *menu = new QMenu(this);
    QAction *action_elevation = menu->addAction("Get Elevation");
    Q_UNUSED(action_elevation)

    menu->popup(this->mapToGlobal(pos));
}
