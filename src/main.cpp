#include <QApplication>
#include <QIcon>
#include "main_window.h"

#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    app.setWindowIcon(QIcon(":/img/favicon.png"));
    
    MainWindow w;
    w.show();
    
    return app.exec();
}
