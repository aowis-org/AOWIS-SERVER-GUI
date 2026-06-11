#include "footer_statusbar.h"

FooterStatusBar::FooterStatusBar(QWidget *parent)
    : QWidget(parent),
    label_map_zoom( new QLabel ),
    label_map_coords_x( new QLabel ),
    label_map_coords_y( new QLabel ),
    label_indicator_map( new QLabel )
{
    this->bar = new QStatusBar(this);
    
    this->layout = new QHBoxLayout(this);
    this->layout->addWidget(this->bar);
    
    this->bar->addWidget(this->label_map_zoom);
    this->bar->addWidget(this->label_map_coords_x);
    this->bar->addWidget(this->label_map_coords_y);
    this->bar->addPermanentWidget(this->label_indicator_map);
    
    setCoordinates(100, 500);
    
    this->label_indicator_map->setText("testing Map Server connection ...");
}

void FooterStatusBar::setCoordinates(double x, double y)
{
    this->label_map_coords_x->setText("Map X: " + QString::number(x));
    this->label_map_coords_y->setText("Map Y: " + QString::number(y));
}


