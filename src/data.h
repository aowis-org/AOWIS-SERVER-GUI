#ifndef DATA_H
#define DATA_H

#include <QObject>

#include <QDebug>

#include "database_gui.h"

#include <aowis/model/project.h>
#include <aowis/model/hydraulic/network.h>

class Data : public QObject
{
    Q_OBJECT
public:
    explicit Data(QObject *parent = nullptr);
    
    void getProject();
    
private:
    DatabaseGui *database_gui = nullptr;
    
    std::optional<Project> project;
    
private slots:
    void onDatabaseReady();
    
signals:
};

#endif // DATA_H
