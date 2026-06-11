#include "map_widget.h"

MapWidget::MapWidget(QWidget *parent)
    : QWidget(parent),
    zoom(15),
    center_lon(18.20982),
    center_lat(11.98236),
    cache(2000)
{
    this->setMinimumHeight(500);
    this->setMinimumWidth(550);
}

void MapWidget::wheelEvent(QWheelEvent *ev)
{
    int delta = ev->angleDelta().y();
    this->zoom += (delta > 0 ? 1 : -1);
    this->zoom = std::clamp(zoom, 1, 19);
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
    qDebug() << "paint";
    
    QPainter p(this);
    drawTiles(p);
}

void MapWidget::drawTiles(QPainter &p)
{
    const int tile_size = 256;
    const int tiles = 1 << zoom;
    
    double cx = lonToTileX(this->center_lon, this->zoom);
    double cy = latToTileY(this->center_lat, this->zoom);
    
    int w = width();
    int h = height();
    
    int tiles_x = w / tile_size + 2;
    int tiles_y = h / tile_size + 2;
    
    int start_x = int(cx) - tiles_x / 2;
    int start_y = int(cy) - tiles_y / 2;
    
    qDebug() << "draw tiles";
    
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
                QImage *img = this->cache.object(key);
                int px = (x - cx) * tile_size + w / 2;
                int py = (y - cy) * tile_size + h / 2;
                p.drawImage(px, py, *img);
            }
        }
    }
}

void MapWidget::pan(const QPoint &d)
{
    double scale = 256.0 * (1 << zoom);
    center_lon -= d.x() / scale * 360.0;
    center_lat += d.y() / scale * 360.0;
}

void MapWidget::requestTile(const QString &key, int x, int y)
{
    qDebug() << "requesting tile";
    
    QString endpoint = QString("/osm/%1/%2/%3.png").arg(this->zoom).arg(x).arg(y);
    
    RESTClient *rest = new RESTClient("http://aowis-server-map.localhost:80", this);
    connect(rest, &RESTClient::requestFinished, this, [this, rest, key](const QByteArray &data)
        {
            QImage img;
            img.loadFromData(data);
            
            this->cache.insert(key, new QImage(img));
            
            rest->deleteLater();
            
            update();
        });
    connect(rest, &RESTClient::requestError, this, [this, rest](const QString &err)
        {
            qDebug() << "fail: " << err;
            
            rest->deleteLater();
        });
    rest->get(endpoint);
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
