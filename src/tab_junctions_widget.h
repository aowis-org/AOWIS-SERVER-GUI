#ifndef TAB_JUNCTIONS_WIDGET_H
#define TAB_JUNCTIONS_WIDGET_H

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

#endif // TAB_JUNCTIONS_WIDGET_H
