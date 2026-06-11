#ifndef FOOTER_STATUSBAR_H
#define FOOTER_STATUSBAR_H

#include <QObject>
#include <QWidget>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QLabel>

class FooterStatusBar : public QWidget
{
public:
    FooterStatusBar(QWidget *parent = nullptr);
    
    void setCoordinates(double x, double y);
    
    QStatusBar* statusBar() const { return bar; };
    
private:
    QStatusBar *bar;
    
    QHBoxLayout *layout;
    
    QLabel *label_map_zoom;
    QLabel *label_map_coords_x;
    QLabel *label_map_coords_y;
    
    QLabel *label_indicator_map_text;
    QLabel *label_indicator_map_status;
};

#endif // FOOTER_STATUSBAR_H
