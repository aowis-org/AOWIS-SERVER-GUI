#include "entity_inspector_widget.h"

#include <QGridLayout>
#include <QPixmap>

EntityInspectorWidget::EntityInspectorWidget(HydraulicData *hydraulic_data, QWidget *parent)
    : QWidget(parent),
    hydraulic_data(hydraulic_data),
    tabs(new QTabWidget(this)),
    layout_main(new QVBoxLayout(this)),
    label_title(new QLabel(this)),
    
    scroll_overview(new QScrollArea(this)),
    widget_overview(new QWidget()),
    layout_overview(new QVBoxLayout(this->widget_overview)),
    
    scroll_configuration(new QScrollArea(this)),
    widget_configuration(new QWidget()),
    layout_configuration(new QVBoxLayout(this->widget_configuration)),
    
    scroll_sim_meas(new QScrollArea(this)),
    widget_sim_meas(new QWidget()),
    layout_sim_meas(new QVBoxLayout(this->widget_sim_meas)),
    
    scroll_quality(new QScrollArea(this)),
    widget_quality(new QWidget()),
    layout_quality(new QVBoxLayout(this->widget_quality)),
    
    scroll_alerts(new QScrollArea(this)),
    widget_alerts(new QWidget()),
    layout_alerts(new QVBoxLayout(this->widget_alerts)),
    
    scroll_history(new QScrollArea(this)),
    widget_history(new QWidget()),
    layout_history(new QVBoxLayout(this->widget_history))
{
    this->scroll_overview->setWidgetResizable(true);
    this->scroll_overview->setWidget(this->widget_overview);
    
    this->scroll_configuration->setWidgetResizable(true);
    this->scroll_configuration->setWidget(this->widget_configuration);
    
    this->scroll_sim_meas->setWidgetResizable(true);
    this->scroll_sim_meas->setWidget(this->widget_sim_meas);
    
    this->scroll_quality->setWidgetResizable(true);
    this->scroll_quality->setWidget(this->widget_quality);
    
    this->scroll_alerts->setWidgetResizable(true);
    this->scroll_alerts->setWidget(this->widget_alerts);
    
    this->scroll_history->setWidgetResizable(true);
    this->scroll_history->setWidget(this->widget_history);
    
    this->tabs->setIconSize(QSize(40, 40));
    this->tabs->tabBar()->setStyleSheet(
        "QTabBar::tab"
        "{"
        "    max-width: 40px;"
        "    padding: 5px;"
        "}"
    );
    
    tabs->addTab(this->scroll_overview, QIcon(":/icon/inspector_dash.png"), "");
    this->tabs->setTabToolTip(this->tabs->count()-1, "Entity Overview");
    
    tabs->addTab(this->scroll_configuration, QIcon(":/icon/settings_2.png"), "");
    this->tabs->setTabToolTip(this->tabs->count()-1, "Configuration");
    
    tabs->addTab(this->scroll_sim_meas, QIcon(":/icon/sim_meas.png"), "");
    this->tabs->setTabToolTip(this->tabs->count()-1, "Simulation & Measurement");
    
    tabs->addTab(this->scroll_quality, QIcon(":/icon/inspector_quality.png"), "");
    this->tabs->setTabToolTip(this->tabs->count()-1, "Quality");
    
    tabs->addTab(this->scroll_alerts, QIcon(":/icon/alarm.png"), "");
    this->tabs->setTabToolTip(this->tabs->count()-1, "Alerts");
    
    tabs->addTab(this->scroll_history, QIcon(":/icon/history.png"), "");
    this->tabs->setTabToolTip(this->tabs->count()-1, "History");

    connect(this->tabs, &QTabWidget::currentChanged, this, &EntityInspectorWidget::signalCurrentTabChanged);
    
    this->layout_main->addWidget(this->label_title);
    this->layout_main->addWidget(this->tabs);
}

void EntityInspectorWidget::setCurrentTabIndex(int index)
{
    if (index < 0 || index >= this->tabs->count())
        return;

    this->tabs->setCurrentIndex(index);
}

