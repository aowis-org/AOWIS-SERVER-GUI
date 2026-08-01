#include "entity_inspector_widget.h"

#include <QGridLayout>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTimer>

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

    const QDate date_unset(100, 1, 1);

    QLabel *label_date_added = new QLabel("Date Added");
    this->date_added = new QDateEdit();
    this->date_added->setCalendarPopup(true);
    this->date_added->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    this->date_added->setToolTip("Date the entity was added to the model. yyyy-MM-dd");
    this->date_added->setMinimumDate(date_unset);
    this->date_added->setSpecialValueText("[Not set]");
    this->date_added->setDate(date_unset);

    QLabel *label_date_installed = new QLabel("Installation Date");
    this->date_installed = new QDateEdit();
    this->date_installed->setCalendarPopup(true);
    this->date_installed->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    this->date_installed->setToolTip("Date the physical asset was installed. yyyy-MM-dd");
    this->date_installed->setMinimumDate(date_unset);
    this->date_installed->setSpecialValueText("[Not set]");
    this->date_installed->setDate(date_unset);

    this->check_enabled = new QCheckBox("Enabled");
    this->check_enabled->setChecked(true);

    grid->addWidget(label_name, 0, 0);
    grid->addWidget(this->line_name, 0, 1);
    grid->addWidget(label_model_role, 1, 0);
    grid->addWidget(this->combo_model_role, 1, 1);
    grid->addWidget(label_date_added, 2, 0);
    grid->addWidget(this->date_added, 2, 1);
    grid->addWidget(label_date_installed, 3, 0);
    grid->addWidget(this->date_installed, 3, 1);
    grid->addWidget(this->check_enabled, 4, 0, 1, 2);

    layoutConfiguration()->addWidget(group);
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
    
    this->button_find_on_map = new QPushButton("Find on Map");
    
    grid->addWidget(label_latitude, 0, 0);
    grid->addWidget(this->spin_latitude, 0, 1);
    grid->addWidget(label_longitude, 1, 0);
    grid->addWidget(this->spin_longitude, 1, 1);
    grid->addWidget(this->button_find_on_map, 2, 0, 1, 2);
    
    layoutConfiguration()->addWidget(group);
}

