#include "map_network_canvas_widget.h"

MapNetworkCanvasWidget::MapNetworkCanvasWidget(MapModel *map_model, MapWidget *map, QWidget *parent)
    : QWidget{parent},
    map_model( map_model ),
    map( map )
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    
    setFocusPolicy(Qt::StrongFocus);
    
    
}

int MapNetworkCanvasWidget::backgroundOpacity() const
{
    return this->map_background_opacity;
}
void MapNetworkCanvasWidget::setBackgroundOpacity(int opacity)
{
    opacity = qBound(0, opacity, 100);
    
    if (this->map_background_opacity == opacity)
    {
        return;
    }
    
    this->map_background_opacity = opacity;
    update();
}

void MapNetworkCanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    
    if (this->map_background_opacity > 0)
    {
        QColor bg = palette().color(QPalette::Window);
        bg.setAlphaF(this->map_background_opacity / 100.0);
        
        p.fillRect(rect(), bg);
    }
    
    
    
}