QVBoxLayout *EntityInspectorWidget::layoutOverview()
{
    return this->layout_overview;
}
QVBoxLayout *EntityInspectorWidget::layoutConfiguration()
{
    return this->layout_configuration;
}
QVBoxLayout *EntityInspectorWidget::layoutSimMeas()
{
    return this->layout_sim_meas;
}
QVBoxLayout *EntityInspectorWidget::layoutQuality()
{
    return this->layout_quality;
}
QVBoxLayout *EntityInspectorWidget::layoutHistory()
{
    return this->layout_history;
}

void EntityInspectorWidget::setTitle(const QString &title)
{
    this->label_title->setText("<b>" + title.toHtmlEscaped() + "</b>");
}

void EntityInspectorWidget::addGroupOverviewImage(const QString &icon_path, const QString &name)
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("General");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *picture = new QLabel();
    QPixmap pixmap(icon_path);
    
    picture->setPixmap(pixmap.scaledToHeight(
        Sizes::SidebarRightImageHeight,
        Qt::SmoothTransformation
        ));
    picture->setAlignment(Qt::AlignCenter);
    
    grid->addWidget(picture, 0, 0, 1, 2);
    
    this->layoutOverview()->addWidget(group);
}

void EntityInspectorWidget::addGroupGeneral(const QString &name)
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("General");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_name = new QLabel("Name");
    this->line_name = new QLineEdit();
    this->line_name->setText(name);
    
    QLabel *label_date_install = new QLabel("Added Date");
    this->date_install = new QDateEdit();
    this->date_install->setCalendarPopup(true);
    this->date_install->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    this->date_install->setToolTip("yyyy-MM-dd");
    this->date_install->setDate(QDate::currentDate());
    
    QLabel *label_model_role = new QLabel("Model Role");
    this->combo_model_role = new QComboBox();
    this->combo_model_role->addItem("[Unspecified]", static_cast<int>(EntityModelRole::Unspecified));
    this->combo_model_role->addItem("Existing Asset", static_cast<int>(EntityModelRole::ExistingAsset));
    this->combo_model_role->addItem("Planned Asset", static_cast<int>(EntityModelRole::PlannedAsset));
    this->combo_model_role->addItem("Virtual / Model-Only", static_cast<int>(EntityModelRole::VirtualModelElement));
    this->combo_model_role->addItem("Boundary Condition", static_cast<int>(EntityModelRole::BoundaryCondition));
    this->combo_model_role->addItem("Temporary / Testing", static_cast<int>(EntityModelRole::TemporaryTesting));
    this->combo_model_role->addItem("Retired Asset", static_cast<int>(EntityModelRole::RetiredAsset));
    this->combo_model_role->setToolTip(
        "Describes whether this entity represents a real asset, a planned asset, "
        "a model-only helper, a boundary condition, or a temporary/testing element."
    );
    
    QCheckBox *check_enabled = new QCheckBox("Enabled");
    check_enabled->setChecked(true);
    
    grid->addWidget(label_name, 1, 0);
    grid->addWidget(this->line_name, 1, 1);
    
    grid->addWidget(label_model_role, 3, 0);
    grid->addWidget(this->combo_model_role, 3, 1);
    
    grid->addWidget(label_date_install, 4, 0);
    grid->addWidget(this->date_install, 4, 1);
    
    grid->addWidget(check_enabled, 5, 0);
    
    this->layoutConfiguration()->addWidget(group);
}