void EntityInspectorWidget::bindHydraulicNode(InfrastructureEntity entity_type, const QUuid &uuid,
                                               const QString &title_prefix)
{
    this->entity_type = entity_type;
    this->entity_uuid = uuid;
    this->entity_title_prefix = title_prefix;

    refreshHydraulicNode();

    connect(this->line_name, &QLineEdit::textEdited, this, [this](const QString &id)
    {
        this->hydraulic_data->setNodeId(this->entity_uuid, id);
    });
    connect(this->combo_model_role, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const EntityModelRole model_role = static_cast<EntityModelRole>(
            this->combo_model_role->currentData().toInt());
        this->hydraulic_data->setNodeModelRole(this->entity_uuid, model_role);
    });
    connect(this->date_added, &QDateEdit::dateChanged, this, [this](const QDate &)
    {
        this->hydraulic_data->setNodeDateAdded(this->entity_uuid, optionalDate(this->date_added));
    });
    connect(this->date_installed, &QDateEdit::dateChanged, this, [this](const QDate &)
    {
        this->hydraulic_data->setNodeDateInstalled(
            this->entity_uuid, optionalDate(this->date_installed));
    });
    connect(this->check_enabled, &QCheckBox::toggled, this, [this](bool enabled)
    {
        this->hydraulic_data->setNodeEnabled(this->entity_uuid, enabled);
    });
    connect(this->spin_latitude, &QDoubleSpinBox::valueChanged, this, [this](double)
    {
        CoordinateWGS84 coordinate;
        coordinate.latitude_deg = this->spin_latitude->value();
        coordinate.longitude_deg = this->spin_longitude->value();
        this->hydraulic_data->setNodeCoordinate(this->entity_uuid, coordinate);
    });
    connect(this->spin_longitude, &QDoubleSpinBox::valueChanged, this, [this](double)
    {
        CoordinateWGS84 coordinate;
        coordinate.latitude_deg = this->spin_latitude->value();
        coordinate.longitude_deg = this->spin_longitude->value();
        this->hydraulic_data->setNodeCoordinate(this->entity_uuid, coordinate);
    });
    connect(this->button_find_on_map, &QPushButton::clicked, this, [this]()
    {
        this->hydraulic_data->requestNodeLocate(this->entity_type, this->entity_uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this, [this](InfrastructureEntity entity_type_changed, const QUuid &uuid_changed)
    {
        if (entity_type_changed == this->entity_type && uuid_changed == this->entity_uuid)
        {
            refreshHydraulicNode();
            scheduleJunctionDemandsRefresh();
        }
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, [this]()
    {
        refreshHydraulicNode();
        scheduleJunctionDemandsRefresh();
    });
}

void EntityInspectorWidget::refreshHydraulicNode()
{
    if (!this->hydraulic_data || this->entity_uuid.isNull())
        return;

    const std::optional<HydraulicNodeCommonData> node =
        this->hydraulic_data->nodeCommonData(this->entity_type, this->entity_uuid);
    if (!node.has_value())
        return;

    const QSignalBlocker name_blocker(this->line_name);
    const QSignalBlocker model_role_blocker(this->combo_model_role);
    const QSignalBlocker date_added_blocker(this->date_added);
    const QSignalBlocker date_installed_blocker(this->date_installed);
    const QSignalBlocker enabled_blocker(this->check_enabled);
    const QSignalBlocker latitude_blocker(this->spin_latitude);
    const QSignalBlocker longitude_blocker(this->spin_longitude);

    this->line_name->setText(node->id);
    const int model_role_index = this->combo_model_role->findData(
        static_cast<int>(node->metadata.model_role));
    this->combo_model_role->setCurrentIndex(model_role_index >= 0 ? model_role_index : 0);
    setOptionalDate(this->date_added, node->metadata.date_added);
    setOptionalDate(this->date_installed, node->metadata.date_installed);
    this->check_enabled->setChecked(node->metadata.enabled);
    this->spin_latitude->setValue(node->coordinate_wgs84.latitude_deg);
    this->spin_longitude->setValue(node->coordinate_wgs84.longitude_deg);
    setTitle(this->entity_title_prefix + " " + node->id);

    refreshHydraulicNodeElevation();
}

std::optional<QDate> EntityInspectorWidget::optionalDate(const QDateEdit *date_edit) const
{
    if (!date_edit || date_edit->date() == date_edit->minimumDate())
        return std::nullopt;

    return date_edit->date();
}

void EntityInspectorWidget::setOptionalDate(QDateEdit *date_edit, const std::optional<QDate> &date)
{
    if (!date_edit)
        return;

    date_edit->setDate(date.has_value() ? date.value() : date_edit->minimumDate());
}

void EntityInspectorWidget::addGroupElevation()
{
    QString group_title;
    QString absolute_input_text;
    QString offset_label_text;
    QString value_label_text;
    QString offset_tooltip;

    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
        group_title = "Elevation";
        absolute_input_text = "Total Elevation";
        offset_label_text = "Elevation Offset";
        value_label_text = "Total Elevation";
        offset_tooltip =
            "Distance from <i>Terrain Elevation</i>.<br>Positive: Above Ground.<br>Negative: Below Ground.";
        break;
    case InfrastructureEntity::Reservoir:
        group_title = "Head";
        absolute_input_text = "Total Head";
        offset_label_text = "Head Offset";
        value_label_text = "Total Head";
        offset_tooltip =
            "Hydraulic head relative to <i>Terrain Elevation</i>. Positive values are above terrain.";
        break;
    case InfrastructureEntity::Tank:
        group_title = "Bottom Elevation";
        absolute_input_text = "Bottom Elevation";
        offset_label_text = "Bottom Offset";
        value_label_text = "Bottom Elevation";
        offset_tooltip =
            "Tank bottom relative to terrain. Positive = above ground, negative = below ground.";
        break;
    default:
        return;
    }

    this->group_elevation = new GroupBoxCollapsible(group_title);
    QGridLayout *grid = new QGridLayout(this->group_elevation);
    
    this->combo_elevation_input_type = new QComboBox();
    this->combo_elevation_input_type->addItem(
        absolute_input_text,
        static_cast<int>(HydraulicNodeElevationInputType::AbsoluteElevation));
    this->combo_elevation_input_type->addItem(
        "Terrain Elevation + Offset",
        static_cast<int>(HydraulicNodeElevationInputType::TerrainElevationAndOffset));
    
    this->button_terrain_elevation = new QPushButton("Terrain Elevation from GIS");
    this->button_terrain_elevation->setToolTip(
        "Uses terrain elevation from GIS/DEM data.<br>Accuracy depends on the dataset and local terrain."
        );
    
    this->label_terrain_elevation = new QLabel("Terrain Elevation");
    this->spin_terrain_elevation = new QDoubleSpinBox;
    this->spin_terrain_elevation->setRange(-10000.0, 10000.0);
    this->spin_terrain_elevation->setDecimals(3);
    this->spin_terrain_elevation->setSingleStep(0.10);
    this->spin_terrain_elevation->setSuffix(" m");
    
    this->label_elevation_offset = new QLabel(offset_label_text);
    this->label_elevation_offset->setWordWrap(true);
    this->spin_elevation_offset = new QDoubleSpinBox;
    this->spin_elevation_offset->setRange(-10000.0, 10000.0);
    this->spin_elevation_offset->setDecimals(3);
    this->spin_elevation_offset->setSingleStep(0.10);
    this->spin_elevation_offset->setSuffix(" m");
    this->spin_elevation_offset->setToolTip(offset_tooltip);

    this->label_elevation_value = new QLabel(value_label_text);
    this->label_elevation_value->setWordWrap(true);
    this->spin_elevation_value = new QDoubleSpinBox;
    this->spin_elevation_value->setRange(-10000.0, 10000.0);
    this->spin_elevation_value->setDecimals(3);
    this->spin_elevation_value->setSingleStep(0.10);
    this->spin_elevation_value->setSuffix(" m");

    grid->addWidget(this->combo_elevation_input_type, 0, 0, 1, 2);
    grid->addWidget(this->button_terrain_elevation, 1, 0, 1, 2);
    grid->addWidget(this->label_terrain_elevation, 2, 0);
    grid->addWidget(this->spin_terrain_elevation, 2, 1);
    grid->addWidget(this->label_elevation_offset, 3, 0);
    grid->addWidget(this->spin_elevation_offset, 3, 1);
    grid->addWidget(this->label_elevation_value, 4, 0);
    grid->addWidget(this->spin_elevation_value, 4, 1);
    
    connect(this->combo_elevation_input_type, &QComboBox::currentIndexChanged, this, &EntityInspectorWidget::onElevationInputTypeChanged);
    connect(this->spin_elevation_value, &QDoubleSpinBox::valueChanged, this, &EntityInspectorWidget::onElevationValueChanged);
    connect(this->spin_terrain_elevation, &QDoubleSpinBox::valueChanged, this, &EntityInspectorWidget::onTerrainElevationChanged);
    connect(this->spin_elevation_offset, &QDoubleSpinBox::valueChanged, this, &EntityInspectorWidget::onElevationOffsetChanged);
    
    connect(this->group_elevation, &GroupBoxCollapsible::signalExpanded, this, &EntityInspectorWidget::onGroupExpand);
    
    this->layoutConfiguration()->addWidget(this->group_elevation);

    refreshHydraulicNodeElevation();
}

void EntityInspectorWidget::onGroupExpand(GroupBoxCollapsible *group)
{
    if (group == this->group_elevation)
        updateElevationModeUi();
}

void EntityInspectorWidget::refreshHydraulicNodeElevation()
{
    if (!this->hydraulic_data || this->entity_uuid.isNull() ||
        !this->combo_elevation_input_type || !this->spin_terrain_elevation ||
        !this->spin_elevation_offset || !this->spin_elevation_value)
        return;

    HydraulicNodeElevationInputType input_type = HydraulicNodeElevationInputType::AbsoluteElevation;
    double value_m = 0.0;
    double terrain_elevation_m = 0.0;
    double offset_m = 0.0;

    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
    {
        const std::optional<HydraulicNodeJunction> junction =
            this->hydraulic_data->junction(this->entity_uuid);
        if (!junction.has_value())
            return;

        input_type = junction->elevation_input_type;
        value_m = junction->elevation_m;
        terrain_elevation_m = junction->terrain_elevation_m;
        offset_m = junction->elevation_offset_m;
        break;
    }
    case InfrastructureEntity::Reservoir:
    {
        const std::optional<HydraulicNodeReservoir> reservoir =
            this->hydraulic_data->reservoir(this->entity_uuid);
        if (!reservoir.has_value())
            return;

        input_type = reservoir->head_input_type;
        value_m = reservoir->head_m;
        terrain_elevation_m = reservoir->terrain_elevation_m;
        offset_m = reservoir->head_offset_m;
        break;
    }
    case InfrastructureEntity::Tank:
    {
        const std::optional<HydraulicNodeTank> tank =
            this->hydraulic_data->tank(this->entity_uuid);
        if (!tank.has_value())
            return;

        input_type = tank->elevation_input_type;
        value_m = tank->bottom_elevation_m;
        terrain_elevation_m = tank->terrain_elevation_m;
        offset_m = tank->bottom_offset_m;
        break;
    }
    default:
        return;
    }

    const QSignalBlocker input_type_blocker(this->combo_elevation_input_type);
    const QSignalBlocker terrain_blocker(this->spin_terrain_elevation);
    const QSignalBlocker offset_blocker(this->spin_elevation_offset);
    const QSignalBlocker value_blocker(this->spin_elevation_value);

    const int input_type_index = this->combo_elevation_input_type->findData(
        static_cast<int>(input_type));
    this->combo_elevation_input_type->setCurrentIndex(input_type_index >= 0 ? input_type_index : 0);
    this->spin_terrain_elevation->setValue(terrain_elevation_m);
    this->spin_elevation_offset->setValue(offset_m);
    this->spin_elevation_value->setValue(
        input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset
            ? terrain_elevation_m + offset_m
            : value_m);

    updateElevationModeUi();
}

