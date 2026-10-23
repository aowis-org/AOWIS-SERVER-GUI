#pragma once

#include <QTabBar>

// Keep the five icon-only workspace tabs inside the map sidebar. Windows
// QStyle includes much more horizontal tab padding than the Linux styles,
// which otherwise expands the whole sidebar beyond its configured width.
class MapModeTabBar final : public QTabBar
{
public:
    explicit MapModeTabBar(QWidget *parent = nullptr) : QTabBar(parent) {}

protected:
    QSize tabSizeHint(int index) const override
    {
        QSize hint = QTabBar::tabSizeHint(index);
#ifdef Q_OS_WIN
        // Five 40px tabs + 6px selector margins fit even the 210px monitor sidebar.
        // Leave the height and native tab painting untouched.
        hint.setWidth(qMin(hint.width(), 40));
#endif
        return hint;
    }

    QSize minimumTabSizeHint(int index) const override
    {
        QSize hint = QTabBar::minimumTabSizeHint(index);
#ifdef Q_OS_WIN
        // Qt's tab layout consults both hints, so cap both consistently.
        hint.setWidth(qMin(hint.width(), 40));
#endif
        return hint;
    }
};
