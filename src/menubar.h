#ifndef MENUBAR_H
#define MENUBAR_H

#include <QObject>

#include <QMenuBar>
#include <QMenu>
#include <QAction>


class MenuBar : public QMenuBar
{
public:
    MenuBar();
    
private:
    void addFileMenu();
    void addMapMenu();
};

#endif // MENUBAR_H
