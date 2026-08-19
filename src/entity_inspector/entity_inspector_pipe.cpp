#include "entity_inspector_pipe.h"

#include <optional>

#include <QSignalBlocker>

EntityInspectorPipe::EntityInspectorPipe(HydraulicData *hydraulic_data, const HydraulicLinkPipe &pipe, QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent), hydraulic_data(hydraulic_data), pipe_uuid(pipe.uuid)
{
    addGroupOverviewImage(":/icon/pipe.png", pipe.id);

    addGroupGeneral(QString());
    addGroupEndpoints();
    addGroupGeometry();
    addGroupRoughness();
    addGroupQuality();
    addGroupHistory();

    bindHydraulicLink(InfrastructureEntity::Pipe, this->pipe_uuid, "Pipe");
    addGroupSimulation();
    addStretches();

    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
            [this](InfrastructureEntity entity_type, const QUuid &uuid)
    {
        if (entity_type == InfrastructureEntity::Pipe && uuid == this->pipe_uuid)
            refreshPipe();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, [this]()
    {
        refreshPipe();
    });

    refreshPipe();
}

void EntityInspectorPipe::addGroupGeometry()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Geometry");
    QGridLayout *grid = new QGridLayout(group);

    QLabel *label_status_initial = new QLabel("Initial Status");
    this->combo_status_initial = new QComboBox();
    this->combo_status_initial->addItem("Open", static_cast<int>(HydraulicLinkPipeInitialStatus::Open));
    this->combo_status_initial->addItem("Closed", static_cast<int>(HydraulicLinkPipeInitialStatus::Closed));
    this->combo_status_initial->addItem("Check Valve", static_cast<int>(HydraulicLinkPipeInitialStatus::CheckValve));

    QLabel *label_diameter = new QLabel("Diameter");
    this->spin_diameter = new QDoubleSpinBox();
    this->spin_diameter->setSuffix(" mm");
    this->spin_diameter->setDecimals(1);
    this->spin_diameter->setRange(1.0, 5000.0);
    this->spin_diameter->setSingleStep(10.0);

    QLabel *label_length_calculated = new QLabel("Calculated Length");
    this->spin_length_calculated = new QDoubleSpinBox();
    this->spin_length_calculated->setSuffix(" m");
    this->spin_length_calculated->setDecimals(2);
    this->spin_length_calculated->setRange(0.0, 1000000000.0);
    this->spin_length_calculated->setReadOnly(true);
    this->spin_length_calculated->setButtonSymbols(QAbstractSpinBox::NoButtons);
    this->spin_length_calculated->setToolTip("Calculated from the endpoint and intermediate vertex coordinates.");

    this->check_length_measured = new QCheckBox("Override with measured length");

    QLabel *label_length_measured = new QLabel("Measured Length");
    this->spin_length_measured = new QDoubleSpinBox();
    this->spin_length_measured->setSuffix(" m");
    this->spin_length_measured->setDecimals(2);
    this->spin_length_measured->setRange(0.0, 1000000000.0);
    this->spin_length_measured->setSingleStep(1.0);
    this->spin_length_measured->setEnabled(false);

    grid->addWidget(label_status_initial, 0, 0);
    grid->addWidget(this->combo_status_initial, 0, 1);
    grid->addWidget(label_diameter, 1, 0);
    grid->addWidget(this->spin_diameter, 1, 1);
    grid->addWidget(label_length_calculated, 2, 0);
    grid->addWidget(this->spin_length_calculated, 2, 1);
    grid->addWidget(this->check_length_measured, 3, 0, 1, 2);
    grid->addWidget(label_length_measured, 4, 0);
    grid->addWidget(this->spin_length_measured, 4, 1);

    connect(this->combo_status_initial, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const HydraulicLinkPipeInitialStatus initial_status = static_cast<HydraulicLinkPipeInitialStatus>(
            this->combo_status_initial->currentData().toInt());
        this->hydraulic_data->setPipeInitialStatus(this->pipe_uuid, initial_status);
    });
    connect(this->spin_diameter, &QDoubleSpinBox::valueChanged, this, [this](double diameter_mm)
    {
        this->hydraulic_data->setPipeDiameterMm(this->pipe_uuid, diameter_mm);
    });
    connect(this->check_length_measured, &QCheckBox::toggled, this, [this](bool use_measured_length)
    {
        if (!use_measured_length)
        {
            this->hydraulic_data->setPipeMeasuredLengthM(this->pipe_uuid, std::nullopt);
            return;
        }

        const std::optional<HydraulicLinkPipe> pipe = this->hydraulic_data->pipe(this->pipe_uuid);
        if (!pipe.has_value())
            return;

        const double measured_length_m = pipe->length_measured_m.value_or(pipe->length_calculated_m);
        this->hydraulic_data->setPipeMeasuredLengthM(this->pipe_uuid, measured_length_m);
    });
    connect(this->spin_length_measured, &QDoubleSpinBox::valueChanged, this, [this](double length_m)
    {
        if (this->check_length_measured->isChecked())
            this->hydraulic_data->setPipeMeasuredLengthM(this->pipe_uuid, length_m);
    });

    layoutConfiguration()->addWidget(group);
}

