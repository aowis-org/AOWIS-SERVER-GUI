#include "data.h"

Data::Data(QObject *parent)
    : QObject{parent},
    database_gui(new DatabaseGui(this))
{
    
}

void Data::getTest()
{
    qDebug() << "FROM SQLITE: " << this->database_gui->getTestName();
}
