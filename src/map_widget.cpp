#include "map_widget.h"

#include <cmath>

MapWidget::MapWidget(QWidget *parent)
    : QWidget(parent),
    m_model(new MapModel(this)),
    m_ownsModel(true),
    m_cache(2000)
{
    init();
}

MapWidget::MapWidget(MapModel *model, QWidget *parent)
    : QWidget(parent),
    m_model(model),
    m_ownsModel(false),
    m_cache(2000)
{
    if (!m_model)
    {
        m_model = new MapModel(this);
        m_ownsModel = true;
    }
    
    init();
}

MapModel *MapWidget::model() const
{
    return m_model;
}

void MapWidget::init()
{
    initRestConnection();
    
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &MapWidget::customContextMenuRequested,
            this, &MapWidget::showContextMenu);
    
    setMinimumHeight(500);
    setMinimumWidth(550);
    setContentsMargins(0, 0, 0, 0);
    
    setFocusPolicy(Qt::StrongFocus);
    setFocus();
    setMouseTracking(true);
    
    connect(m_model, &MapModel::zoomChanged,
            this, &MapWidget::signalZoomChanged);
    
    connect(m_model, &MapModel::centerChanged,
            this, &MapWidget::signalCoordsChangedWgs84);
    
    connect(m_model, &MapModel::providerChanged,
            this, [this](MapProvider) {
                update();
            });
    
    QTimer::singleShot(100, this, [this] {
        emit signalZoomChanged(m_model->zoom());
        CoordinateWGS84 wgs;
        wgs.lat = m_model->centerLat();
        wgs.lon = m_model->centerLon();
        emit signalCoordsChangedWgs84(wgs);
    });
    
    initTimer();
}

void MapWidget::initRestConnection()
{
    m_rest = new RESTClient("http://aowis-server-map.localhost:80", this);
    
    connect(m_rest, &RESTClient::requestFinishedTile,
            this, [this](const QByteArray &data, const QString &key) {
                m_pending.remove(key);
                
                QPixmap pix;
                if (!pix.loadFromData(data))
                {
                    qDebug() << "Tile decode failed:" << key;
                    return;
                }
                
                m_cache.insert(key, new QPixmap(pix));
                update();
            });
    
    connect(m_rest, &RESTClient::requestError,
            this, [this](const QString &err) {
                qDebug() << "Tile request failed:" << err;
            });
}

void MapWidget::initTimer()
{
    m_timerPanInertia = new QTimer(this);
    m_timerPanInertia->setInterval(16);
    
    connect(m_timerPanInertia, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const double dt = (now - m_timeLastInertia) / 16.0;
        m_timeLastInertia = now;
        
        if (m_panVelocity.manhattanLength() < 0.1)
        {
            m_panVelocity = QPointF(0, 0);
            m_timerPanInertia->stop();
            return;
        }
        
        const QPointF move = m_panVelocity * dt;
        m_model->panByPixels(QPoint(move.x(), move.y()), size());

#ifdef Q_OS_WASM
        const double frictionPerFrame = 0.95;
#else
        const double frictionPerFrame = 0.95;
#endif
        const double friction = std::pow(frictionPerFrame, dt);
        m_panVelocity *= friction;
        
        update();
    });
}

void MapWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Left)
        addPanVelocity(1, 0);
    else if (event->key() == Qt::Key_Right)
        addPanVelocity(-1, 0);
    else if (event->key() == Qt::Key_Up)
        addPanVelocity(0, 1);
    else if (event->key() == Qt::Key_Down)
        addPanVelocity(0, -1);
    
    else if (event->key() == Qt::Key_U)
        addPanVelocity(1, 0);
    else if (event->key() == Qt::Key_A)
        addPanVelocity(-1, 0);
    else if (event->key() == Qt::Key_V)
        addPanVelocity(0, 1);
    else if (event->key() == Qt::Key_I)
        addPanVelocity(0, -1);
    
    else if (event->key() == Qt::Key_Shift)
        zoomIn();
    else if (event->key() == Qt::Key_Space)
        zoomOut();
    
    else
        QWidget::keyPressEvent(event);
    
    event->accept();
    return;
}
void MapWidget::addPanVelocity(int x, int y)
{
    const int step = 20;
    
    if (x >= 1)
        m_panVelocity += QPointF(step, 0);
    else if (x <= -1)
        m_panVelocity += QPointF(-step, 0);
    else if (y >= 1)
        m_panVelocity += QPointF(0, step);
    else if (y <= -1)
        m_panVelocity += QPointF(0, -step);
    
    m_timeLastInertia = QDateTime::currentMSecsSinceEpoch();
    
    if (!m_timerPanInertia->isActive())
        m_timerPanInertia->start();
}
void MapWidget::wheelEvent(QWheelEvent *ev)
{
    static int accumulated = 0;
    
    accumulated += ev->angleDelta().y();
    
    const int threshold = 120;
    
    if (std::abs(accumulated) < threshold)
        return;
    
    const int steps = accumulated / threshold;
    accumulated %= threshold;
    
    m_model->zoomByAt(steps, ev->position().toPoint(), size());
    update();
}