void EntityInspectorWidget::addGroupEndpoints()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Endpoints");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_node_1 = new QLabel("Node 1");
    QLabel *label_node_1_id = new QLabel();
    QPushButton *button_node_1_locate = new QPushButton(QIcon(":/icon/geomarker.png"), "");
    button_node_1_locate->setIconSize(QSize(20, 20));
    button_node_1_locate->setToolTip("Show on Map");
    button_node_1_locate->setMaximumWidth(35);
    QPushButton *button_node_1_inspect = new QPushButton(QIcon(":/icon/target.png"), "");
    button_node_1_inspect->setIconSize(QSize(20, 20));
    button_node_1_inspect->setToolTip("Inspect");
    button_node_1_inspect->setMaximumWidth(35);
    
    QLabel *label_node_2 = new QLabel("Node 2");
    QLabel *label_node_2_id = new QLabel();
    QPushButton *button_node_2_locate = new QPushButton(QIcon(":/icon/geomarker.png"), "");
    button_node_2_locate->setIconSize(QSize(20, 20));
    button_node_2_locate->setToolTip("Show on Map");
    button_node_2_locate->setMaximumWidth(35);
    QPushButton *button_node_2_inspect = new QPushButton(QIcon(":/icon/target.png"), "");
    button_node_2_inspect->setIconSize(QSize(20, 20));
    button_node_2_inspect->setToolTip("Inspect");
    button_node_2_inspect->setMaximumWidth(35);
    
    grid->addWidget(label_node_1, 0, 0);
    grid->addWidget(label_node_1_id, 0, 1);
    grid->addWidget(button_node_1_locate, 0, 2);
    grid->addWidget(button_node_1_inspect, 0, 3);
    
    grid->addWidget(label_node_2, 1, 0);
    grid->addWidget(label_node_2_id, 1, 1);
    grid->addWidget(button_node_2_locate, 1, 2);
    grid->addWidget(button_node_2_inspect, 1, 3);
    
    this->layoutConfiguration()->addWidget(group);
}

void EntityInspectorWidget::addGroupPosition()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Position");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_latitude = new QLabel("Latitude");
    this->spin_latitude = new QDoubleSpinBox;
    this->spin_latitude->setRange(-90.0, 90.0);
    this->spin_latitude->setDecimals(6);
    this->spin_latitude->setSingleStep(0.000001);
    this->spin_latitude->setSuffix(" °");
    this->spin_latitude->setAccelerated(true);
    
    QLabel *label_longitude = new QLabel("Longitude");
    this->spin_longitude = new QDoubleSpinBox;
    this->spin_longitude->setRange(-180.0, 180.0);
    this->spin_longitude->setDecimals(6);
    this->spin_longitude->setSingleStep(0.000001);
    this->spin_longitude->setSuffix(" °");
    this->spin_longitude->setAccelerated(true);
    
    QPushButton *button_find = new QPushButton("Find on Map");
    
    grid->addWidget(label_latitude, 0, 0);
    grid->addWidget(this->spin_latitude, 0, 1);
    grid->addWidget(label_longitude, 1, 0);
    grid->addWidget(this->spin_longitude, 1, 1);
    grid->addWidget(button_find, 2, 0, 1, 2);
    
    this->layoutConfiguration()->addWidget(group);
}

