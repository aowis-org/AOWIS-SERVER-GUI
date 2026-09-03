#include "tab_settings_widget.h"

#include "keyboard_shortcuts_settings_widget.h"
#include "map_settings_widget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QStackedWidget>

namespace
{
constexpr int TableOfContentsWidth = 200;
}

SettingsWidget::SettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    this->table_of_contents = new QListWidget(this);
    this->table_of_contents->setFixedWidth(TableOfContentsWidth);
    this->table_of_contents->setFrameShape(QFrame::NoFrame);
    layout->addWidget(this->table_of_contents);

    this->pages = new QStackedWidget(this);
    layout->addWidget(this->pages, 1);

    this->keyboard_shortcuts_page = new KeyboardShortcutsSettingsWidget(this->pages);
    this->table_of_contents->addItem(QStringLiteral("Keyboard Shortcuts"));
    this->pages->addWidget(this->keyboard_shortcuts_page);

    this->map_settings_page = new MapSettingsWidget(this->pages);
    this->table_of_contents->addItem(QStringLiteral("Map Settings"));
    this->pages->addWidget(this->map_settings_page);

    connect(this->table_of_contents, &QListWidget::currentRowChanged,
            this, [this](int row)
    {
        if (row >= 0)
            this->pages->setCurrentIndex(row);
    });

    this->table_of_contents->setCurrentRow(0);
}

void SettingsWidget::focusShortcut(GuiShortcutId id)
{
    this->table_of_contents->setCurrentRow(0);
    this->keyboard_shortcuts_page->focusShortcut(id);
}