void EntityInspectorWidget::updateElevationModeUi()
{
    if (!this->combo_elevation_input_type)
        return;

    const HydraulicNodeElevationInputType input_type =
        static_cast<HydraulicNodeElevationInputType>(
            this->combo_elevation_input_type->currentData().toInt());
    const bool uses_terrain =
        input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset;

    this->button_terrain_elevation->setVisible(uses_terrain);
    this->label_terrain_elevation->setVisible(uses_terrain);
    this->spin_terrain_elevation->setVisible(uses_terrain);
    this->label_elevation_offset->setVisible(uses_terrain);
    this->spin_elevation_offset->setVisible(uses_terrain);
    this->spin_elevation_value->setReadOnly(uses_terrain);

    if (uses_terrain)
    {
        this->spin_elevation_value->setToolTip(
            "Calculated automatically from <i>Terrain Elevation</i> + <i>Offset</i>");
        updateCalculatedElevation();
    }
    else
    {
        this->spin_elevation_value->setToolTip("");
    }
}

void EntityInspectorWidget::updateCalculatedElevation()
{
    if (!this->combo_elevation_input_type || !this->spin_elevation_value)
        return;

    const HydraulicNodeElevationInputType input_type =
        static_cast<HydraulicNodeElevationInputType>(
            this->combo_elevation_input_type->currentData().toInt());
    if (input_type != HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        return;

    const QSignalBlocker value_blocker(this->spin_elevation_value);
    this->spin_elevation_value->setValue(
        this->spin_terrain_elevation->value() + this->spin_elevation_offset->value());
}

void EntityInspectorWidget::onElevationInputTypeChanged(int index)
{
    Q_UNUSED(index)

    const HydraulicNodeElevationInputType input_type =
        static_cast<HydraulicNodeElevationInputType>(
            this->combo_elevation_input_type->currentData().toInt());
    updateElevationModeUi();
    setElevationInputType(input_type);
}

void EntityInspectorWidget::onElevationValueChanged(double value_m)
{
    const HydraulicNodeElevationInputType input_type =
        static_cast<HydraulicNodeElevationInputType>(
            this->combo_elevation_input_type->currentData().toInt());
    if (input_type == HydraulicNodeElevationInputType::AbsoluteElevation)
        setElevationValue(value_m);
}

void EntityInspectorWidget::onTerrainElevationChanged(double terrain_elevation_m)
{
    setTerrainElevation(terrain_elevation_m);
    updateCalculatedElevation();
}

void EntityInspectorWidget::onElevationOffsetChanged(double offset_m)
{
    setElevationOffset(offset_m);
    updateCalculatedElevation();
}

bool EntityInspectorWidget::setElevationInputType(HydraulicNodeElevationInputType input_type)
{
    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
        return this->hydraulic_data->setJunctionElevationInputType(this->entity_uuid, input_type);
    case InfrastructureEntity::Reservoir:
        return this->hydraulic_data->setReservoirHeadInputType(this->entity_uuid, input_type);
    case InfrastructureEntity::Tank:
        return this->hydraulic_data->setTankElevationInputType(this->entity_uuid, input_type);
    default:
        return false;
    }
}

