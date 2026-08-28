#include "footer_statusbar.h"

FooterStatusBar::FooterStatusBar(QWidget *parent)
    : QWidget(parent),
    label_map_zoom( new QLabel ),
    label_map_coords_lon( new QLabel ),
    label_map_coords_lat( new QLabel ),
    label_map_coords_utm_easting( new QLabel ),
    label_map_coords_utm_northing( new QLabel ),
    label_indicator_map( new QLabel ),
    label_indicator_map_status(new QLabel ),
    label_indicator_server( new QLabel )
{
    this->bar = new QStatusBar(this);
    
    this->layout = new QHBoxLayout(this);
    this->layout->addWidget(this->bar);
    
    this->label_map_zoom->setMinimumWidth(80);
    this->label_map_coords_lat->setMinimumWidth(135);
    this->label_map_coords_lon->setMinimumWidth(112);
    
    this->label_map_coords_utm_easting->setMinimumWidth(155);
    this->label_map_coords_utm_northing->setMinimumWidth(100);
    
    this->bar->addWidget(this->label_map_zoom);
    this->bar->addWidget(this->label_map_coords_lat);
    this->bar->addWidget(this->label_map_coords_lon);
    
    this->bar->addWidget(this->label_map_coords_utm_easting);
    this->bar->addWidget(this->label_map_coords_utm_northing);
    
    this->bar->addPermanentWidget(this->label_indicator_map);
    
    #ifdef AOWIS_STANDALONE
    this->label_indicator_map->setText("[AOWIS Controller Standalone Build]");
    #else
    this->label_indicator_map->setText("AOWIS Map Server");
    this->label_indicator_server->setText("AOWIS Server");
    this->bar->addPermanentWidget(this->label_indicator_server);
    #endif
    
    //this->label_indicator_map_status->setFixedSize(12, 12);
    //this->label_indicator_map_status->setStyleSheet("background-color: green; border-radius: 6px;");
}

void FooterStatusBar::setMapZoom(int zoom)
{
    this->label_map_zoom->setText("Zoom: " + QString::number(zoom));
}
void FooterStatusBar::setMapCoordinatesWGS84(const CoordinateWGS84 &wgs)
{
    // 5 decimals are about 1.11 m (depending on location)
    this->label_map_coords_lat->setText(
        "WGS 84 Lat: " + QString::number(wgs.latitude_deg, 'f', 5)
        );
    
    this->label_map_coords_lon->setText(
        "Lon: " + QString::number(wgs.longitude_deg, 'f', 5)
        );
}
void FooterStatusBar::setMapCoordinatesUTM(const CoordinateUTM &utm)
{
    const QString hemisphere = utm.hemisphere_northern ? "N" : "S";
    
    const QString crs = utm.isUPS()
                            ? "UPS " + hemisphere
                            : "UTM " + QString::number(utm.zone) + hemisphere;
    
    this->label_map_coords_utm_easting->setText(
        crs + " E: " + QString::number(utm.easting_m, 'f', 2) + " m"
        );
    
    this->label_map_coords_utm_northing->setText(
        "N: " + QString::number(utm.northing_m, 'f', 2) + " m"
        );
}

void FooterStatusBar::clearMapCoordinates()
{
    this->label_map_coords_lat->clear();
    this->label_map_coords_lon->clear();
    this->label_map_coords_utm_easting->clear();
    this->label_map_coords_utm_northing->clear();
}

void FooterStatusBar::statusUpdateServerMap(StatusColorCode code)
{
    QString style = "color: black;";
    switch (code)
    {
        case StatusColorCode::None:   style = "color: black";   break;
        case StatusColorCode::Green:  style = "color: green;";  break;
        case StatusColorCode::Yellow: style = "color: yellow;"; break;
        case StatusColorCode::Red:    style = "color: red;";    break;
    }
    
    this->label_indicator_map->setStyleSheet(style);
}

