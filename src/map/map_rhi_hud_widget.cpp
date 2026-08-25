#include "map_rhi_hud_widget.h"

#include "map_scale_renderer.h"
#include "../gps_provider.h"

#include <QGeoCoordinate>
#include <QGeoPositionInfo>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>

MapRhiHudWidget::MapRhiHudWidget(MapModel *map_model, GpsProvider *gps, QWidget *parent)
    : QWidget(parent),
      map_model(map_model),
      gps(gps)
{
    Q_ASSERT(this->map_model != nullptr);

    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);

    connect(this->map_model, &MapModel::centerChangedWGS84, this, [this]
    {
        update();
    });
    connect(this->map_model, &MapModel::zoomChanged, this, [this]
    {
        update();
    });
    connect(this->map_model, &MapModel::view2dContinuousScaleChanged,
            this, [this](double)
    {
        update();
    });
    connect(this->map_model, &MapModel::viewModeChanged, this, [this](MapViewMode)
    {
        update();
    });
    connect(this->map_model, &MapModel::view3dCameraChanged, this, [this]
    {
        update();
    });

    if (this->gps != nullptr)
    {
        connect(this->gps, &GpsProvider::positionChanged,
                this, &MapRhiHudWidget::updateGpsPosition);
        connect(this->gps, &GpsProvider::gpsDisconnected, this, [this]
        {
            this->has_gps_coordinate = false;
            update();
        });
    }
}

void MapRhiHudWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setClipRegion(event->region());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (this->has_gps_coordinate)
    {
        const QPointF gps_point = this->map_model->screenFromWgs84(
            this->gps_coordinate, size());
        painter.setPen(Qt::black);
        painter.setBrush(Qt::red);
        painter.drawEllipse(gps_point, 5.0, 5.0);
    }

    static const QPixmap crosshair_pixmap =
        QPixmap(QStringLiteral(":/icon/crosshair.png")).scaled(
            QSize(60, 60), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!crosshair_pixmap.isNull())
    {
        const QPoint crosshair_position(
            (width() - crosshair_pixmap.width()) / 2,
            (height() - crosshair_pixmap.height()) / 2);
        painter.drawPixmap(crosshair_position, crosshair_pixmap);
    }

    if (this->map_model->viewMode() == MapViewMode::TwoD)
        MapScaleRenderer::draw(painter, *this->map_model, size());
}

void MapRhiHudWidget::updateGpsPosition(const QGeoPositionInfo &info)
{
    const QGeoCoordinate coordinate = info.coordinate();
    if (!info.isValid() || !coordinate.isValid())
    {
        this->has_gps_coordinate = false;
        update();
        return;
    }

    this->gps_coordinate.latitude_deg = coordinate.latitude();
    this->gps_coordinate.longitude_deg = coordinate.longitude();
    this->gps_coordinate.altitude_m = coordinate.altitude();
    this->has_gps_coordinate = true;
    update();
}