bool EntityInspectorWidget::setElevationValue(double value_m)
{
    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
        return this->hydraulic_data->setJunctionElevationM(this->entity_uuid, value_m);
    case InfrastructureEntity::Reservoir:
        return this->hydraulic_data->setReservoirHeadM(this->entity_uuid, value_m);
    case InfrastructureEntity::Tank:
        return this->hydraulic_data->setTankBottomElevationM(this->entity_uuid, value_m);
    default:
        return false;
    }
}

bool EntityInspectorWidget::setTerrainElevation(double terrain_elevation_m)
{
    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
        return this->hydraulic_data->setJunctionTerrainElevationM(
            this->entity_uuid, terrain_elevation_m);
    case InfrastructureEntity::Reservoir:
        return this->hydraulic_data->setReservoirTerrainElevationM(
            this->entity_uuid, terrain_elevation_m);
    case InfrastructureEntity::Tank:
        return this->hydraulic_data->setTankTerrainElevationM(
            this->entity_uuid, terrain_elevation_m);
    default:
        return false;
    }
}

bool EntityInspectorWidget::setElevationOffset(double offset_m)
{
    switch (this->entity_type)
    {
    case InfrastructureEntity::Junction:
        return this->hydraulic_data->setJunctionElevationOffsetM(this->entity_uuid, offset_m);
    case InfrastructureEntity::Reservoir:
        return this->hydraulic_data->setReservoirHeadOffsetM(this->entity_uuid, offset_m);
    case InfrastructureEntity::Tank:
        return this->hydraulic_data->setTankBottomOffsetM(this->entity_uuid, offset_m);
    default:
        return false;
    }
}

