#ifndef TAB_CUSTOMERS_WIDGET_H
#define TAB_CUSTOMERS_WIDGET_H

#include <QObject>
#include <QWidget>

class CustomersWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CustomersWidget(QWidget *parent = nullptr);

signals:
};

#endif // TAB_CUSTOMERS_WIDGET_H