void EntityInspectorPipe::addGroupRoughness()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Roughness");
    QGridLayout *grid = new QGridLayout(group);

    QLabel *label_material = new QLabel("Material");
    this->combo_material = new QComboBox();
    this->combo_material->setEditable(true);
    this->combo_material->setInsertPolicy(QComboBox::NoInsert);

    QPushButton *button_material_edit = new QPushButton("Edit Materials");

    QLabel *label_roughness_hw = new QLabel("Roughness<br>Hazen-Williams");
    label_roughness_hw->setWordWrap(true);
    this->spin_roughness_hw = new QDoubleSpinBox();
    this->spin_roughness_hw->setToolTip("Pipe roughness coefficient C");
    this->spin_roughness_hw->setDecimals(0);
    this->spin_roughness_hw->setRange(1.0, 200.0);
    this->spin_roughness_hw->setSingleStep(1.0);

    QLabel *label_roughness_dw = new QLabel("Roughness<br>Darcy-Weisbach");
    label_roughness_dw->setWordWrap(true);
    this->spin_roughness_dw = new QDoubleSpinBox();
    this->spin_roughness_dw->setToolTip("Absolute pipe roughness ε in mm");
    this->spin_roughness_dw->setSuffix(" mm");
    this->spin_roughness_dw->setDecimals(6);
    this->spin_roughness_dw->setRange(0.000001, 100.0);
    this->spin_roughness_dw->setSingleStep(0.001);

    QLabel *label_roughness_cm = new QLabel("Roughness<br>Chezy-Manning");
    label_roughness_cm->setWordWrap(true);
    this->spin_roughness_cm = new QDoubleSpinBox();
    this->spin_roughness_cm->setToolTip("Manning roughness coefficient n");
    this->spin_roughness_cm->setDecimals(4);
    this->spin_roughness_cm->setRange(0.0010, 0.1000);
    this->spin_roughness_cm->setSingleStep(0.0010);

    QLabel *label_loss_coefficient = new QLabel("Loss Coefficient");
    label_loss_coefficient->setWordWrap(true);
    this->spin_loss_coefficient = new QDoubleSpinBox();
    this->spin_loss_coefficient->setToolTip(QStringLiteral("Dimensionless minor loss coefficient K<br>for local losses from bends, fittings,<br>entrances, exits, etc.<br><br>Default is 0 for no additional minor losses."));
    this->spin_loss_coefficient->setDecimals(3);
    this->spin_loss_coefficient->setRange(0.0, 1000.0);
    this->spin_loss_coefficient->setSingleStep(0.1);

    grid->addWidget(label_material, 0, 0);
    grid->addWidget(this->combo_material, 0, 1);
    grid->addWidget(button_material_edit, 1, 1);
    grid->addWidget(label_roughness_hw, 2, 0);
    grid->addWidget(this->spin_roughness_hw, 2, 1);
    grid->addWidget(label_roughness_dw, 3, 0);
    grid->addWidget(this->spin_roughness_dw, 3, 1);
    grid->addWidget(label_roughness_cm, 4, 0);
    grid->addWidget(this->spin_roughness_cm, 4, 1);
    grid->addWidget(label_loss_coefficient, 5, 0);
    grid->addWidget(this->spin_loss_coefficient, 5, 1);

    connect(this->combo_material, &QComboBox::editTextChanged, this, [this](const QString &material_id)
    {
        this->hydraulic_data->setPipeMaterialId(this->pipe_uuid, material_id);
    });
    connect(this->spin_roughness_hw, &QDoubleSpinBox::valueChanged, this, [this](double roughness_hazen_williams)
    {
        this->hydraulic_data->setPipeRoughnessHw(this->pipe_uuid, roughness_hazen_williams);
    });
    connect(this->spin_roughness_dw, &QDoubleSpinBox::valueChanged, this, [this](double roughness_darcy_weisbach_mm)
    {
        this->hydraulic_data->setPipeRoughnessDwMm(this->pipe_uuid, roughness_darcy_weisbach_mm);
    });
    connect(this->spin_roughness_cm, &QDoubleSpinBox::valueChanged, this, [this](double roughness_chezy_manning)
    {
        this->hydraulic_data->setPipeRoughnessCm(this->pipe_uuid, roughness_chezy_manning);
    });
    connect(this->spin_loss_coefficient, &QDoubleSpinBox::valueChanged, this, [this](double minor_loss_coefficient)
    {
        this->hydraulic_data->setPipeMinorLoss(this->pipe_uuid, minor_loss_coefficient);
    });

    layoutConfiguration()->addWidget(group);
}

