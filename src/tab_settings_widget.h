#ifndef TAB_SETTINGS_WIDGET_H
#define TAB_SETTINGS_WIDGET_H

#include <QObject>
#include <QWidget>

class SettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsWidget(QWidget *parent = nullptr);

signals:
};

#endif // TAB_SETTINGS_WIDGET_H
