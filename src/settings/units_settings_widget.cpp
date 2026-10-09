#include "settings/units_settings_widget.h"
#include "config/gui_configuration.h"
#include <aowis/model/units/canonical_registry.h>
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

namespace {
struct UnitField { const char *group; const char *key; const char *choices; };
const UnitField fields[] = {
    {"Lengths and geometry", "length", "m;ft;km;mi"},
    {"Lengths and geometry", "elevation", "m;ft"},
    {"Lengths and geometry", "altitude", "m;ft"},
    {"Lengths and geometry", "distance", "m;ft;km;mi"},
    {"Lengths and geometry", "vertical_offset", "m;ft"},
    {"Lengths and geometry", "link_diameter", "mm;in;cm;m"},
    {"Lengths and geometry", "tank_diameter", "m;ft"},
    {"Lengths and geometry", "darcy_weisbach_roughness_height", "mm;in;millift"},
    {"Coordinates", "latitude", "deg"},
    {"Coordinates", "longitude", "deg"},
    {"Coordinates", "projected_easting", "m;ft"},
    {"Coordinates", "projected_northing", "m;ft"},
    {"Coordinates", "local_x", "m;ft"},
    {"Coordinates", "local_y", "m;ft"},
    {"Area and volume", "area", "m2;ft2;ha;acre"},
    {"Area and volume", "volume", "m3;L;ft3;US gal;Imp gal"},
    {"Flow and transport", "volumetric_flow_rate", "m3/h;L/s;L/min;ML/d;m3/d;m3/s;ft3/s;US gal/min;MUSgal/d;MImpgal/d;acre.ft/d"},
    {"Flow and transport", "velocity", "m/s;ft/s"},
    {"Flow and transport", "molecular_diffusivity", "m2/s;cm2/s"},
    {"Flow and transport", "longitudinal_dispersion_coefficient", "m2/s;cm2/s"},
    {"Hydraulics and pressure", "hydraulic_head", "m;ft"},
    {"Hydraulics and pressure", "pressure_head", "m;ft"},
    {"Hydraulics and pressure", "water_level", "m;ft"},
    {"Hydraulics and pressure", "head_gain", "m;ft"},
    {"Hydraulics and pressure", "head_loss", "m;ft"},
    {"Hydraulics and pressure", "head_loss_gradient", "m/km;ft/1000ft"},
    {"Hydraulics and pressure", "pressure", "kPa;Pa;bar;psi"},
    {"Hydraulics and pressure", "stress", "MPa;kPa;psi"},
    {"Time", "elapsed_time", "s;min;h;d"},
    {"Time", "duration", "s;min;h;d"},
    {"Time", "time_of_day", "s;min;h"},
    {"Dimensionless", "hazen_williams_roughness_coefficient", "1"},
    {"Dimensionless", "chezy_manning_roughness_coefficient", "1"},
    {"Dimensionless", "minor_loss_coefficient", "1"},
    {"Dimensionless", "darcy_weisbach_friction_factor", "1"},
    {"Dimensionless", "pump_speed_ratio", "1"},
    {"Dimensionless", "pattern_multiplier", "1"},
    {"Dimensionless", "demand_multiplier", "1"},
    {"Dimensionless", "pressure_exponent", "1"},
    {"Dimensionless", "emitter_exponent", "1"},
    {"Dimensionless", "reaction_order", "1"},
    {"Dimensionless", "specific_gravity", "1"},
    {"Dimensionless", "relative_viscosity", "1"},
    {"Dimensionless", "relative_diffusivity", "1"},
    {"Dimensionless", "peclet_number", "1"},
    {"Dimensionless", "mixing_fraction", "1"},
    {"Dimensionless", "hydraulic_accuracy", "1"},
    {"Dimensionless", "hydraulic_damping_limit", "1"},
    {"Dimensionless", "relative_error", "1"},
    {"Dimensionless", "flow_balance_ratio", "1"},
    {"Dimensionless", "quality_mass_balance_ratio", "1"},
    {"Percentages", "efficiency", "%;1"},
    {"Percentages", "relative_flow", "%;1"},
    {"Percentages", "valve_position", "%;1"},
    {"Percentages", "source_trace_percentage", "%;1"},
    {"Percentages", "operating_time_percentage", "%;1"},
    {"Percentages", "demand_reduction_percentage", "%;1"},
    {"Percentages", "leakage_loss_percentage", "%;1"},
    {"Reactions and leakage", "first_order_bulk_reaction_coefficient", "/d;/h;/s"},
    {"Reactions and leakage", "first_order_wall_reaction_coefficient", "m/d;ft/d"},
    {"Reactions and leakage", "leak_area_per_100m_pipe_length", "mm2/(100.m);in2/(100.ft)"},
    {"Reactions and leakage", "leak_area_expansion_per_pressure_head", "mm2/m;in2/ft"},
    {"Water quality", "chemical_mass_concentration", "mg/L;g/m3;ug/L"},
    {"Water quality", "chemical_amount_concentration", "mmol/L;mol/m3"},
    {"Water quality", "chemical_surface_mass_density", "mg/m2;g/m2"},
    {"Water quality", "chemical_surface_amount_density", "mmol/m2;mol/m2"},
    {"Water quality", "chemical_mass_flow_rate", "mg/min;g/min;g/h"},
    {"Water quality", "chemical_amount_flow_rate", "mmol/min;mol/min"},
    {"Water quality", "water_age", "h;d;min"},
    {"Power and electrical", "power", "kW;W;hp"},
    {"Power and electrical", "energy", "kW.h;J;MJ"},
    {"Power and electrical", "energy_intensity", "kW.h/m3;kW.h/ft3"},
    {"Power and electrical", "electric_current", "A;mA"},
    {"Power and electrical", "voltage", "V;mV;kV"},
    {"Power and electrical", "electrical_resistance", "Ohm;kOhm"},
    {"Power and electrical", "capacitance", "F;uF"},
};
const QStringList flowTemplates = {"CMH", "LPS", "LPM", "MLD", "CMD", "CMS", "CFS", "GPM", "MGD", "IMGD", "AFD"};
const QStringList flowUnits = {"m3/h", "L/s", "L/min", "ML/d", "m3/d", "m3/s", "ft3/s", "US gal/min", "MUSgal/d", "MImpgal/d", "acre.ft/d"};
QJsonObject canonicalUnits()
{
    QJsonObject units;
    for (const UnitField &field : fields) {
        const std::string_view unit = aowis::units::canonicalUnit(field.key);
        if (!unit.empty())
            units.insert(QString::fromLatin1(field.key), QString::fromLatin1(unit.data(), static_cast<qsizetype>(unit.size())));
    }
    return units;
}
QJsonObject templateUnits(int index)
{
    QJsonObject units = canonicalUnits();
    if (index >= 0 && index < flowUnits.size())
        units.insert(QStringLiteral("volumetric_flow_rate"), flowUnits.at(index));
    // EPANET US flow choices imply feet for geometric and hydraulic lengths.
    if (index >= 6) {
        for (const UnitField &field : fields) {
            const QString key = QString::fromLatin1(field.key);
            const QStringList choices = QString::fromLatin1(field.choices).split(';');
            if (choices.contains(QStringLiteral("ft"))) units.insert(key, QStringLiteral("ft"));
        }
        units.insert(QStringLiteral("link_diameter"), QStringLiteral("in"));
        units.insert(QStringLiteral("pressure"), QStringLiteral("psi"));
        units.insert(QStringLiteral("power"), QStringLiteral("hp"));
    }
    return units;
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
        if (index < 0 || index >= this->profiles.size() || this->profiles.at(index).builtin) return;

        const QString profile_name = this->profiles.at(index).name;
        QMessageBox *confirmation = new QMessageBox(
            QMessageBox::Question,
            QStringLiteral("Delete unit profile"),
            QStringLiteral("Delete the custom unit profile \"%1\"?\nThis cannot be undone.").arg(profile_name),
            QMessageBox::Yes | QMessageBox::No,
            this);
        confirmation->setAttribute(Qt::WA_DeleteOnClose);
        confirmation->setDefaultButton(QMessageBox::No);
        confirmation->setEscapeButton(QMessageBox::No);
        connect(confirmation, &QMessageBox::finished, this, [this, profile_name](int result) {
            if (result != QMessageBox::Yes) return;
            // Resolve the profile again: the selection or profile list may have changed
            // while this nonblocking dialog was open.
            for (int i = 0; i < this->profiles.size(); ++i) {
                if (this->profiles.at(i).builtin || this->profiles.at(i).name != profile_name) continue;
                const int selected = this->profile_selector->currentIndex();
                this->profiles.removeAt(i);
                this->refreshSelector(selected == i ? 0 : (selected > i ? selected - 1 : selected));
                this->save();
                break;
            }
        });
        confirmation->open();
    });
}

