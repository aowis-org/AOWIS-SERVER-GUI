#ifndef MAP_CONTROLS_H
#define MAP_CONTROLS_H

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include "../_enums_structs.h"

class MapControls : public QWidget
{
    Q_OBJECT
    
public:
    explicit MapControls(QWidget *parent = nullptr);
    
public slots:
    void setZoom(int zoom);
    void setProvider(MapProvider provider);
    
signals:
    void zoomInRequested();
    void zoomOutRequested();
    void providerChanged(MapProvider provider);
    
private:
    QPushButton *m_buttonZoomIn = nullptr;
    QPushButton *m_buttonZoomOut = nullptr;
    QLabel *m_labelZoom = nullptr;
    QComboBox *m_comboProvider = nullptr;
};

#endif // MAP_CONTROLS_H
