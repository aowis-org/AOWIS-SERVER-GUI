#ifndef CUSTOMER_POINTS_WIDGET_H
#define CUSTOMER_POINTS_WIDGET_H

#include <QObject>
#include <QWidget>

class CustomerPointsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CustomerPointsWidget(QWidget *parent = nullptr);

signals:
};

#endif // CUSTOMER_POINTS_WIDGET_H