void EntityInspectorWidget::onHeadlossFormulaChanged(HeadlossFormulas formulas)
{
    Q_UNUSED(formulas)
}

void EntityInspectorWidget::addGroupDemands()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Demands & Emitter");
    QGridLayout *grid = new QGridLayout(group);

    this->label_demands_summary = new QLabel();
    this->label_demands_summary->setWordWrap(true);

    QLabel *label_emitter_coefficient = new QLabel("Emitter Coefficient");
    this->spin_emitter_coefficient = new QDoubleSpinBox();
    this->spin_emitter_coefficient->setRange(-1000000000.0, 1000000000.0);
    this->spin_emitter_coefficient->setDecimals(6);
    this->spin_emitter_coefficient->setSingleStep(0.001);
    this->spin_emitter_coefficient->setSuffix(QStringLiteral(" m³/h/mⁿ"));
    this->spin_emitter_coefficient->setAlignment(Qt::AlignRight);
    this->spin_emitter_coefficient->setToolTip(
        "Coefficient C in Q = C · pⁿ. Flow Q is stored in m³/h, pressure head p in m, "
        "and n is the global emitter exponent."
    );

    QPushButton *button_editor = new QPushButton("Open Editor");
    connect(button_editor, &QPushButton::clicked, this, [this]()
    {
        openDemandsEditor();
    });
    connect(this->spin_emitter_coefficient, &QDoubleSpinBox::valueChanged, this, [this](double value)
    {
        this->hydraulic_data->setJunctionEmitterCoefficientM3PerHPerMExponent(
            this->entity_uuid, value);
    });

    grid->addWidget(this->label_demands_summary, 0, 0, 1, 2);
    grid->addWidget(label_emitter_coefficient, 1, 0);
    grid->addWidget(this->spin_emitter_coefficient, 1, 1);
    grid->addWidget(button_editor, 2, 0, 1, 2);

    this->layoutConfiguration()->addWidget(group);
    refreshJunctionDemands();
}

