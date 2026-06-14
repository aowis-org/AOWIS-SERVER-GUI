#ifndef CUSTOMERS_WIDGET_H
#define CUSTOMERS_WIDGET_H

#include <QObject>
#include <QWidget>

class CustomersWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CustomersWidget(QWidget *parent = nullptr);

signals:
};

#endif // CUSTOMERS_WIDGET_H
