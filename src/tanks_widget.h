#ifndef TANKS_WIDGET_H
#define TANKS_WIDGET_H

#include <QObject>
#include <QWidget>

class TanksWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TanksWidget(QWidget *parent = nullptr);

signals:
};

#endif // TANKS_WIDGET_H
