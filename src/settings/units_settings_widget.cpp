#include "settings/units_settings_widget.h"
#include "config/unit_profile_manager.h"
#include "config/unit_profile_notifications.h"
#include <aowis/model/units/unit_profile.h>
#include <QComboBox>
#include <QToolButton>
#include <QSizePolicy>
#include <QMenu>
#include <QAction>
#ifdef Q_OS_WASM
#include "widgets/wasm_popup_menu.h"
#endif
#include <QFormLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QPointer>
#include <QLineEdit>
#include <QFont>
#include <QLayoutItem>
#include <QStringList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QTimer>

namespace {
using UnitField = aowis::units::UnitField;
using Profile = UnitProfileManager::Profile;
QString asQString(std::string_view value);
constexpr const auto &fields = aowis::units::unit_fields;
const QStringList flowTemplates = [] {
    QStringList names;
    for (const std::string_view value : aowis::units::flow_template_names) names.append(asQString(value));
    return names;
}();
QString asQString(std::string_view value)
{
    return QString::fromLatin1(value.data(), static_cast<qsizetype>(value.size()));
}

}

UnitsSettingsWidget::UnitsSettingsWidget(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 20, 24, 20);
    QLabel *title = new QLabel(QStringLiteral("Unit Profiles"), this);
    QFont font = title->font(); font.setPointSize(font.pointSize() + 2); font.setBold(true);
    title->setFont(font);
    outer->addWidget(title);
    QLabel *info = new QLabel(QStringLiteral("Display-unit preferences only. Canonical model values are never changed. The toolbar unit selector and existing editors are not connected to these profiles yet."), this);
    info->setWordWrap(true);
    outer->addWidget(info);
    QHBoxLayout *controls = new QHBoxLayout;
    this->profile_selector = new QComboBox(this);
    this->profile_selector->hide();
    this->profile_button = new QToolButton(this);
    this->profile_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    this->profile_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    controls->addWidget(this->profile_button, 1);
#ifdef Q_OS_WASM
    this->profile_menu = new WasmPopupMenu(this->profile_button);
    connect(this->profile_button, &QToolButton::clicked, this, [this] {
        if (this->profile_menu->isPopupVisible()) this->profile_menu->closeAll();
        else this->profile_menu->popupBelow(this->profile_button);
    });
#else
    this->profile_button->setPopupMode(QToolButton::InstantPopup);
    this->profile_menu = new QMenu(this->profile_button);
    this->profile_button->setMenu(this->profile_menu);
