#include "map_widget.h"

MapWidget::MapWidget(QWidget *parent)
    : QWidget(parent),
    zoom(15),
    center_lon(18.20982),
    center_lat(11.98236),
    cache(2000),
    rest(new RESTClient("http://aowis-server-map.localhost:80", this))
{
    connect(this->rest, &RESTClient::requestFinishedTile, this, [this](const QByteArray &data, const QString &key)
        {
            QPixmap pix;
            pix.loadFromData(data);
            
            this->cache.insert(key, new QPixmap(pix));
            
            update();
        });
    connect(this->rest, &RESTClient::requestError, this, [this](const QString &err)
        {
            qDebug() << "fail: " << err;
        });
    
    this->setMinimumHeight(500);
    this->setMinimumWidth(550);
    
    setFocusPolicy(Qt::StrongFocus);
    setFocus();
    setMouseTracking(true);
    
    // make the status bar show correct zoom level from the start
    QTimer::singleShot(0, this, [this] {
        emit signalZoomChanged(this->zoom);
        emit signalCoordsChanged(this->center_lon, this->center_lat);
    });
    
    initializeTimer();
}
void MapWidget::initializeTimer()
{
    this->timer_pan_inertia = new QTimer(this);
    this->timer_pan_inertia->setInterval(16); // target ~60 FPS
    
    connect(this->timer_pan_inertia, &QTimer::timeout, this, [this]()
            {
                qint64 now = QDateTime::currentMSecsSinceEpoch();
                double dt = (now - this->time_last_innertia) / 16.0;  // normalize to 60 FPS
                this->time_last_innertia = now;
                
                if (pan_velocity.manhattanLength() < 0.1)
                {
                    pan_velocity = QPointF(0,0);
                    timer_pan_inertia->stop();
                    return;
                }
                
                // movement scaled by dt
                QPointF move = pan_velocity * dt;
                pan(QPoint(move.x(), move.y()));
                
                // time‑based friction
                #ifdef Q_OS_WASM
                    double friction_per_frame = 0.95;
                #else
                    double friction_per_frame = 0.95;
                #endif
                double friction = pow(friction_per_frame, dt);
                pan_velocity *= friction;
                
                update();
            });
    
}


void MapWidget::keyPressEvent(QKeyEvent *ev)
{
    const int step = 20; // base movement in pixels
    
    switch (ev->key())
    {
    case Qt::Key_Left:
        this->pan_velocity += QPointF(step, 0);
        break;
    case Qt::Key_Right:
        this->pan_velocity += QPointF(-step, 0);
        break;
    case Qt::Key_Up:
        this->pan_velocity += QPointF(0, step);
        break;
    case Qt::Key_Down:
        this->pan_velocity += QPointF(0, -step);
        break;
    default:
        QWidget::keyPressEvent(ev);
        return;
    }
    
    this->time_last_innertia = QDateTime::currentMSecsSinceEpoch();
    
    if (!this->timer_pan_inertia->isActive())
        this->timer_pan_inertia->start();
}