void EntityInspectorWidget::addGroupElevation()
{
    this->group_elevation = new GroupBoxCollapsible("Elevation");
    QGridLayout *grid = new QGridLayout(group_elevation);
    
    this->combo_elevation_mode = new QComboBox();
    this->combo_elevation_mode->addItem("Tank Bottom Elevation");
    this->combo_elevation_mode->addItem("Terrain Elevation + Offset");
    
    this->button_terrain_elevation = new QPushButton("Terrain Elevation from GIS");
    this->button_terrain_elevation->setToolTip(
        "Uses terrain elevation from GIS/DEM data.<br>Accuracy depends on the dataset and local terrain."
        );
    
    this->label_terrain_elevation = new QLabel("Terrain elevation");
    this->spin_terrain_elevation = new QDoubleSpinBox;
    this->spin_terrain_elevation->setRange(-10000.0, 10000.0);
    this->spin_terrain_elevation->setDecimals(3);
    this->spin_terrain_elevation->setSingleStep(0.10);
    this->spin_terrain_elevation->setSuffix(" m");
    
    this->label_tank_bottom_offset = new QLabel("Tank bottom offset");
    this->label_tank_bottom_offset->setWordWrap(true);
    this->spin_tank_bottom_offset = new QDoubleSpinBox;
    this->spin_tank_bottom_offset->setRange(-100.0, 200.0);
    this->spin_tank_bottom_offset->setDecimals(3);
    this->spin_tank_bottom_offset->setSingleStep(0.10);
    this->spin_tank_bottom_offset->setSuffix(" m");
    this->spin_tank_bottom_offset->setToolTip(
        "Tank bottom relative to terrain. Positive = above ground, negative = below ground."
        );
    
    this->label_tank_bottom_elevation = new QLabel("Tank bottom elevation");
    this->label_tank_bottom_elevation->setWordWrap(true);
    this->spin_tank_bottom_elevation = new QDoubleSpinBox;
    this->spin_tank_bottom_elevation->setRange(-10000.0, 10000.0);
    this->spin_tank_bottom_elevation->setDecimals(3);
    this->spin_tank_bottom_elevation->setSingleStep(0.10);
    this->spin_tank_bottom_elevation->setSuffix(" m");
    
    grid->addWidget(this->combo_elevation_mode, 0, 0, 1, 2);
    grid->addWidget(button_terrain_elevation, 1, 0, 1, 2);
    grid->addWidget(this->label_terrain_elevation, 2, 0);
    grid->addWidget(this->spin_terrain_elevation, 2, 1);
    grid->addWidget(this->label_tank_bottom_offset, 3, 0);
    grid->addWidget(this->spin_tank_bottom_offset, 3, 1);
    grid->addWidget(label_tank_bottom_elevation, 4, 0);
    grid->addWidget(this->spin_tank_bottom_elevation, 4, 1);
    
    connect(this->combo_elevation_mode, &QComboBox::currentIndexChanged, this, &EntityInspectorWidget::onElevationModeSignalChanged);
    
    this->combo_elevation_mode->setCurrentIndex(1);
    
    connect(this->spin_terrain_elevation, &QDoubleSpinBox::valueChanged, this, &EntityInspectorWidget::onElevationCalc);
    connect(this->spin_tank_bottom_offset, &QDoubleSpinBox::valueChanged, this, &EntityInspectorWidget::onElevationCalc);
    
    connect(group_elevation, &GroupBoxCollapsible::signalExpanded, this, &EntityInspectorWidget::onGroupExpand);
    
    this->layoutConfiguration()->addWidget(group_elevation);
}

void EntityInspectorWidget::onGroupExpand(GroupBoxCollapsible *group)
{
    if (group == this->group_elevation)
        this->onElevationModeSignalChanged(this->combo_elevation_mode->currentIndex());
}

void EntityInspectorWidget::onElevationModeSignalChanged(int index)
{
    switch (index)
    {
    case 0:
        this->button_terrain_elevation->hide();
        this->label_terrain_elevation->hide();
        this->spin_terrain_elevation->hide();
        this->label_tank_bottom_offset->hide();
        this->spin_tank_bottom_offset->hide();
        this->spin_tank_bottom_elevation->setReadOnly(false);
        this->spin_tank_bottom_elevation->setToolTip("");
        return;
    case 1:
        this->button_terrain_elevation->show();
        this->label_terrain_elevation->show();
        this->spin_terrain_elevation->show();
        this->label_tank_bottom_offset->show();
        this->spin_tank_bottom_offset->show();
        this->spin_tank_bottom_elevation->setReadOnly(true);
        this->spin_tank_bottom_elevation->setToolTip(
            "Calculated automatically from <i>Terrain Elevation</i> + <i>Offset</i>"
            );
        onElevationCalc();
        return;
    }
}

void EntityInspectorWidget::onElevationCalc()
{
    double ground = this->spin_terrain_elevation->value();
    double offset = this->spin_tank_bottom_offset->value();
    this->spin_tank_bottom_elevation->setValue(ground + offset);
}

void EntityInspectorWidget::onHeadlossFormulaChanged(HeadlossFormulas formulas)
{
    Q_UNUSED(formulas)
}

