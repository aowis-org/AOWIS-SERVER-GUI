#ifndef TAB_SETTINGS_WIDGET_H
#define TAB_SETTINGS_WIDGET_H

#include "shortcut_registry.h"

#include <QHash>
#include <QWidget>

class QEvent;
class QLabel;
class QKeySequenceEdit;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class SettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsWidget(QWidget *parent = nullptr);

    void focusShortcut(GuiShortcutId id);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLineEdit *shortcut_search = nullptr;
    QLabel *shortcut_status = nullptr;
    QTreeWidget *shortcut_tree = nullptr;
    QPushButton *reset_all_shortcuts = nullptr;
    QHash<int, QTreeWidgetItem *> shortcut_items;
    QHash<int, QKeySequenceEdit *> shortcut_editors;

    void buildShortcutSettings();
    void refreshShortcutEditor(GuiShortcutId id);
    void refreshShortcutFilter();
    void validateShortcutEditor(GuiShortcutId id, const QKeySequence &sequence);
    void commitShortcutEditor(GuiShortcutId id);
    void showStatus(const QString &message, bool error);
};

#endif // TAB_SETTINGS_WIDGET_H
