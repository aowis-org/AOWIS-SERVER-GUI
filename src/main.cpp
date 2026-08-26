#include <QApplication>
#include <QIcon>
#include "main_window.h"
#include "gui_configuration.h"

#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

#ifndef __EMSCRIPTEN__
    // Materialize the native configuration file immediately on application startup.
    (void)guiConfiguration();
#endif

    app.setWindowIcon(QIcon(":/icon/aowis_light_128.png"));
    
    MainWindow w;
    w.show();
    
    return app.exec();
}
