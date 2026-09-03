#ifndef TAB_LOGS_WIDGET_H
#define TAB_LOGS_WIDGET_H

#include <QObject>
#include <QWidget>

class LogsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LogsWidget(QWidget *parent = nullptr);

signals:
};

#endif // TAB_LOGS_WIDGET_H