void EntityInspectorPipe::refreshPipe()
{
    if (!this->hydraulic_data || this->pipe_uuid.isNull())
        return;

    const std::optional<HydraulicLinkPipe> pipe = this->hydraulic_data->pipe(this->pipe_uuid);
    if (!pipe.has_value())
        return;

    const QSignalBlocker status_blocker(this->combo_status_initial);
    const QSignalBlocker diameter_blocker(this->spin_diameter);
    const QSignalBlocker calculated_length_blocker(this->spin_length_calculated);
    const QSignalBlocker measured_length_enabled_blocker(this->check_length_measured);
    const QSignalBlocker measured_length_blocker(this->spin_length_measured);
    const QSignalBlocker material_blocker(this->combo_material);
    const QSignalBlocker roughness_hw_blocker(this->spin_roughness_hw);
    const QSignalBlocker roughness_dw_blocker(this->spin_roughness_dw);
    const QSignalBlocker roughness_cm_blocker(this->spin_roughness_cm);
    const QSignalBlocker minor_loss_blocker(this->spin_loss_coefficient);

    const int status_index = this->combo_status_initial->findData(static_cast<int>(pipe->initial_status));
    this->combo_status_initial->setCurrentIndex(status_index >= 0 ? status_index : 0);
    this->spin_diameter->setValue(pipe->diameter_mm);
    this->spin_length_calculated->setValue(pipe->length_calculated_m);

    const bool has_measured_length = pipe->length_measured_m.has_value();
    this->check_length_measured->setChecked(has_measured_length);
    this->spin_length_measured->setEnabled(has_measured_length);
    this->spin_length_measured->setValue(pipe->length_measured_m.value_or(pipe->length_calculated_m));

    if (this->combo_material->findText(pipe->material_id) < 0)
        this->combo_material->addItem(pipe->material_id);
    this->combo_material->setCurrentText(pipe->material_id);

    this->spin_roughness_hw->setValue(pipe->roughness_hazen_williams);
    this->spin_roughness_dw->setValue(pipe->roughness_darcy_weisbach_mm);
    this->spin_roughness_cm->setValue(pipe->roughness_chezy_manning);
    this->spin_loss_coefficient->setValue(pipe->minor_loss_coefficient);
}

