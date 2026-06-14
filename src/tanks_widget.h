#ifndef TANKS_WIDGET_H
#define TANKS_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QVBoxLayout>
#include <QTableWidget>

class TanksWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TanksWidget(QWidget *parent = nullptr);
    
private:
    QVBoxLayout *layout;
    QTableWidget *table;
    
signals:
};

#endif // TANKS_WIDGET_H
