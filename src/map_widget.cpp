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
}

void MapWidget::wheelEvent(QWheelEvent *ev)
{
    /*
    int delta = ev->angleDelta().y();
    this->zoom += (delta > 0 ? 1 : -1);
    this->zoom = std::clamp(zoom, 1, 19);
    update();
    */
    
    // mouse position in widget
    QPoint pos = ev->position().toPoint();
    int w = width();
    int h = height();
    
    // current center in tile coords
    double cx = lonToTileX(this->center_lon, this->zoom);
    double cy = latToTileY(this->center_lat, this->zoom);
    
    // offset from center pixels
    double dx = pos.x() - w / 2.0;
    double dy = pos.y() - h / 2.0;
    
    // tile coords under mouse before zoom
    double mx = cx + dx / this->tile_size;
    double my = cy + dy / this->tile_size;
    
    double lon_mouse = tileXToLon(mx, this->zoom);
    double lat_mouse = tileYToLat(my, this->zoom);
    
    // apply zoom change
    int delta = ev->angleDelta().y();
    int zoom_new = std::clamp(this->zoom + (delta > 0 ? 1 : -1), 1, 19);
    
    if (zoom_new == this->zoom)
        return;
    
    this->zoom = zoom_new;
    
    // tile coords of mouse at new zoom
    double mx2 = lonToTileX(lon_mouse, this->zoom);
    double my2 = latToTileY(lat_mouse, this->zoom);
    
    // new center tile coords so that mouse stays on samle lon/lat
    double cx2 = mx2 - dx / this->tile_size;
    double cy2 = my2 - dy / this->tile_size;
    
    this->center_lon = tileXToLon(cx2, this->zoom);
    this->center_lat = tileYToLat(cy2, this->zoom);
    
    update();
}
void MapWidget::mousePressEvent(QMouseEvent *ev)
{
    this->pos_last = ev->pos();
}
void MapWidget::mouseMoveEvent(QMouseEvent *ev)
{
    QPoint d = ev->pos() - this->pos_last;
    this->pos_last = ev->pos();
    pan(d);
    update();
}

void MapWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    drawTiles(p);
}

void MapWidget::drawTiles(QPainter &p)
{
    #ifndef Q_OS_WASM
    qDebug() << this->zoom;
    #endif
    
    const int tiles = 1 << zoom;
    
    double cx = lonToTileX(this->center_lon, this->zoom);
    double cy = latToTileY(this->center_lat, this->zoom);
    
    int w = width();
    int h = height();
    
    int tiles_x = w / this->tile_size + 2;
    int tiles_y = h / this->tile_size + 2;
    
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
            
            QString key = QString("%1/%2/%3").arg(zoom).arg(x).arg(y);
            
            if (!this->cache.contains(key))
            {
                requestTile(key, x, y);
            }
            
            if (this->cache.contains(key))
            {
                QPixmap *pix = this->cache.object(key);
                int px = int((x - cx) * this->tile_size + w / 2);
                int py = int((y - cy) * this->tile_size + h / 2);
                p.drawPixmap(px, py, *pix);
            }
        }
    }
}

void MapWidget::pan(const QPoint &d)
{
    double scale = 256.0 * (1 << zoom);
    this->center_lon -= d.x() / scale * 360.0;
    this->center_lat += d.y() / scale * 360.0;
    
    clampCenter();
}

void MapWidget::clampCenter()
{
    double cx = lonToTileX(center_lon, zoom);
    double cy = latToTileY(center_lat, zoom);
    
    double maxTile = (1 << zoom) - 1;
    
    double half_w = (width()  / double(tile_size)) / 2.0;
    double half_h = (height() / double(tile_size)) / 2.0;
    
    double min_cx = half_w;
    double max_cx = maxTile - half_w;
    
    double min_cy = half_h;
    double max_cy = maxTile - half_h;
    
    // --- CASE 1: Map smaller than screen horizontally ---
    if (min_cx > max_cx) {
        // exact center of world in tile coords
        cx = maxTile / 2.0;
    } else {
        cx = std::clamp(cx, min_cx, max_cx);
    }
    
    // --- CASE 2: Map smaller than screen vertically ---
    if (min_cy > max_cy) {
        cy = maxTile / 2.0;
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
    
    //QString endpoint = QString("/opentopomap/%1/%2/%3.png").arg(this->zoom).arg(x).arg(y);
    QString endpoint = QString("/arcgis/%1/%2/%3.png").arg(this->zoom).arg(x).arg(y);
    if (this->zoom > 17)
    {
        endpoint = QString("/openstreetmap/%1/%2/%3.png").arg(this->zoom).arg(x).arg(y);
    }
    
    this->rest->getTile(endpoint, key);
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
