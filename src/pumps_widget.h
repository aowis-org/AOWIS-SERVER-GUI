#ifndef PUMPS_WIDGET_H
#define PUMPS_WIDGET_H

#include <QObject>
#include <QWidget>

class PumpsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PumpsWidget(QWidget *parent = nullptr);

signals:
};

#endif // PUMPS_WIDGET_H
