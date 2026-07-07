#include "entity_inspector_widget.h"

#include <QGridLayout>
#include <QPixmap>

EntityInspectorWidget::EntityInspectorWidget(QWidget *parent)
    : QWidget(parent),
    layout_main(new QVBoxLayout(this)),
    label_title(new QLabel(this))
{
    this->layout_main->addWidget(this->label_title);
}

QVBoxLayout *EntityInspectorWidget::mainLayout() const
{
    return this->layout_main;
}

void EntityInspectorWidget::setTitle(const QString &title)
{
    this->label_title->setText("<b>" + title.toHtmlEscaped() + "</b>");
}

void EntityInspectorWidget::addGroupGeneral(const QString &icon_path, const QString &name)
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
    
    grid->addWidget(picture, 0, 0, 1, 2);
    
    grid->addWidget(label_name, 1, 0);
    grid->addWidget(this->line_name, 1, 1);
    
    grid->addWidget(label_model_role, 3, 0);
    grid->addWidget(this->combo_model_role, 3, 1);
    
    grid->addWidget(label_date_install, 4, 0);
    grid->addWidget(this->date_install, 4, 1);
    
    grid->addWidget(check_enabled, 5, 0);
    
    this->layout_main->addWidget(group);
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
    
    mainLayout()->addWidget(group);
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
    
    this->layout_main->addWidget(group);
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
    
    this->layout_main->addWidget(group_elevation);
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

void EntityInspectorWidget::addGroupHistory()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("History");
    QGridLayout *grid = new QGridLayout(group);
    
    
    
    this->layout_main->addWidget(group);
}