void UnitsSettingsWidget::load()
{
    this->profiles.append({QStringLiteral("AOWIS Canonical"), canonicalUnits(), true});
    for (int i = 0; i < flowTemplates.size(); ++i)
        this->profiles.append({flowTemplates.at(i), templateUnits(i), true});
    QSettings settings(guiConfigurationFilePath(), QSettings::IniFormat);
    const QJsonDocument document = QJsonDocument::fromJson(settings.value(QStringLiteral("units/custom_profiles")).toByteArray());
    if (document.isArray()) {
        for (const QJsonValue &value : document.array()) {
            const QJsonObject object = value.toObject();
            const QString name = object.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty()) continue;
            bool duplicate = false;
            for (const Profile &profile : this->profiles)
                if (profile.name.compare(name, Qt::CaseInsensitive) == 0) duplicate = true;
            if (duplicate) continue;
            QJsonObject units = canonicalUnits();
            const QJsonObject saved_units = object.value(QStringLiteral("units")).toObject();
            for (const UnitField &field : fields) {
                const QString key = QString::fromLatin1(field.key);
                const QString unit = saved_units.value(key).toString();
                if (QString::fromLatin1(field.choices).split(';').contains(unit)) units.insert(key, unit);
            }
            this->profiles.append({name, units, false});
        }
    }
    const QString selected = settings.value(QStringLiteral("units/selected_profile"), QStringLiteral("AOWIS Canonical")).toString();
    int selected_index = 0;
    for (int i = 0; i < this->profiles.size(); ++i)
        if (this->profiles.at(i).name == selected) selected_index = i;
    this->refreshSelector(selected_index);
}
void UnitsSettingsWidget::save() const
{
    QSettings settings(guiConfigurationFilePath(), QSettings::IniFormat);
    QJsonArray array;
    for (const Profile &profile : this->profiles)
        if (!profile.builtin) array.append(QJsonObject{{QStringLiteral("name"), profile.name}, {QStringLiteral("units"), profile.units}});
    settings.setValue(QStringLiteral("units/custom_profiles"), QJsonDocument(array).toJson(QJsonDocument::Compact));
    const int index = this->profile_selector->currentIndex();
    if (index >= 0 && index < this->profiles.size())
        settings.setValue(QStringLiteral("units/selected_profile"), this->profiles.at(index).name);
    settings.sync();
}
void UnitsSettingsWidget::refreshSelector(int selected)
{
    QSignalBlocker blocker(this->profile_selector);
    this->profile_selector->clear();
    for (const Profile &profile : this->profiles)
        this->profile_selector->addItem(profile.name);
    this->profile_selector->setCurrentIndex(selected);
    this->profile_button->setText(this->profiles.at(selected).name + QStringLiteral("  ▾"));
#ifdef Q_OS_WASM
    this->profile_menu->clear();
    const auto add_action = [this](WasmPopupMenu *menu, int index, const QString &text) {
        menu->addAction(text, [this, index] { this->profile_selector->setCurrentIndex(index); });
    };
    add_action(this->profile_menu, 0, this->profiles.at(0).name);
    this->profile_menu->addSeparator();
    add_action(this->profile_menu, 1, QStringLiteral("CMH — cubic meters per hour"));
    add_action(this->profile_menu, 2, QStringLiteral("LPS — liters per second"));
    WasmPopupMenu *other = this->profile_menu->addSubMenu(QStringLiteral("Other metric"));
    for (int i = 3; i <= 6; ++i) add_action(other, i, this->profiles.at(i).name);
    WasmPopupMenu *imperial = this->profile_menu->addSubMenu(QStringLiteral("Imperial / US"));
    for (int i = 7; i <= 11; ++i) add_action(imperial, i, this->profiles.at(i).name);
    WasmPopupMenu *custom = this->profile_menu->addSubMenu(QStringLiteral("Custom"));
    for (int i = 12; i < this->profiles.size(); ++i) add_action(custom, i, this->profiles.at(i).name);
#else
    this->profile_menu->clear();
    const auto add_action = [this](QMenu *menu, int index, const QString &text) {
        QAction *action = menu->addAction(text);
        action->setData(index);
        action->setCheckable(true);
        action->setChecked(index == this->profile_selector->currentIndex());
        connect(action, &QAction::triggered, this, [this, index] { this->profile_selector->setCurrentIndex(index); });
    };
    add_action(this->profile_menu, 0, this->profiles.at(0).name);
    this->profile_menu->addSeparator();
    add_action(this->profile_menu, 1, QStringLiteral("CMH — cubic meters per hour"));
    add_action(this->profile_menu, 2, QStringLiteral("LPS — liters per second"));
    QMenu *other = this->profile_menu->addMenu(QStringLiteral("Other metric"));
    for (int i = 3; i <= 6; ++i) add_action(other, i, this->profiles.at(i).name);
    QMenu *imperial = this->profile_menu->addMenu(QStringLiteral("Imperial / US"));
    for (int i = 7; i <= 11; ++i) add_action(imperial, i, this->profiles.at(i).name);
    this->profile_menu->addSeparator();
    QMenu *custom = this->profile_menu->addMenu(QStringLiteral("Custom"));
    for (int i = 12; i < this->profiles.size(); ++i) add_action(custom, i, this->profiles.at(i).name);
#endif
    this->refreshFields();
}
void UnitsSettingsWidget::refreshFields()
{
    this->fields_tree->clear();
    const int index = this->profile_selector->currentIndex();
    if (index < 0 || index >= this->profiles.size()) return;
    const bool builtin = this->profiles.at(index).builtin;
    this->delete_button->setEnabled(!builtin);
    this->rename_button->setEnabled(!builtin);
    this->profile_button->setText(this->profiles.at(index).name + QStringLiteral("  ▾"));
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
    for (const UnitField &field : fields) {
        const QString group = QString::fromLatin1(field.group);
        if (group != last_group) {
            category_item = new QTreeWidgetItem(this->fields_tree);
            category_item->setText(0, group);
            QFont category_font = category_item->font(0);
            category_font.setBold(true);
            category_item->setFont(0, category_font);
            category_item->setFirstColumnSpanned(true);
            last_group = group;
        }
        const QString key = QString::fromLatin1(field.key);
        QTreeWidgetItem *item = new QTreeWidgetItem(category_item);
        item->setText(0, key);
        item->setText(2, QString::fromLatin1(aowis::units::canonicalUnit(field.key).data(), static_cast<qsizetype>(aowis::units::canonicalUnit(field.key).size())));
        QComboBox *selector = new QComboBox(this->fields_tree);
        selector->addItems(QString::fromLatin1(field.choices).split(';'));
        const QString unit = this->profiles.at(index).units.value(key).toString();
        selector->setCurrentIndex(qMax(0, selector->findText(unit)));
        selector->setEnabled(!builtin && selector->count() > 1);
        selector->setToolTip(QStringLiteral("Canonical: %1").arg(QString::fromLatin1(aowis::units::canonicalUnit(field.key).data(), static_cast<qsizetype>(aowis::units::canonicalUnit(field.key).size()))));
        this->fields_tree->setItemWidget(item, 1, selector);
        connect(selector, &QComboBox::currentTextChanged, this, [this, index, key](const QString &text) {
            this->profiles[index].units.insert(key, text);
            this->save();
        });
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
    base_button->setText(this->profiles.at(source).name + QStringLiteral("  ▾"));
    QLineEdit *name_edit = new QLineEdit(dialog);
    const auto suggestedName = [this](int index) {
        return this->profiles.at(index).name + QStringLiteral(" (custom)");
    };
    name_edit->setText(suggestedName(source));
    const auto selectBase = [this, dialog, base_button, name_edit, suggestedName](int index) {
        const QString previousSuggestion = suggestedName(dialog->property("sourceIndex").toInt());
        const bool useSuggestion = name_edit->text() == previousSuggestion;
        dialog->setProperty("sourceIndex", index);
        base_button->setText(this->profiles.at(index).name + QStringLiteral("  ▾"));
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
    add_action(base_menu, 0, this->profiles.at(0).name);
    base_menu->addSeparator();
    add_action(base_menu, 1, QStringLiteral("CMH — cubic meters per hour"));
    add_action(base_menu, 2, QStringLiteral("LPS — liters per second"));
#ifdef Q_OS_WASM
    WasmPopupMenu *other = base_menu->addSubMenu(QStringLiteral("Other metric"));
    for (int i = 3; i <= 6; ++i) add_action(other, i, this->profiles.at(i).name);
    WasmPopupMenu *imperial = base_menu->addSubMenu(QStringLiteral("Imperial / US"));
    for (int i = 7; i <= 11; ++i) add_action(imperial, i, this->profiles.at(i).name);
    base_menu->addSeparator();
    WasmPopupMenu *custom = base_menu->addSubMenu(QStringLiteral("Custom"));
#else
    QMenu *other = base_menu->addMenu(QStringLiteral("Other metric"));
    for (int i = 3; i <= 6; ++i) add_action(other, i, this->profiles.at(i).name);
    QMenu *imperial = base_menu->addMenu(QStringLiteral("Imperial / US"));
    for (int i = 7; i <= 11; ++i) add_action(imperial, i, this->profiles.at(i).name);
    base_menu->addSeparator();
    QMenu *custom = base_menu->addMenu(QStringLiteral("Custom"));
#endif
    for (int i = 12; i < this->profiles.size(); ++i)
        add_action(custom, i, this->profiles.at(i).name);
    layout->addWidget(new QLabel(QStringLiteral("Profile name:"), dialog));
    layout->addWidget(name_edit);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, dialog, name_edit] {
    const int source = dialog->property("sourceIndex").toInt();
    if (source < 0 || source >= this->profiles.size()) return;
    const QString name = name_edit->text().trimmed();
    if (name.isEmpty()) return;
    for (const Profile &profile : this->profiles) {
        if (profile.name.compare(name, Qt::CaseInsensitive) == 0) {
            QMessageBox *message = new QMessageBox(QMessageBox::Warning, QStringLiteral("Unit profiles"),
                QStringLiteral("A profile with this name already exists."), QMessageBox::Ok, this);
            message->setAttribute(Qt::WA_DeleteOnClose);
            message->open();
            return;
        }
    }
    this->profiles.append({name, this->profiles.at(source).units, false});
    this->refreshSelector(this->profiles.size() - 1);
    this->save();
    });
    dialog->open();
}

void UnitsSettingsWidget::renameProfile()
{
    const int index = this->profile_selector->currentIndex();
    if (index < 0 || index >= this->profiles.size() || this->profiles.at(index).builtin) return;
    QInputDialog *dialog = new QInputDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Rename unit profile"));
    dialog->setLabelText(QStringLiteral("Profile name:"));
    dialog->setInputMode(QInputDialog::TextInput);
    dialog->setTextValue(this->profiles.at(index).name);
    connect(dialog, &QDialog::accepted, this, [this, dialog, index] {
        if (index >= this->profiles.size() || this->profiles.at(index).builtin) return;
        const QString name = dialog->textValue().trimmed();
        if (name.isEmpty() || name == this->profiles.at(index).name) return;
        for (int i = 0; i < this->profiles.size(); ++i) {
            if (i != index && this->profiles.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
                QMessageBox *message = new QMessageBox(QMessageBox::Warning, QStringLiteral("Unit profiles"),
                    QStringLiteral("A profile with this name already exists."), QMessageBox::Ok, this);
                message->setAttribute(Qt::WA_DeleteOnClose);
                message->open();
                return;
            }
        }
        this->profiles[index].name = name;
        this->refreshSelector(index);
        this->save();
    });
    dialog->open();
}