void EntityInspectorWidget::scheduleJunctionDemandsRefresh()
{
    if (this->entity_type != InfrastructureEntity::Junction ||
        this->junction_demands_refresh_pending)
        return;

    this->junction_demands_refresh_pending = true;
    QTimer::singleShot(0, this, [this]()
    {
        this->junction_demands_refresh_pending = false;
        refreshJunctionDemands();
    });
}

void EntityInspectorWidget::refreshJunctionDemands()
{
    if (!this->hydraulic_data || this->entity_type != InfrastructureEntity::Junction ||
        this->entity_uuid.isNull())
        return;

    const std::optional<HydraulicNodeJunction> junction =
        this->hydraulic_data->junction(this->entity_uuid);
    if (!junction.has_value())
        return;

    if (this->label_demands_summary)
    {
        double total_base_demand_m3_per_h = 0.0;
        for (const HydraulicNodeJunctionDemand &demand : junction->demands)
            total_base_demand_m3_per_h += demand.base_demand_m3_per_h;

        const int demand_count = junction->demands.size();
        this->label_demands_summary->setText(
            QStringLiteral("%1 demand categor%2\nTotal base demand: %3 m³/h")
                .arg(demand_count)
                .arg(demand_count == 1 ? QStringLiteral("y") : QStringLiteral("ies"))
                .arg(total_base_demand_m3_per_h, 0, 'f', 3)
        );
    }

    if (this->spin_emitter_coefficient)
    {
        const QSignalBlocker emitter_blocker(this->spin_emitter_coefficient);
        this->spin_emitter_coefficient->setValue(
            junction->emitter_coefficient_m3_per_h_per_m_exponent);
    }

    if (!this->table_demands)
        return;

    if (this->table_demands->rowCount() != junction->demands.size())
    {
        rebuildJunctionDemandRows(junction.value());
        return;
    }

    for (int demand_index = 0; demand_index < junction->demands.size(); demand_index++)
        updateJunctionDemandRow(demand_index, junction->demands.at(demand_index));
}

void EntityInspectorWidget::rebuildJunctionDemandRows(const HydraulicNodeJunction &junction)
{
    if (!this->table_demands)
        return;

    this->table_demands->setUpdatesEnabled(false);
    this->table_demands->clearContents();
    this->table_demands->setRowCount(junction.demands.size());

    for (int demand_index = 0; demand_index < junction.demands.size(); demand_index++)
        addJunctionDemandRow(demand_index, junction.demands.at(demand_index));

    this->table_demands->setUpdatesEnabled(true);
}