#endif
    QPushButton *create_button = new QPushButton(QStringLiteral("New custom profile..."), this);
    controls->addWidget(create_button);
    this->rename_button = new QPushButton(QStringLiteral("Rename..."), this);
    controls->addWidget(this->rename_button);
    this->delete_button = new QPushButton(QStringLiteral("Delete"), this);
    controls->addWidget(this->delete_button);
    outer->addLayout(controls);
    this->field_search = new QLineEdit(this);
    this->field_search->setClearButtonEnabled(true);
    this->field_search->setPlaceholderText(QStringLiteral("Search quantities or units…"));
    outer->addWidget(this->field_search);

    this->fields_tree = new QTreeWidget(this);
    this->fields_tree->setColumnCount(3);
    this->fields_tree->setHeaderLabels({QStringLiteral("Quantity"), QStringLiteral("Display unit"),
                                        QStringLiteral("Canonical unit")});
    this->fields_tree->setRootIsDecorated(true);
    this->fields_tree->setAlternatingRowColors(true);
    this->fields_tree->setUniformRowHeights(false);
    this->fields_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    this->fields_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->fields_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    this->fields_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    this->fields_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    outer->addWidget(this->fields_tree, 1);
    connect(this->field_search, &QLineEdit::textChanged, this,
            [this](const QString &) { this->filterFields(); });
    this->load();
    connect(this->profile_selector, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { this->refreshFields(); this->save(); });
    connect(create_button, &QPushButton::clicked, this, [this] { this->createProfile(); });
    connect(this->rename_button, &QPushButton::clicked, this, [this] { this->renameProfile(); });
    connect(this->delete_button, &QPushButton::clicked, this, [this] {
        const int index = this->profile_selector->currentIndex();
        if (index < 0 || index >= UnitProfileManager::instance().profiles().size() || UnitProfileManager::instance().profiles().at(index).builtin) return;

        const QString profile_name = UnitProfileManager::instance().profiles().at(index).name;
        const QString profile_id = UnitProfileManager::instance().profiles().at(index).id;
        QMessageBox *confirmation = new QMessageBox(
            QMessageBox::Question,
            QStringLiteral("Delete unit profile"),
            QStringLiteral("Delete the custom unit profile \"%1\"?\nThis cannot be undone.").arg(profile_name),
            QMessageBox::Yes | QMessageBox::No,
            this);
        confirmation->setAttribute(Qt::WA_DeleteOnClose);
        confirmation->setDefaultButton(QMessageBox::No);
        confirmation->setEscapeButton(QMessageBox::No);
        connect(confirmation, &QMessageBox::finished, this, [this, profile_id](int result) {
            if (result != QMessageBox::Yes) return;
            // Resolve the profile again: the selection or profile list may have changed
            // while this nonblocking dialog was open.
            for (int i = 0; i < UnitProfileManager::instance().profiles().size(); ++i) {
                if (UnitProfileManager::instance().profiles().at(i).builtin || UnitProfileManager::instance().profiles().at(i).id != profile_id) continue;
                (void)UnitProfileManager::instance().remove(i, this);
                this->refreshSelector(UnitProfileManager::instance().activeIndex());
                break;
            }
        });
        confirmation->open();
    });
}

