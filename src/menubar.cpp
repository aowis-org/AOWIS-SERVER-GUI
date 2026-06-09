#include "menubar.h"

MenuBar::MenuBar()
{
    addFileMenu();
}

void MenuBar::addFileMenu()
{
    QMenu *menu = addMenu("File");
    
    QAction *action_export = new QAction(QIcon::fromTheme("document-save"), "Export");
    
    menu->addAction(action_export);
}