void EntityInspectorWidget::addJunctionDemandRow(
    int demand_index, const HydraulicNodeJunctionDemand &demand)
{
    if (!this->table_demands)
        return;

    QLineEdit *line_category = new QLineEdit(this->table_demands);
    QDoubleSpinBox *spin_base_demand = new QDoubleSpinBox(this->table_demands);
    spin_base_demand->setRange(-1000000000.0, 1000000000.0);
    spin_base_demand->setDecimals(6);
    spin_base_demand->setSingleStep(0.1);
    spin_base_demand->setSuffix(QStringLiteral(" m³/h"));
    spin_base_demand->setAlignment(Qt::AlignRight);

    QComboBox *combo_pattern = new QComboBox(this->table_demands);

    QComboBox *combo_source = new QComboBox(this->table_demands);
    combo_source->addItem(
        "Manual Estimation",
        static_cast<int>(HydraulicNodeJunctionDemandSourceMethod::ManualEstimation));
    combo_source->addItem(
        "Meter Data",
        static_cast<int>(HydraulicNodeJunctionDemandSourceMethod::MeterData));
    combo_source->addItem(
        "Scenario",
        static_cast<int>(HydraulicNodeJunctionDemandSourceMethod::Scenario));

    QLineEdit *line_note = new QLineEdit(this->table_demands);
    QPushButton *button_delete = new QPushButton(QIcon(":/icon/remove.png"), "", this->table_demands);
    button_delete->setToolTip("Delete this demand category");
    button_delete->setMaximumWidth(35);

    this->table_demands->setCellWidget(demand_index, 0, line_category);
    this->table_demands->setCellWidget(demand_index, 1, spin_base_demand);
    this->table_demands->setCellWidget(demand_index, 2, combo_pattern);
    this->table_demands->setCellWidget(demand_index, 3, combo_source);
    this->table_demands->setCellWidget(demand_index, 4, line_note);
    this->table_demands->setCellWidget(demand_index, 5, button_delete);

    updateJunctionDemandRow(demand_index, demand);

    connect(line_category, &QLineEdit::textEdited, this, [this, demand_index](const QString &category_name)
    {
        this->hydraulic_data->setJunctionDemandCategoryName(
            this->entity_uuid, demand_index, category_name);
    });
    connect(spin_base_demand, &QDoubleSpinBox::valueChanged, this, [this, demand_index](double base_demand_m3_per_h)
    {
        this->hydraulic_data->setJunctionDemandBaseDemandM3PerH(
            this->entity_uuid, demand_index, base_demand_m3_per_h);
    });
    connect(combo_pattern, &QComboBox::currentIndexChanged, this, [this, demand_index, combo_pattern](int)
    {
        this->hydraulic_data->setJunctionDemandPatternUuid(
            this->entity_uuid, demand_index, combo_pattern->currentData().toUuid());
    });
    connect(combo_source, &QComboBox::currentIndexChanged, this, [this, demand_index, combo_source](int)
    {
        const HydraulicNodeJunctionDemandSourceMethod source_method =
            static_cast<HydraulicNodeJunctionDemandSourceMethod>(
                combo_source->currentData().toInt());
        this->hydraulic_data->setJunctionDemandSourceMethod(
            this->entity_uuid, demand_index, source_method);
    });
    connect(line_note, &QLineEdit::textEdited, this, [this, demand_index](const QString &note)
    {
        this->hydraulic_data->setJunctionDemandNote(this->entity_uuid, demand_index, note);
    });
    connect(button_delete, &QPushButton::clicked, this, [this, demand_index]()
    {
        this->hydraulic_data->removeJunctionDemand(this->entity_uuid, demand_index);
    });
}

void EntityInspectorWidget::updateJunctionDemandRow(
    int demand_index, const HydraulicNodeJunctionDemand &demand)
{
    if (!this->table_demands || demand_index < 0 || demand_index >= this->table_demands->rowCount())
        return;

    QLineEdit *line_category = qobject_cast<QLineEdit *>(
        this->table_demands->cellWidget(demand_index, 0));
    QDoubleSpinBox *spin_base_demand = qobject_cast<QDoubleSpinBox *>(
        this->table_demands->cellWidget(demand_index, 1));
    QComboBox *combo_pattern = qobject_cast<QComboBox *>(
        this->table_demands->cellWidget(demand_index, 2));
    QComboBox *combo_source = qobject_cast<QComboBox *>(
        this->table_demands->cellWidget(demand_index, 3));
    QLineEdit *line_note = qobject_cast<QLineEdit *>(
        this->table_demands->cellWidget(demand_index, 4));

    if (line_category && line_category->text() != demand.category_name)
    {
        const QSignalBlocker category_blocker(line_category);
        line_category->setText(demand.category_name);
    }

    if (spin_base_demand)
    {
        const QSignalBlocker base_demand_blocker(spin_base_demand);
        spin_base_demand->setValue(demand.base_demand_m3_per_h);
    }

    if (combo_pattern)
        populateDemandPatternCombo(combo_pattern, demand.pattern_uuid);

    if (combo_source)
    {
        const int source_index = combo_source->findData(static_cast<int>(demand.source_method));
        const QSignalBlocker source_blocker(combo_source);
        combo_source->setCurrentIndex(source_index >= 0 ? source_index : 0);
    }

    if (line_note && line_note->text() != demand.note)
    {
        const QSignalBlocker note_blocker(line_note);
        line_note->setText(demand.note);
    }
}

