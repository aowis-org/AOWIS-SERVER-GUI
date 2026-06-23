#include <QApplication>
#include <QIcon>
#include "main_window.h"

#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    app.setWindowIcon(QIcon(":/icon/aowis_light_128.png"));
    
    MainWindow w;
    w.show();
    
    return app.exec();
}
