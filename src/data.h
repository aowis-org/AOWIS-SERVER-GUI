#ifndef DATA_H
#define DATA_H

#include <QObject>

// ChatGPT: we need to include here ../external/AOWIS-SERVER-DB/src/database_gui.h

class Data : public QObject
{
    Q_OBJECT
public:
    explicit Data(QObject *parent = nullptr);

signals:
};

#endif // DATA_H
