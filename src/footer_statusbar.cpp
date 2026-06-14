#include "footer_statusbar.h"

FooterStatusBar::FooterStatusBar(QWidget *parent)
    : QWidget(parent),
    label_map_zoom( new QLabel ),
    label_map_coords_x( new QLabel ),
    label_map_coords_y( new QLabel ),
    label_indicator_map_text( new QLabel ),
    label_indicator_map_status(new QLabel )
{
    this->bar = new QStatusBar(this);
    
    this->layout = new QHBoxLayout(this);
    this->layout->addWidget(this->bar);
    
    this->bar->addWidget(this->label_map_zoom);
    this->bar->addWidget(this->label_map_coords_x);
    this->bar->addWidget(this->label_map_coords_y);
    this->bar->addPermanentWidget(this->label_indicator_map_text);
    this->bar->addPermanentWidget(this->label_indicator_map_status);
    
    this->label_indicator_map_text->setText("Map Server");
    //this->label_indicator_map_status->setFixedSize(12, 12);
    //this->label_indicator_map_status->setStyleSheet("background-color: green; border-radius: 6px;");
}

void FooterStatusBar::setMapZoom(int zoom)
{
    this->label_map_zoom->setText("Zoom: " + QString::number(zoom));
}
void FooterStatusBar::setMapCoordinates(double lon, double lat)
{
    this->label_map_coords_y->setText("Lon: " + QString::number(lon));
    this->label_map_coords_x->setText("Lat: " + QString::number(lat));
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
    
    this->label_indicator_map_text->setStyleSheet(style);
}

