#pragma once

#include <QIcon>
#include <QString>
#include <QWidget>

class MainNavigationBar;
class QStackedLayout;

class MainNavigationWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Placement
    {
        Top,
        Bottom
    };

    explicit MainNavigationWidget(QWidget *parent = nullptr);

    int addPage(QWidget *page,
                const QIcon &icon,
                const QString &tool_tip,
                Placement placement = Placement::Top);

    int count() const;
    int currentIndex() const;
    QWidget *currentWidget() const;
    void setCurrentIndex(int index);

signals:
    void currentChanged(int index);

private:
    MainNavigationBar *navigation_bar = nullptr;
    QWidget *page_host = nullptr;
    QStackedLayout *page_layout = nullptr;
};