void EntityInspectorWidget::populateDemandPatternCombo(
    QComboBox *combo_pattern, const QUuid &pattern_uuid)
{
    if (!combo_pattern || !this->hydraulic_data)
        return;

    const QList<HydraulicPatternTime> &patterns =
        this->hydraulic_data->networkHydraulic().patterns_time;
    QString pattern_signature;
    for (const HydraulicPatternTime &pattern : patterns)
    {
        pattern_signature += pattern.uuid.toString(QUuid::WithoutBraces);
        pattern_signature += QLatin1Char(':');
        pattern_signature += pattern.id;
        pattern_signature += QLatin1Char('\n');
    }

    bool pattern_exists = pattern_uuid.isNull();
    for (const HydraulicPatternTime &pattern : patterns)
    {
        if (pattern.uuid == pattern_uuid)
        {
            pattern_exists = true;
            break;
        }
    }
    if (!pattern_exists)
        pattern_signature += QStringLiteral("missing:%1").arg(
            pattern_uuid.toString(QUuid::WithoutBraces));

    const QSignalBlocker pattern_blocker(combo_pattern);
    if (combo_pattern->property("aowisPatternSignature").toString() != pattern_signature)
    {
        combo_pattern->clear();
        combo_pattern->addItem("Constant", QUuid());
        for (const HydraulicPatternTime &pattern : patterns)
        {
            const QString pattern_name = pattern.id.isEmpty()
                ? pattern.uuid.toString(QUuid::WithoutBraces)
                : pattern.id;
            combo_pattern->addItem(pattern_name, pattern.uuid);
        }
        if (!pattern_exists)
        {
            combo_pattern->addItem(
                QStringLiteral("[Missing Pattern] %1").arg(
                    pattern_uuid.toString(QUuid::WithoutBraces)),
                pattern_uuid);
        }
        combo_pattern->setProperty("aowisPatternSignature", pattern_signature);
    }

    const int pattern_index = combo_pattern->findData(pattern_uuid);
    combo_pattern->setCurrentIndex(pattern_index >= 0 ? pattern_index : 0);
}

void EntityInspectorWidget::openDemandsEditor()
{
    if (this->dialog_demands)
    {
        this->dialog_demands->raise();
        this->dialog_demands->activateWindow();
        return;
    }

    this->dialog_demands = new QDialog(this);
    this->dialog_demands->setWindowTitle("Junction Demands");
    this->dialog_demands->resize(950, 420);
    this->dialog_demands->setAttribute(Qt::WA_DeleteOnClose);

    QGridLayout *grid = new QGridLayout(this->dialog_demands);

    this->table_demands = new QTableWidget(this->dialog_demands);
    this->table_demands->setColumnCount(6);
    this->table_demands->setHorizontalHeaderLabels(
        QStringList{"Category", "Base Demand", "Pattern", "Source / Method", "Note", ""});
    this->table_demands->verticalHeader()->setVisible(false);
    this->table_demands->setSelectionMode(QAbstractItemView::NoSelection);
    this->table_demands->horizontalHeader()->setStretchLastSection(false);
    this->table_demands->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    this->table_demands->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    this->table_demands->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    this->table_demands->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    this->table_demands->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    this->table_demands->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    this->table_demands->setColumnWidth(0, 150);
    this->table_demands->setColumnWidth(2, 150);
    this->table_demands->setColumnWidth(5, 35);

    QPushButton *button_demand = new QPushButton("Add Demand");
    connect(button_demand, &QPushButton::clicked, this, [this]()
    {
        HydraulicNodeJunctionDemand demand;
        this->hydraulic_data->addJunctionDemand(this->entity_uuid, demand);
    });

    QPushButton *button_patterns = new QPushButton("Manage Patterns");

    grid->addWidget(this->table_demands, 0, 0, 1, 2);
    grid->addWidget(button_demand, 1, 0);
    grid->addWidget(button_patterns, 1, 1);

    connect(this->dialog_demands, &QObject::destroyed, this, [this]()
    {
        this->dialog_demands = nullptr;
        this->table_demands = nullptr;
    });

    refreshJunctionDemands();
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
