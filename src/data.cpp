#include "data.h"

Data::Data(QObject *parent)
    : QObject{parent},
    database_gui(new DatabaseGui(this))
{
    connect(this->database_gui, &DatabaseGui::signalReady, this, &Data::getTest);
}

void Data::getTest()
{
    qDebug() << "FROM SQLITE:" << this->database_gui->getTestName();
}
