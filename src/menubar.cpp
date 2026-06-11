#include "menubar.h"

MenuBar::MenuBar()
{
    addFileMenu();
    addMapMenu();
}

void MenuBar::addFileMenu()
{
    QMenu *menu = addMenu("File");
    
    QAction *action_export = new QAction(QIcon::fromTheme("document-save"), "Export");
    
    menu->addAction(action_export);
}

void MenuBar::addMapMenu()
{
    QMenu *menu = addMenu("Map");
    
    QAction *action_zoom_in = new QAction("Zoom In", this);
    connect(action_zoom_in, &QAction::triggered, this, [this]{ emit signalMapZoomIn(); });
    QAction *action_zoom_out = new QAction("Zoom Out", this);
    connect(action_zoom_out, &QAction::triggered, this, [this]{ emit signalMapZoomOut(); });
    
    QActionGroup *group_map_select = new QActionGroup(this);
    group_map_select->setExclusive(true);
    QAction *action_map_arcgissat = new QAction("ArcGIS SAT", this);
    connect(action_map_arcgissat, &QAction::triggered, this, [this]{
        emit signalMapChange(MapProvider::ArcGISSat); });
    QAction *action_map_openstreetmap = new QAction("OpenStreetMap", this);
    connect(action_map_openstreetmap, &QAction::triggered, this, [this]{
        emit signalMapChange(MapProvider::OpenStreetMap); });
    QAction *action_map_opentopomap = new QAction("OpenTopoMap", this);
    connect(action_map_opentopomap, &QAction::triggered, this, [this]{
        emit signalMapChange(MapProvider::OpenTopoMap);});
    
    action_map_arcgissat->setCheckable(true);
    action_map_arcgissat->setChecked(true);
    action_map_openstreetmap->setCheckable(true);
    action_map_opentopomap->setCheckable(true);
    
    group_map_select->addAction(action_map_arcgissat);
    group_map_select->addAction(action_map_openstreetmap);
    group_map_select->addAction(action_map_opentopomap);
    
    menu->addAction(action_zoom_in);
    menu->addAction(action_zoom_out);
    menu->addSeparator();
    menu->addAction(action_map_arcgissat);
    menu->addAction(action_map_opentopomap);
    menu->addAction(action_map_openstreetmap);
}