void MapWidget::wheelEvent(QWheelEvent *ev)
{
    static int accumulated = 0;
    
    accumulated += ev->angleDelta().y();
    
    const int threshold = 120; // one mouse wheel step
    
    if (std::abs(accumulated) < threshold)
        return;
    
    int steps = accumulated / threshold;
    accumulated %= threshold;
    
    // mouse position in widget
    QPoint pos = ev->position().toPoint();
    int w = width();
    int h = height();
    
    double cx = lonToTileX(center_lon, zoom);
    double cy = latToTileY(center_lat, zoom);
    
    double dx = pos.x() - w / 2.0;
    double dy = pos.y() - h / 2.0;
    
    double mx = cx + dx / TILE_SIZE;
    double my = cy + dy / TILE_SIZE;
    
    double lon_mouse = tileXToLon(mx, zoom);
    double lat_mouse = tileYToLat(my, zoom);
    
    int zoom_new = std::clamp(zoom + steps, 1, 19);
    if (zoom_new == zoom)
        return;
    
    zoom = zoom_new;
    
    double mx2 = lonToTileX(lon_mouse, zoom);
    double my2 = latToTileY(lat_mouse, zoom);
    
    double cx2 = mx2 - dx / TILE_SIZE;
    double cy2 = my2 - dy / TILE_SIZE;
    
    center_lon = tileXToLon(cx2, zoom);
    center_lat = tileYToLat(cy2, zoom);
    
    update();
    emit signalZoomChanged(zoom);
}
void MapWidget::mousePressEvent(QMouseEvent *ev)
{
    this->time_last_innertia = QDateTime::currentMSecsSinceEpoch();
    
    this->timer_pan_inertia->stop();
    this->pan_velocity = QPointF(0,0);
    
    this->pos_last = ev->pos();
}
void MapWidget::mouseReleaseEvent(QMouseEvent *ev)
{
    /*
    if (!this->timer_pan_inertia->isActive())
        this->timer_pan_inertia->start();
    */
}
void MapWidget::mouseMoveEvent(QMouseEvent *ev)
{
    QPointF ll = latLonUnderCursor(ev->pos());
    emit signalCoordsChanged(ll.x(), ll.y());
    
    if (ev->buttons() & Qt::LeftButton)
    {
        QPoint d = ev->pos() - this->pos_last;
        this->pos_last = ev->pos();
        
        // first value: inertia memory: how much of the previous movement kept
        // second value: responsiveness: adds some of the new drag movement
        #ifdef Q_OS_WASM
            this->pan_velocity = this->pan_velocity * 0 + QPointF(d) * 0;
        #else
            this->pan_velocity = this->pan_velocity * 0 + QPointF(d) * 0;
        #endif
        
        pan(d);
        update();
    }
}
void MapWidget::zoomIn()
{
    this->zoom++;
    if (this->zoom > 19)
        this->zoom = 19;
    
    update();
    
    emit signalZoomChanged(this->zoom);
}
void MapWidget::zoomOut()
{
    this->zoom--;
    if (this->zoom < 1)
        this->zoom = 1;
    
    update();
    
    emit signalZoomChanged(this->zoom);
}
void MapWidget::changeMapProvider(MapProvider provider)
{
    this->map_provider = provider;
    
    switch (provider)
    {
    case MapProvider::ArcGISSat:
        this->cache_key_provider = "arcgis";
        break;
    case MapProvider::OpenTopoMap:
        this->cache_key_provider = "opentopomap";
        break;
    case MapProvider::OpenStreetMap:
        this->cache_key_provider = "openstreetmap";
        break;
    }
    
    update();
}

void MapWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    drawTiles(p);
}

void MapWidget::drawTiles(QPainter &p)
{
    const int tiles = 1 << zoom;
    
    double cx = lonToTileX(this->center_lon, this->zoom);
    double cy = latToTileY(this->center_lat, this->zoom);
    
    int w = width();
    int h = height();
    
    int tiles_x = w / this->TILE_SIZE + 4;
    int tiles_y = h / this->TILE_SIZE + 4;
    
    int start_x = int(cx) - tiles_x / 2;
    int start_y = int(cy) - tiles_y / 2;
    
    for (int dx = 0; dx < tiles_x; dx++)
    {
        for (int dy = 0; dy < tiles_y; dy++)
        {
            int x = start_x + dx;
            int y = start_y + dy;
            
            if (x < 0 || x >= tiles || y < 0 || y >= tiles)
                continue;
            
            QString key = this->cache_key_provider + QString("/%1/%2/%3").arg(zoom).arg(x).arg(y);
            
            if (!this->cache.contains(key))
            {
                requestTile(key, x, y);
            }
            
            if (this->cache.contains(key))
            {
                QPixmap *pix = this->cache.object(key);
                int px = int((x - cx) * this->TILE_SIZE + w / 2);
                int py = int((y - cy) * this->TILE_SIZE + h / 2);
                p.drawPixmap(px, py, *pix);
            }
        }
    }
}

