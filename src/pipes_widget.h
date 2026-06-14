#ifndef PIPES_WIDGET_H
#define PIPES_WIDGET_H

#include <QObject>
#include <QWidget>

class PipesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PipesWidget(QWidget *parent = nullptr);

signals:
};

#endif // PIPES_WIDGET_H
