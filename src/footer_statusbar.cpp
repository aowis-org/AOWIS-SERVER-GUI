#include "footer_statusbar.h"

FooterStatusBar::FooterStatusBar(QWidget *parent)
    : QWidget(parent),
    label_map_zoom( new QLabel ),
    label_map_coords_lon( new QLabel ),
    label_map_coords_lat( new QLabel ),
    label_map_coords_crs_lon( new QLabel ),
    label_map_coords_crs_lat( new QLabel ),
    label_indicator_map( new QLabel ),
    label_indicator_map_status(new QLabel ),
    label_indicator_server( new QLabel )
{
    this->bar = new QStatusBar(this);
    
    this->layout = new QHBoxLayout(this);
    this->layout->addWidget(this->bar);
    
    this->label_map_zoom->setMinimumWidth(80);
    //this->label_map_coords_lat->setMinimumWidth(160);
    this->label_map_coords_lon->setMinimumWidth(180);
    
    this->label_map_coords_crs_lat->setMinimumWidth(100);
    this->label_map_coords_crs_lon->setMinimumWidth(100);
    
    this->bar->addWidget(this->label_map_zoom);
    this->bar->addWidget(this->label_map_coords_lat);
    this->bar->addWidget(this->label_map_coords_lon);
    
    this->bar->addPermanentWidget(this->label_indicator_map);
    this->bar->addPermanentWidget(this->label_indicator_server);
    //this->bar->addPermanentWidget(this->label_indicator_map_status);
    
    this->label_indicator_map->setText("AOWIS Map Server");
    this->label_indicator_server->setText("AOWIS Server");
    //this->label_indicator_map_status->setFixedSize(12, 12);
    //this->label_indicator_map_status->setStyleSheet("background-color: green; border-radius: 6px;");
}

void FooterStatusBar::setMapZoom(int zoom)
{
    this->label_map_zoom->setText("Zoom: " + QString::number(zoom));
}
void FooterStatusBar::setMapCoordinatesWGS84(CoordinateWGS84 wgs)
{
    // 5 decimals are about 1.11 m (depending on location)
    this->label_map_coords_lat->setText(
        "WGS84 Lat: " + QString::number(wgs.lat, 'f', 5)
        );
    
    this->label_map_coords_lon->setText(
        "Lon: " + QString::number(wgs.lon, 'f', 5)
        );
}
void FooterStatusBar::setMapCoordinatesCRS(double lon, double lat)
{
    this->label_map_coords_crs_lon->setText("UTM 34N Lon: " + QString::number(lat));
    this->label_map_coords_crs_lat->setText("Lat: " + QString::number(lon));
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