void EntityInspectorPipe::addGroupQuality()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Quality Settings");
    QGridLayout *grid = new QGridLayout(group);
    
    this->check_override = new QCheckBox("Override global coefficients");
    QPushButton *button_override_show = new QPushButton("Edit global reaction coefficients");
    
    QLabel *label_spin_bulk = new QLabel("Bulk reaction coefficient");
    label_spin_bulk->setWordWrap(true);
    
    this->spin_bulk_reaction = new QDoubleSpinBox();
    this->spin_bulk_reaction->setDecimals(6);
    this->spin_bulk_reaction->setMinimum(-1000.0);
    this->spin_bulk_reaction->setMaximum(1000.0);
    this->spin_bulk_reaction->setSingleStep(0.001);
    this->spin_bulk_reaction->setValue(0.0);
    this->spin_bulk_reaction->setToolTip(QStringLiteral("Coefficient dimensions depend on the configured bulk reaction order."));
    this->spin_bulk_reaction->setAlignment(Qt::AlignRight);
    this->spin_bulk_reaction->setEnabled(false);
    
    QLabel *label_spin_wall = new QLabel("Wall reaction coefficient");
    label_spin_wall->setWordWrap(true);
    
    this->spin_wall_reaction = new QDoubleSpinBox();
    this->spin_wall_reaction->setDecimals(6);
    this->spin_wall_reaction->setMinimum(-1000.0);
    this->spin_wall_reaction->setMaximum(1000.0);
    this->spin_wall_reaction->setSingleStep(0.001);
    this->spin_wall_reaction->setValue(0.0);
    this->spin_wall_reaction->setToolTip(QStringLiteral("Coefficient dimensions depend on the configured wall reaction order."));
    this->spin_wall_reaction->setAlignment(Qt::AlignRight);
    this->spin_wall_reaction->setEnabled(false);
    
    connect(this->check_override, &QCheckBox::checkStateChanged, this, [this]
    {
        if (this->check_override->isChecked())
        {
            this->spin_bulk_reaction->setEnabled(true);
            this->spin_wall_reaction->setEnabled(true);
        }
        else
        {
            this->spin_bulk_reaction->setEnabled(false);
            this->spin_wall_reaction->setEnabled(false);
        }
    });
    
    grid->addWidget(button_override_show, 0, 0, 1, 2);
    grid->addWidget(this->check_override, 1, 0, 1, 2);
    grid->addWidget(label_spin_bulk, 2, 0);
    grid->addWidget(this->spin_bulk_reaction, 2, 1);
    grid->addWidget(label_spin_wall, 3, 0);
    grid->addWidget(this->spin_wall_reaction, 3, 1);
    
    layoutQuality()->addWidget(group);
}

void EntityInspectorPipe::onHeadlossFormulaChanged(HeadlossFormulas formulas)
{
    const bool use_hw = formulas.testFlag(HeadlossFormula::HazenWilliams);
    const bool use_dw = formulas.testFlag(HeadlossFormula::DarcyWeisbach);
    const bool use_cm = formulas.testFlag(HeadlossFormula::ChezyManning);
    
    this->spin_roughness_hw->setDisabled(!use_hw);
    this->spin_roughness_dw->setDisabled(!use_dw);
    this->spin_roughness_cm->setDisabled(!use_cm);
    
    this->spin_roughness_hw->setToolTip(
        use_hw
            ? QStringLiteral("Pipe roughness coefficient C<br>Used by the <b>Hazen-Williams</b> formula.")
            : QStringLiteral("Enable <b>Hazen-Williams</b> in the simulation toolbar dropdown<br>to edit the pipe roughness coefficient C.")
        );
    
    this->spin_roughness_dw->setToolTip(
        use_dw
            ? QStringLiteral("Absolute pipe roughness ε in mm<br>Used by the <b>Darcy-Weisbach</b> formula.")
            : QStringLiteral("Enable <b>Darcy-Weisbach</b> in the simulation toolbar dropdown<br>to edit the absolute pipe roughness ε.")
        );
    
    this->spin_roughness_cm->setToolTip(
        use_cm
            ? QStringLiteral("Manning roughness coefficient n<br>Used by the <b>Chezy-Manning</b> formula.")
            : QStringLiteral("Enable <b>Chezy-Manning</b> in the simulation toolbar dropdown<br>to edit the Manning roughness coefficient n.")
        );
}