void UnitsSettingsWidget::load()
{
    this->refreshSelector(UnitProfileManager::instance().activeIndex());
    UnitProfileNotifications::instance().subscribe(this, [this] {
        QTimer::singleShot(0, this, [this] {
            this->refreshSelector(UnitProfileManager::instance().activeIndex());
        });
    });
}
void UnitsSettingsWidget::save() const
{
    (void)UnitProfileManager::instance().select(this->profile_selector->currentIndex(), const_cast<UnitsSettingsWidget *>(this));
}
void UnitsSettingsWidget::refreshSelector(int selected)
{
    QSignalBlocker blocker(this->profile_selector);
    this->profile_selector->clear();
    for (const Profile &profile : UnitProfileManager::instance().profiles())
        this->profile_selector->addItem(profile.name);
    this->profile_selector->setCurrentIndex(selected);
    this->profile_button->setText(UnitProfileManager::instance().profiles().at(selected).name + QStringLiteral("  ▾"));
#ifdef Q_OS_WASM
    this->profile_menu->clear();
    const auto add_action = [this](WasmPopupMenu *menu, int index, const QString &text) {
        menu->addAction(text, [this, index] { this->profile_selector->setCurrentIndex(index); });
    };
    add_action(this->profile_menu, 0, UnitProfileManager::instance().profiles().at(0).name);
    this->profile_menu->addSeparator();
    add_action(this->profile_menu, 1, QStringLiteral("CMH — cubic meters per hour"));
    add_action(this->profile_menu, 2, QStringLiteral("LPS — liters per second"));
    WasmPopupMenu *other = this->profile_menu->addSubMenu(QStringLiteral("Other metric"));
    for (int i = 3; i <= 6; ++i) add_action(other, i, UnitProfileManager::instance().profiles().at(i).name);
    WasmPopupMenu *imperial = this->profile_menu->addSubMenu(QStringLiteral("Imperial / US"));
    for (int i = 7; i <= 11; ++i) add_action(imperial, i, UnitProfileManager::instance().profiles().at(i).name);
    WasmPopupMenu *custom = this->profile_menu->addSubMenu(QStringLiteral("Custom"));
    for (int i = 12; i < UnitProfileManager::instance().profiles().size(); ++i) add_action(custom, i, UnitProfileManager::instance().profiles().at(i).name);
#else
    this->profile_menu->clear();
    const auto add_action = [this](QMenu *menu, int index, const QString &text) {
        QAction *action = menu->addAction(text);
        action->setData(index);
        action->setCheckable(true);
        action->setChecked(index == this->profile_selector->currentIndex());
        connect(action, &QAction::triggered, this, [this, index] { this->profile_selector->setCurrentIndex(index); });
    };
    add_action(this->profile_menu, 0, UnitProfileManager::instance().profiles().at(0).name);
    this->profile_menu->addSeparator();
    add_action(this->profile_menu, 1, QStringLiteral("CMH — cubic meters per hour"));
    add_action(this->profile_menu, 2, QStringLiteral("LPS — liters per second"));
    QMenu *other = this->profile_menu->addMenu(QStringLiteral("Other metric"));
    for (int i = 3; i <= 6; ++i) add_action(other, i, UnitProfileManager::instance().profiles().at(i).name);
    QMenu *imperial = this->profile_menu->addMenu(QStringLiteral("Imperial / US"));
    for (int i = 7; i <= 11; ++i) add_action(imperial, i, UnitProfileManager::instance().profiles().at(i).name);
    this->profile_menu->addSeparator();
    QMenu *custom = this->profile_menu->addMenu(QStringLiteral("Custom"));
    for (int i = 12; i < UnitProfileManager::instance().profiles().size(); ++i) add_action(custom, i, UnitProfileManager::instance().profiles().at(i).name);
#endif
    this->refreshFields();
}
void UnitsSettingsWidget::refreshFields()
{
    this->fields_tree->clear();
    const int index = this->profile_selector->currentIndex();
    if (index < 0 || index >= UnitProfileManager::instance().profiles().size()) return;
    const bool builtin = UnitProfileManager::instance().profiles().at(index).builtin;
    this->delete_button->setEnabled(!builtin);
    this->rename_button->setEnabled(!builtin);
    this->profile_button->setText(UnitProfileManager::instance().profiles().at(index).name + QStringLiteral("  ▾"));
    // Refresh checked menu entries without rebuilding menus while a selection is in progress.
#ifndef Q_OS_WASM
    const auto update_checks = [index](QMenu *menu, const auto &self) -> void {
        for (QAction *action : menu->actions()) {
            if (action->menu()) self(action->menu(), self);
            else if (action->isCheckable()) action->setChecked(action->data().toInt() == index);
        }
    };
    update_checks(this->profile_menu, update_checks);
#endif
    QString last_group;
    QTreeWidgetItem *category_item = nullptr;
    // Keep dimensionless quantities and percentages at the bottom of the view.
    // This changes presentation order only; the Model registry remains authoritative.
    for (int pass = 0; pass < 2; ++pass) {
        last_group.clear();
        category_item = nullptr;
        for (const UnitField &field : fields) {
            const bool bottom_group = field.group == "Dimensionless" || field.group == "Percentages";
            if (bottom_group != (pass == 1)) continue;
            const QString group = asQString(field.group);
            if (group != last_group) {
                category_item = new QTreeWidgetItem(this->fields_tree);
                category_item->setText(0, group);
                QFont category_font = category_item->font(0);
                category_font.setBold(true);
                category_item->setFont(0, category_font);
                category_item->setFirstColumnSpanned(true);
                last_group = group;
            }
            const QString key = asQString(field.key);
            QTreeWidgetItem *item = new QTreeWidgetItem(category_item);
            item->setText(0, key);
            item->setText(2, asQString(aowis::units::canonicalUnit(field.key)));
            QComboBox *selector = new QComboBox(this->fields_tree);
            selector->addItems(asQString(field.choices).split(';'));
            const QString unit = UnitProfileManager::instance().profiles().at(index).units.value(key).toString();
            selector->setCurrentIndex(qMax(0, selector->findText(unit)));
            selector->setEnabled(!builtin && selector->count() > 1);
            selector->setToolTip(QStringLiteral("Canonical: %1").arg(asQString(aowis::units::canonicalUnit(field.key))));
            this->fields_tree->setItemWidget(item, 1, selector);
            connect(selector, &QComboBox::currentTextChanged, this, [this, index, key](const QString &text) {
                if (!aowis::units::isAllowedUnit(key.toStdString(), text.toStdString())) return;
                (void)UnitProfileManager::instance().setUnit(index, key, text, this);
            });
        }
    }
    this->fields_tree->expandAll();
    this->filterFields();
}
void UnitsSettingsWidget::filterFields()
{
    const QString query = this->field_search->text().trimmed();
    for (int i = 0; i < this->fields_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *category = this->fields_tree->topLevelItem(i);
        const bool category_match = category->text(0).contains(query, Qt::CaseInsensitive);
        bool visible_child = false;
        for (int j = 0; j < category->childCount(); ++j) {
            QTreeWidgetItem *item = category->child(j);
            QComboBox *selector = qobject_cast<QComboBox *>(this->fields_tree->itemWidget(item, 1));
            QString available_units;
            if (selector != nullptr) {
                for (int k = 0; k < selector->count(); ++k)
                    available_units += selector->itemText(k) + QLatin1Char(' ');
            }
            const bool match = query.isEmpty() || category_match
                || item->text(0).contains(query, Qt::CaseInsensitive)
                || item->text(2).contains(query, Qt::CaseInsensitive)
                || available_units.contains(query, Qt::CaseInsensitive);
            item->setHidden(!match);
            visible_child |= match;
        }
        category->setHidden(!visible_child);
    }
}

