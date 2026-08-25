#ifndef MAP_RHI_HUD_WIDGET_H
#define MAP_RHI_HUD_WIDGET_H

#include "map_model.h"

#include <QWidget>

class GpsProvider;
class QGeoPositionInfo;
class QPaintEvent;

class MapRhiHudWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit MapRhiHudWidget(MapModel *map_model, GpsProvider *gps, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateGpsPosition(const QGeoPositionInfo &info);

    MapModel *map_model = nullptr;
    GpsProvider *gps = nullptr;
    CoordinateWGS84 gps_coordinate;
    bool has_gps_coordinate = false;
};

#endif // MAP_RHI_HUD_WIDGET_H