void MapWidget::pan(const QPoint &d)
{
    // convert center to tile coords
    double cx = lonToTileX(center_lon, zoom);
    double cy = latToTileY(center_lat, zoom);
    
    // apply pixel delta in tile space
    cx -= double(d.x()) / TILE_SIZE;
    cy -= double(d.y()) / TILE_SIZE;
    
    // convert back to lat/lon
    center_lon = tileXToLon(cx, zoom);
    center_lat = tileYToLat(cy, zoom);
    
    clampCenter();
    
    emit signalCoordsChanged(center_lon, center_lat);
}

void MapWidget::clampCenter()
{
    double cx = lonToTileX(center_lon, zoom);
    double cy = latToTileY(center_lat, zoom);
    
    double max_tile = (1 << zoom) - 1;
    
    double half_w = (width()  / double(TILE_SIZE)) / 2.0;
    double half_h = (height() / double(TILE_SIZE)) / 2.0;
    
    double min_cx = half_w;
    double max_cx = max_tile - half_w;
    
    double min_cy = half_h;
    double max_cy = max_tile - half_h;
    
    // --- CASE 1: Map smaller than screen horizontally ---
    if (min_cx > max_cx) {
        // exact center of world in tile coords
        cx = max_tile / 2.0;
    } else {
        cx = std::clamp(cx, min_cx, max_cx);
    }
    
    // --- CASE 2: Map smaller than screen vertically ---
    if (min_cy > max_cy) {
        cy = max_tile / 2.0;
    } else {
        cy = std::clamp(cy, min_cy, max_cy);
    }
    
    // convert back to lat/lon
    center_lon = tileXToLon(cx, zoom);
    center_lat = tileYToLat(cy, zoom);
}

void MapWidget::requestTile(const QString &key, int x, int y)
{
    if (this->pending.contains(key))
        return;
    
    this->pending.insert(key);
    
    QString endpoint;
    switch (this->map_provider)
    {
    case MapProvider::ArcGISSat:
        endpoint = QString("/arcgis/%1/%2/%3.png").arg(this->zoom).arg(x).arg(y);
        break;
    case MapProvider::OpenTopoMap:
        endpoint = QString("/opentopomap/%1/%2/%3.png").arg(this->zoom).arg(x).arg(y);
        break;
    case MapProvider::OpenStreetMap:
        endpoint = QString("/openstreetmap/%1/%2/%3.png").arg(this->zoom).arg(x).arg(y);
        break;
    }
    // fallback, because only OSM has zoom level > 17
    if (this->zoom > 17)
    {
        endpoint = QString("/openstreetmap/%1/%2/%3.png").arg(this->zoom).arg(x).arg(y);
    }
    
    this->rest->getTile(endpoint, key);
}

QPointF MapWidget::latLonUnderCursor(const QPoint &pos) const
{
    double cx = lonToTileX(center_lon, zoom);
    double cy = latToTileY(center_lat, zoom);
    
    double dx = pos.x() - width()  / 2.0;
    double dy = pos.y() - height() / 2.0;
    
    double tx = cx + dx / TILE_SIZE;
    double ty = cy + dy / TILE_SIZE;
    
    double lon = tileXToLon(tx, zoom);
    double lat = tileYToLat(ty, zoom);
    
    return QPointF(lon, lat);
}


double MapWidget::lonToTileX(double lon, int zoom) const
{
    return (lon + 180.0) / 360.0 * (1 << zoom);
}
double MapWidget::latToTileY(double lat, int zoom) const
{
    double rad = qDegreesToRadians(lat);
    return (1.0 - log(tan(rad) + 1.0 / cos(rad)) / M_PI) / 2.0 * (1 << zoom);
}
double MapWidget::tileXToLon(double x, int zoom) const
{
    return x / (1 << zoom) * 360.0 - 180.0;
}
double MapWidget::tileYToLat(double y, int zoom) const
{
    double n = M_PI - 2.0 * M_PI * y / (1 << zoom);
    return qRadiansToDegrees(atan(0.5 * (exp(n) - exp(-n))));
}
