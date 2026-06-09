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
};

#endif // MENUBAR_H