void UnitsSettingsWidget::createProfile()
{
    QDialog *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("New unit profile"));
    dialog->setMinimumWidth(440);
    QVBoxLayout *layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(QStringLiteral("Start from profile:"), dialog));
    QToolButton *base_button = new QToolButton(dialog);
    base_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(base_button);
    dialog->setProperty("sourceIndex", qMax(0, this->profile_selector->currentIndex()));
    const int source = dialog->property("sourceIndex").toInt();
    base_button->setText(UnitProfileManager::instance().profiles().at(source).name + QStringLiteral("  ▾"));
    QLineEdit *name_edit = new QLineEdit(dialog);
    const auto suggestedName = [this](int index) {
        return UnitProfileManager::instance().profiles().at(index).name + QStringLiteral(" (custom)");
    };
    name_edit->setText(suggestedName(source));
    const auto selectBase = [this, dialog, base_button, name_edit, suggestedName](int index) {
        const QString previousSuggestion = suggestedName(dialog->property("sourceIndex").toInt());
        const bool useSuggestion = name_edit->text() == previousSuggestion;
        dialog->setProperty("sourceIndex", index);
        base_button->setText(UnitProfileManager::instance().profiles().at(index).name + QStringLiteral("  ▾"));
        if (useSuggestion) name_edit->setText(suggestedName(index));
    };
#ifdef Q_OS_WASM
    WasmPopupMenu *base_menu = new WasmPopupMenu(base_button);
    connect(base_button, &QToolButton::clicked, dialog, [base_button, base_menu] {
        if (base_menu->isPopupVisible()) base_menu->closeAll();
        else base_menu->popupBelow(base_button);
    });
    const auto add_action = [selectBase](WasmPopupMenu *menu, int index, const QString &text) {
        menu->addAction(text, [selectBase, index] {
            selectBase(index);
        });
    };
#else
    base_button->setPopupMode(QToolButton::InstantPopup);
    QMenu *base_menu = new QMenu(base_button);
    base_button->setMenu(base_menu);
    const auto add_action = [selectBase, dialog](QMenu *menu, int index, const QString &text) {
        QAction *action = menu->addAction(text);
        connect(action, &QAction::triggered, dialog, [selectBase, index] {
            selectBase(index);
        });
    };
#endif
    add_action(base_menu, 0, UnitProfileManager::instance().profiles().at(0).name);
    base_menu->addSeparator();
    add_action(base_menu, 1, QStringLiteral("CMH — cubic meters per hour"));
    add_action(base_menu, 2, QStringLiteral("LPS — liters per second"));
