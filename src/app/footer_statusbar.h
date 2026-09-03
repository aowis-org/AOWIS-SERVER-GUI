#ifndef FOOTER_STATUSBAR_H
#define FOOTER_STATUSBAR_H

#include <QObject>
#include <QWidget>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QLabel>
#include <QDoubleSpinBox>

#include "common/_enums_structs.h"
#include <aowis/model/gis.h>

class FooterStatusBar : public QWidget
{
    Q_OBJECT

public:
    FooterStatusBar(QWidget *parent = nullptr);
    
    // Updates the displayed zoom level. Ignored while the person has the
    // control focused/mid-edit, so a live camera update doesn't yank a
    // value out from under them while they're typing.
    void setMapZoom(double zoom);
    void setMapCoordinatesWGS84(const CoordinateWGS84 &wgs);
    void setMapCoordinatesUTM(const CoordinateUTM &utm);
    void clearMapCoordinates();
    
    QStatusBar* statusBar() const { return bar; };
    
    void statusUpdateServerMap(StatusColorCode code);

signals:
    // Fired when the person commits a new value (Enter, focus loss, or the
    // spin buttons/scroll) -- not on every keystroke while typing, see
    // setKeyboardTracking(false) in the constructor.
    void zoomEdited(double zoom);

private:
    QStatusBar *bar;
    
    QHBoxLayout *layout;
    
    QDoubleSpinBox *spin_map_zoom;
    QLabel *label_map_coords_lat;
    QLabel *label_map_coords_lon;
    
    QLabel *label_map_coords_utm_easting;
    QLabel *label_map_coords_utm_northing;
    
    QLabel *label_indicator_map;
    QLabel *label_indicator_map_status;
    QLabel *label_indicator_server;
};

#endif // FOOTER_STATUSBAR_H
