#ifndef MENUBAR_H
#define MENUBAR_H

#include <QObject>

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>

#include "enums_structs.h"

class MenuBar : public QMenuBar
{
    Q_OBJECT
    
public:
    MenuBar();
    
private:
    void addFileMenu();
    void addMapMenu();
    
signals:
    void signalMapZoomIn();
    void signalMapZoomOut();
    void signalMapChange(MapProvider provider);
};

#endif // MENUBAR_H