#ifdef Q_OS_WASM
    WasmPopupMenu *other = base_menu->addSubMenu(QStringLiteral("Other metric"));
    for (int i = 3; i <= 6; ++i) add_action(other, i, UnitProfileManager::instance().profiles().at(i).name);
    WasmPopupMenu *imperial = base_menu->addSubMenu(QStringLiteral("Imperial / US"));
    for (int i = 7; i <= 11; ++i) add_action(imperial, i, UnitProfileManager::instance().profiles().at(i).name);
    base_menu->addSeparator();
    WasmPopupMenu *custom = base_menu->addSubMenu(QStringLiteral("Custom"));
#else
    QMenu *other = base_menu->addMenu(QStringLiteral("Other metric"));
    for (int i = 3; i <= 6; ++i) add_action(other, i, UnitProfileManager::instance().profiles().at(i).name);
    QMenu *imperial = base_menu->addMenu(QStringLiteral("Imperial / US"));
    for (int i = 7; i <= 11; ++i) add_action(imperial, i, UnitProfileManager::instance().profiles().at(i).name);
    base_menu->addSeparator();
    QMenu *custom = base_menu->addMenu(QStringLiteral("Custom"));
#endif
    for (int i = 12; i < UnitProfileManager::instance().profiles().size(); ++i)
        add_action(custom, i, UnitProfileManager::instance().profiles().at(i).name);
    layout->addWidget(new QLabel(QStringLiteral("Profile name:"), dialog));
    layout->addWidget(name_edit);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, dialog, name_edit] {
    const int source = dialog->property("sourceIndex").toInt();
    if (source < 0 || source >= UnitProfileManager::instance().profiles().size()) return;
    const QString name = name_edit->text().trimmed();
    if (name.isEmpty()) return;
    for (const Profile &profile : UnitProfileManager::instance().profiles()) {
        if (profile.name.compare(name, Qt::CaseInsensitive) == 0) {
            QMessageBox *message = new QMessageBox(QMessageBox::Warning, QStringLiteral("Unit profiles"),
                QStringLiteral("A profile with this name already exists."), QMessageBox::Ok, this);
            message->setAttribute(Qt::WA_DeleteOnClose);
            message->open();
            return;
        }
    }
    if (UnitProfileManager::instance().create(source, name, this))
        this->refreshSelector(UnitProfileManager::instance().activeIndex());
    });
    dialog->open();
}

void UnitsSettingsWidget::renameProfile()
{
    const int index = this->profile_selector->currentIndex();
    if (index < 0 || index >= UnitProfileManager::instance().profiles().size() || UnitProfileManager::instance().profiles().at(index).builtin) return;
    QInputDialog *dialog = new QInputDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Rename unit profile"));
    dialog->setLabelText(QStringLiteral("Profile name:"));
    dialog->setInputMode(QInputDialog::TextInput);
    dialog->setTextValue(UnitProfileManager::instance().profiles().at(index).name);
    connect(dialog, &QDialog::accepted, this, [this, dialog, index] {
        if (index >= UnitProfileManager::instance().profiles().size() || UnitProfileManager::instance().profiles().at(index).builtin) return;
        const QString name = dialog->textValue().trimmed();
        if (name.isEmpty() || name == UnitProfileManager::instance().profiles().at(index).name) return;
        for (int i = 0; i < UnitProfileManager::instance().profiles().size(); ++i) {
            if (i != index && UnitProfileManager::instance().profiles().at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
                QMessageBox *message = new QMessageBox(QMessageBox::Warning, QStringLiteral("Unit profiles"),
                    QStringLiteral("A profile with this name already exists."), QMessageBox::Ok, this);
                message->setAttribute(Qt::WA_DeleteOnClose);
                message->open();
                return;
            }
        }
        if (UnitProfileManager::instance().rename(index, name, this))
            this->refreshSelector(index);
    });
    dialog->open();
}
