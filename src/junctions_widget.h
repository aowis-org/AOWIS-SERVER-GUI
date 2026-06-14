#ifndef JUNCTIONS_WIDGET_H
#define JUNCTIONS_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QVBoxLayout>
#include <QTableWidget>

class JunctionsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit JunctionsWidget(QWidget *parent = nullptr);
    
private:
    QVBoxLayout *layout;
    QTableWidget *table;
    
signals:
};

#endif // JUNCTIONS_WIDGET_H
