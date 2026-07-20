#ifndef FOOTER_STATUSBAR_H
#define FOOTER_STATUSBAR_H

#include <QObject>
#include <QWidget>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QLabel>

#include "_enums_structs.h"
#include <aowis/model/gis.h>

class FooterStatusBar : public QWidget
{
public:
    FooterStatusBar(QWidget *parent = nullptr);
    
    void setMapZoom(int zoom);
    void setMapCoordinatesWGS84(const CoordinateWGS84 &wgs);
    void setMapCoordinatesUTM(const CoordinateUTM &utm);
    
    QStatusBar* statusBar() const { return bar; };
    
    void statusUpdateServerMap(StatusColorCode code);
    
private:
    QStatusBar *bar;
    
    QHBoxLayout *layout;
    
    QLabel *label_map_zoom;
    QLabel *label_map_coords_lat;
    QLabel *label_map_coords_lon;
    
    QLabel *label_map_coords_utm_easting;
    QLabel *label_map_coords_utm_northing;
    
    QLabel *label_indicator_map;
    QLabel *label_indicator_map_status;
    QLabel *label_indicator_server;
};

#endif // FOOTER_STATUSBAR_H
