#ifndef DATA_H
#define DATA_H

#include <QObject>

#include <QDebug>

#include "database_gui.h"

#include <aowis/model/hydraulic/simulation_request.h>

class Data : public QObject
{
    Q_OBJECT
public:
    explicit Data(QObject *parent = nullptr);
    
    void initializeTestDB();
    void getTest();
    
private:
    DatabaseGui *database_gui = nullptr;
    
private slots:
    void onDatabaseReady();
    
signals:
};

#endif // DATA_H