void EntityInspectorWidget::addGroupDemands()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Demands");
    QGridLayout *grid = new QGridLayout(group);
    
    QPushButton *button_editor = new QPushButton("Open Editor");
    connect(button_editor, &QPushButton::clicked, this, [this]
            {
                openDemandsEditor();
            });
    
    grid->addWidget(button_editor, 1, 0);
    
    this->layoutConfiguration()->addWidget(group);
}
void EntityInspectorWidget::openDemandsEditor()
{
    if (this->dialog_demands != nullptr)
    {
        this->dialog_demands->raise();
        this->dialog_demands->activateWindow();
        return;
    }
    
    this->dialog_demands = new QDialog(this);
    this->dialog_demands->setWindowTitle("Demands");
    this->dialog_demands->resize(700, 400);
    this->dialog_demands->setAttribute(Qt::WA_DeleteOnClose);
    
    QGridLayout *grid = new QGridLayout(this->dialog_demands);
    
    this->table_demands = new QTableWidget();
    this->table_demands->setColumnCount(5);
    this->table_demands->setHorizontalHeaderLabels(QStringList{"Demand", "Pattern", "Source / Method", "Note", ""});
    //this->table_demands->setAlternatingRowColors(true);
    /*
    this->table_demands->verticalHeader()->setVisible(false);
    this->table_demands->horizontalHeader()->setStretchLastSection(false);
    this->table_demands->setColumnWidth(0, 95);
    this->table_demands->setColumnWidth(1, 70);
    this->table_demands->setColumnWidth(2, 70);
    */
    this->table_demands->horizontalHeader()->setMinimumSectionSize(1);
    this->table_demands->setColumnWidth(3, 30);
    
    QPushButton *button_demand = new QPushButton("Add Demand");
    connect(button_demand, &QPushButton::clicked, this, [this]
            {
                int row = this->table_demands->rowCount();
                this->table_demands->insertRow(row);
                
                QDoubleSpinBox *spin_base_demand = new QDoubleSpinBox();
                
                spin_base_demand->setDecimals(3);
                spin_base_demand->setMinimum(0.0);
                spin_base_demand->setMaximum(100000.0);
                spin_base_demand->setSingleStep(0.1);
                spin_base_demand->setValue(0.0);
                spin_base_demand->setSuffix(QStringLiteral(" m³/h"));
                spin_base_demand->setAlignment(Qt::AlignRight);
                //spin_base_demand->setMaximumWidth(95);
                
                QComboBox *combo_demand = new QComboBox();
                combo_demand->addItem("Constant");
                combo_demand->addItem("RESIDENTIAL");
                //combo_demand->setMaximumWidth(70);
                
                QComboBox *combo_source = new QComboBox();
                combo_source->addItem("Manual Estimation");
                combo_source->addItem("Meter Data");
                combo_source->addItem("Scenario");
                
                QLineEdit *line_demand = new QLineEdit();
                line_demand->setPlaceholderText("");
                line_demand->setMinimumWidth(200);
                
                QPushButton *button_delete = new QPushButton(QIcon(":/icon/remove.png"), "");
                button_delete->setToolTip("Delete this entry");
                //button_delete->setMaximumWidth(30);
                
                this->table_demands->setCellWidget(row, 0, spin_base_demand);
                this->table_demands->setCellWidget(row, 1, combo_demand);
                this->table_demands->setCellWidget(row, 2, combo_source);
                this->table_demands->setCellWidget(row, 3, line_demand);
                this->table_demands->setCellWidget(row, 4, button_delete);
                
                this->table_demands->resizeColumnsToContents();
            });
    
    
    QPushButton *button_patterns = new QPushButton("Manage Patterns");
    
    grid->addWidget(this->table_demands, 0, 0, 1, 2);
    grid->addWidget(button_demand, 1, 0);
    
    grid->addWidget(button_patterns, 1, 1);
    
    connect(
        this->dialog_demands,
        &QObject::destroyed,
        this,
        [this]
        {
            this->dialog_demands = nullptr;
            this->table_demands = nullptr;
        }
        );
    
    this->dialog_demands->show();
}

void EntityInspectorWidget::addGroupHistory()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("History");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layoutHistory()->addWidget(group);
}

void EntityInspectorWidget::addStretches()
{
    this->layoutOverview()->addStretch();
    this->layoutConfiguration()->addStretch();
    this->layoutSimMeas()->addStretch();
    this->layoutQuality()->addStretch();
    this->layoutHistory()->addStretch();
}
