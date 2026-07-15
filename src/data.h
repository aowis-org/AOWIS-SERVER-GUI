#ifndef DATA_H
#define DATA_H

#include <QObject>

#include <QDebug>

#include "database_gui.h"

class Data : public QObject
{
    Q_OBJECT
public:
    explicit Data(QObject *parent = nullptr);
    
    void getTest();
    
private:
    DatabaseGui *database_gui = nullptr;
    
signals:
};

#endif // DATA_H