void MapWidget::mousePressEvent(QMouseEvent *ev)
{
    if (ev->buttons() & Qt::LeftButton)
    {
        m_timeLastInertia = QDateTime::currentMSecsSinceEpoch();
        m_timerPanInertia->stop();
        m_panVelocity = QPointF(0, 0);
    }
    
    m_posLast = ev->pos();
}

void MapWidget::mouseReleaseEvent(QMouseEvent *ev)
{
    Q_UNUSED(ev)
    
    /*
    if (!m_timerPanInertia->isActive())
        m_timerPanInertia->start();
    */
}

void MapWidget::mouseMoveEvent(QMouseEvent *ev)
{
    const CoordinateWGS84 wgs = m_model->wgs84FromScreen(ev->pos(), size());
    emit signalCoordsChangedWgs84(wgs);
    
    if (ev->buttons() & Qt::LeftButton)
    {
        const QPoint d = ev->pos() - m_posLast;
        
        #ifdef Q_OS_WASM
            m_panVelocity = m_panVelocity * 0 + QPointF(d) * 0;
        #else
            m_panVelocity = m_panVelocity * 0 + QPointF(d) * 0;
        #endif
        
        m_model->panByPixels(d, size());
        update();
    }
    
    m_posLast = ev->pos();
}

void MapWidget::zoomIn()
{
    m_model->zoomIn(size());
    update();
}

void MapWidget::zoomOut()
{
    m_model->zoomOut(size());
    update();
}

void MapWidget::changeMapProvider(MapProvider provider)
{
    m_model->setProvider(provider);
    update();
}

void MapWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    drawTiles(p);
}

void MapWidget::drawTiles(QPainter &p)
{
    const int tiles = m_model->tileCount();
    
    const QPointF center = m_model->centerTile();
    const double cx = center.x();
    const double cy = center.y();
    
    const int w = width();
    const int h = height();
    
    const int tilesX = w / MapModel::TileSize + 4;
    const int tilesY = h / MapModel::TileSize + 4;
    
    const int startX = int(cx) - tilesX / 2;
    const int startY = int(cy) - tilesY / 2;
    
    for (int dx = 0; dx < tilesX; ++dx)
    {
        for (int dy = 0; dy < tilesY; ++dy)
        {
            const int x = startX + dx;
            const int y = startY + dy;
            
            if (x < 0 || x >= tiles || y < 0 || y >= tiles)
                continue;
            
            const QString key = m_model->tileCacheKey(x, y);
            
            if (!m_cache.contains(key))
                requestTile(key, x, y);
            
            if (QPixmap *pix = m_cache.object(key))
            {
                const int px = int((x - cx) * MapModel::TileSize + w / 2);
                const int py = int((y - cy) * MapModel::TileSize + h / 2);
                p.drawPixmap(px, py, *pix);
            }
        }
    }
}

void MapWidget::requestTile(const QString &key, int x, int y)
{
    if (m_pending.contains(key))
        return;
    
    m_pending.insert(key);
    m_rest->getTile(m_model->tileEndpoint(x, y), key);
}

void MapWidget::showContextMenu(const QPoint &pos)
{
    QMenu *menu = new QMenu(this);
    QAction *actionElevation = menu->addAction("Get Elevation");
    Q_UNUSED(actionElevation)
    
    menu->popup(mapToGlobal(pos));
}
