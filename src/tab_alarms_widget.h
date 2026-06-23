#ifndef TAB_ALARMS_WIDGET_H
#define TAB_ALARMS_WIDGET_H

#include <QObject>
#include <QWidget>

class AlarmsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AlarmsWidget(QWidget *parent = nullptr);

signals:
};

#endif // TAB_ALARMS_WIDGET_H
