#include "entity_inspector_tank.h"

#include <QAbstractSpinBox>
#include <QGridLayout>
#include <QSignalBlocker>
#include <QtMath>

namespace
{
constexpr int volume_curve_uuid_role = Qt::UserRole;
constexpr double geometry_spin_minimum = -1000000000000000.0;
constexpr double geometry_spin_maximum = 1000000000000000.0;

void configureLengthSpin(QDoubleSpinBox *spin)
{
    spin->setRange(-1000000.0, 1000000.0);
    spin->setDecimals(3);
    spin->setSingleStep(0.10);
    spin->setSuffix(" m");
    spin->setGroupSeparatorShown(true);
    spin->setAlignment(Qt::AlignRight);
}

void configureAreaSpin(QDoubleSpinBox *spin)
{
    spin->setRange(geometry_spin_minimum, geometry_spin_maximum);
    spin->setDecimals(3);
    spin->setSingleStep(1.0);
    spin->setSuffix(" m²");
    spin->setGroupSeparatorShown(true);
    spin->setAlignment(Qt::AlignRight);
}

void configureVolumeSpin(QDoubleSpinBox *spin)
{
    spin->setRange(geometry_spin_minimum, geometry_spin_maximum);
    spin->setDecimals(3);
    spin->setSingleStep(1.0);
    spin->setSuffix(" m³");
    spin->setGroupSeparatorShown(true);
    spin->setAlignment(Qt::AlignRight);
}

std::optional<double> equivalentDiameter(double area_m2)
{
    if (!qIsFinite(area_m2) || area_m2 < 0.0)
        return std::nullopt;

    return qSqrt(4.0 * area_m2 / M_PI);
}
}

EntityInspectorTank::EntityInspectorTank(HydraulicData *hydraulic_data, const QUuid &uuid, QWidget *parent)
    : EntityInspectorWidget(hydraulic_data, parent),
      hydraulic_data(hydraulic_data),
      tank_uuid(uuid)
{
    addGroupOverviewImage(":/icon/tower_large.png", QString());
    addGroupGeneral(QString());
    addGroupPosition();
    bindHydraulicNode(InfrastructureEntity::Tank, uuid, "Tank");
    addGroupElevation();
    addGroupGeometry();
    bindGeometry();
    addGroupSimulation();
    addGroupQuality();
    addGroupAlerts();
    addGroupHistory();
    addStretches();
}

void EntityInspectorTank::addGroupGeometry()
{
    this->group_geometry = new GroupBoxCollapsible("Geometry / Levels");
    QGridLayout *grid = new QGridLayout(this->group_geometry);

    connect(this->group_geometry, &GroupBoxCollapsible::signalExpanded, this, &EntityInspectorTank::onGroupExpand);

    QLabel *label_level_initial = new QLabel("Initial Level");
    this->spin_level_initial = new QDoubleSpinBox();
    configureLengthSpin(this->spin_level_initial);

    QLabel *label_level_minimum = new QLabel("Minimum Level");
    this->spin_level_minimum = new QDoubleSpinBox();
    configureLengthSpin(this->spin_level_minimum);

    QLabel *label_level_maximum = new QLabel("Maximum Level");
    this->spin_level_maximum = new QDoubleSpinBox();
    configureLengthSpin(this->spin_level_maximum);

    this->check_overflow = new QCheckBox("Overflow Allowed");

    QLabel *label_geometry_type = new QLabel("Geometry Input");
    this->combo_geometry_type = new QComboBox();
    this->combo_geometry_type->addItem(
        "Cylindrical", static_cast<int>(HydraulicNodeTankGeometryInputType::Cylindrical));
    this->combo_geometry_type->addItem(
        "Uniform Area", static_cast<int>(HydraulicNodeTankGeometryInputType::UniformArea));
    this->combo_geometry_type->addItem(
        "Maximum Volume",
        static_cast<int>(HydraulicNodeTankGeometryInputType::VolumeAtMaximumLevel));
    this->combo_geometry_type->addItem(
        "Volume Curve", static_cast<int>(HydraulicNodeTankGeometryInputType::VolumeCurve));

    this->label_diameter = new QLabel("Diameter");
    this->spin_diameter = new QDoubleSpinBox();
    configureLengthSpin(this->spin_diameter);

    this->label_area = new QLabel("Cross-Section Area");
    this->label_area->setWordWrap(true);
    this->spin_area = new QDoubleSpinBox();
    configureAreaSpin(this->spin_area);

    this->label_volume_minimum = new QLabel("Minimum Volume");
    this->label_volume_minimum->setWordWrap(true);
    this->spin_volume_minimum = new QDoubleSpinBox();
    configureVolumeSpin(this->spin_volume_minimum);

    this->label_volume_maximum = new QLabel("Maximum Volume");
    this->label_volume_maximum->setWordWrap(true);
    this->spin_volume_maximum = new QDoubleSpinBox();
    configureVolumeSpin(this->spin_volume_maximum);

    this->label_volume_curve = new QLabel("Volume Curve");
    this->combo_volume_curve = new QComboBox();

    grid->addWidget(label_level_initial, 0, 0);
    grid->addWidget(this->spin_level_initial, 0, 1);
    grid->addWidget(label_level_minimum, 1, 0);
    grid->addWidget(this->spin_level_minimum, 1, 1);
    grid->addWidget(label_level_maximum, 2, 0);
    grid->addWidget(this->spin_level_maximum, 2, 1);
    grid->addWidget(this->check_overflow, 3, 0, 1, 2);
    grid->addWidget(label_geometry_type, 4, 0);
    grid->addWidget(this->combo_geometry_type, 4, 1);
    grid->addWidget(this->label_diameter, 5, 0);
    grid->addWidget(this->spin_diameter, 5, 1);
    grid->addWidget(this->label_area, 6, 0);
    grid->addWidget(this->spin_area, 6, 1);
    grid->addWidget(this->label_volume_minimum, 7, 0);
    grid->addWidget(this->spin_volume_minimum, 7, 1);
    grid->addWidget(this->label_volume_maximum, 8, 0);
    grid->addWidget(this->spin_volume_maximum, 8, 1);
    grid->addWidget(this->label_volume_curve, 9, 0);
    grid->addWidget(this->combo_volume_curve, 9, 1);

    layoutConfiguration()->addWidget(this->group_geometry);
}

void EntityInspectorTank::bindGeometry()
{
    connect(this->spin_level_initial, &QDoubleSpinBox::valueChanged, this, [this](double value_m)
    {
        this->hydraulic_data->setTankWaterLevelInitialM(this->tank_uuid, value_m);
    });
    connect(this->spin_level_minimum, &QDoubleSpinBox::valueChanged, this, [this](double value_m)
    {
        this->hydraulic_data->setTankWaterLevelMinimumM(this->tank_uuid, value_m);
    });
    connect(this->spin_level_maximum, &QDoubleSpinBox::valueChanged, this, [this](double value_m)
    {
        this->hydraulic_data->setTankWaterLevelMaximumM(this->tank_uuid, value_m);
    });
    connect(this->check_overflow, &QCheckBox::toggled, this, [this](bool can_overflow)
    {
        this->hydraulic_data->setTankCanOverflow(this->tank_uuid, can_overflow);
    });
    connect(this->combo_geometry_type, &QComboBox::currentIndexChanged, this, &EntityInspectorTank::onGeometryTypeChanged);
    connect(this->spin_diameter, &QDoubleSpinBox::valueChanged, this, [this](double diameter_m)
    {
        this->hydraulic_data->setTankDiameterM(this->tank_uuid, diameter_m);
    });
    connect(this->spin_area, &QDoubleSpinBox::valueChanged, this, [this](double area_m2)
    {
        this->hydraulic_data->setTankCrossSectionAreaM2(this->tank_uuid, area_m2);
    });
    connect(this->spin_volume_minimum, &QDoubleSpinBox::valueChanged, this, [this](double volume_m3)
    {
        this->hydraulic_data->setTankMinimumVolumeM3(this->tank_uuid, volume_m3);
    });
    connect(this->spin_volume_maximum, &QDoubleSpinBox::valueChanged, this, [this](double volume_m3)
    {
        this->hydraulic_data->setTankVolumeAtMaximumLevelM3(this->tank_uuid, volume_m3);
    });
    connect(this->combo_volume_curve, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const QUuid curve_uuid = this->combo_volume_curve->currentData(volume_curve_uuid_role).toUuid();
        this->hydraulic_data->setTankVolumeCurveUuid(this->tank_uuid, curve_uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this, [this](InfrastructureEntity entity_type, const QUuid &uuid)
    {
        if (entity_type == InfrastructureEntity::Tank && uuid == this->tank_uuid)
            refreshGeometry();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, &EntityInspectorTank::refreshGeometry);

    refreshGeometry();
}

void EntityInspectorTank::refreshGeometry()
{
    if (!this->hydraulic_data || this->tank_uuid.isNull())
        return;

    const std::optional<HydraulicNodeTank> tank = this->hydraulic_data->tank(this->tank_uuid);
    if (!tank.has_value())
        return;

    const QSignalBlocker initial_level_blocker(this->spin_level_initial);
    const QSignalBlocker minimum_level_blocker(this->spin_level_minimum);
    const QSignalBlocker maximum_level_blocker(this->spin_level_maximum);
    const QSignalBlocker overflow_blocker(this->check_overflow);
    const QSignalBlocker geometry_type_blocker(this->combo_geometry_type);
    const QSignalBlocker diameter_blocker(this->spin_diameter);
    const QSignalBlocker area_blocker(this->spin_area);
    const QSignalBlocker minimum_volume_blocker(this->spin_volume_minimum);
    const QSignalBlocker maximum_volume_blocker(this->spin_volume_maximum);

    this->spin_level_initial->setValue(tank->water_level_initial_m);
    this->spin_level_minimum->setValue(tank->water_level_minimum_m);
    this->spin_level_maximum->setValue(tank->water_level_maximum_m);
    this->check_overflow->setChecked(tank->can_overflow);

    const int geometry_index = this->combo_geometry_type->findData(
        static_cast<int>(tank->geometry_input_type));
    this->combo_geometry_type->setCurrentIndex(geometry_index >= 0 ? geometry_index : 0);

    this->spin_diameter->setValue(tank->diameter_m);
    this->spin_area->setValue(tank->cross_section_area_m2);
    this->spin_volume_minimum->setValue(tank->minimum_volume_m3);
    this->spin_volume_maximum->setValue(tank->volume_at_maximum_level_m3);

    populateVolumeCurveCombo(tank->volume_curve_uuid);
    updateGeometryModeUi(tank->geometry_input_type);
    updateDerivedGeometry(tank.value());
}

void EntityInspectorTank::populateVolumeCurveCombo(const QUuid &volume_curve_uuid)
{
    const QList<HydraulicCurveTankVolume> &curves =
        this->hydraulic_data->networkHydraulic().curves_tank_volume;

    QString signature = volume_curve_uuid.toString(QUuid::WithoutBraces);
    signature += QLatin1Char('\n');
    for (const HydraulicCurveTankVolume &curve : curves)
    {
        signature += curve.uuid.toString(QUuid::WithoutBraces);
        signature += QLatin1Char(':');
        signature += curve.id;
        signature += QLatin1Char('\n');
    }

    const QSignalBlocker curve_blocker(this->combo_volume_curve);
    if (this->combo_volume_curve->property("aowisVolumeCurveSignature").toString() != signature)
    {
        this->combo_volume_curve->clear();
        this->combo_volume_curve->addItem("[Select Curve]");
        this->combo_volume_curve->setItemData(
            this->combo_volume_curve->count() - 1, QUuid(), volume_curve_uuid_role);

        bool selected_curve_exists = volume_curve_uuid.isNull();
        for (const HydraulicCurveTankVolume &curve : curves)
        {
            const QString curve_name = curve.id.isEmpty()
                ? curve.uuid.toString(QUuid::WithoutBraces)
                : curve.id;
            this->combo_volume_curve->addItem(curve_name);
            this->combo_volume_curve->setItemData(
                this->combo_volume_curve->count() - 1, curve.uuid, volume_curve_uuid_role);
            if (curve.uuid == volume_curve_uuid)
                selected_curve_exists = true;
        }

        if (!volume_curve_uuid.isNull() && !selected_curve_exists)
        {
            this->combo_volume_curve->addItem(
                QStringLiteral("[Missing Curve] %1").arg(
                    volume_curve_uuid.toString(QUuid::WithoutBraces)));
            this->combo_volume_curve->setItemData(
                this->combo_volume_curve->count() - 1,
                volume_curve_uuid, volume_curve_uuid_role);
        }

        this->combo_volume_curve->setProperty("aowisVolumeCurveSignature", signature);
    }

    int selected_index = 0;
    for (int index = 0; index < this->combo_volume_curve->count(); index++)
    {
        if (this->combo_volume_curve->itemData(index, volume_curve_uuid_role).toUuid() ==
            volume_curve_uuid)
        {
            selected_index = index;
            break;
        }
    }
    this->combo_volume_curve->setCurrentIndex(selected_index);
}

void EntityInspectorTank::updateGeometryModeUi(
    HydraulicNodeTankGeometryInputType geometry_input_type)
{
    const bool is_cylindrical =
        geometry_input_type == HydraulicNodeTankGeometryInputType::Cylindrical;
    const bool is_uniform_area =
        geometry_input_type == HydraulicNodeTankGeometryInputType::UniformArea;
    const bool is_maximum_volume =
        geometry_input_type == HydraulicNodeTankGeometryInputType::VolumeAtMaximumLevel;
    const bool is_volume_curve =
        geometry_input_type == HydraulicNodeTankGeometryInputType::VolumeCurve;
    const bool group_expanded = !this->group_geometry->isCollapsed();

    this->label_diameter->setVisible(group_expanded && !is_volume_curve);
    this->spin_diameter->setVisible(group_expanded && !is_volume_curve);
    this->label_area->setVisible(group_expanded && !is_volume_curve);
    this->spin_area->setVisible(group_expanded && !is_volume_curve);
    this->label_volume_minimum->setVisible(group_expanded);
    this->spin_volume_minimum->setVisible(group_expanded);
    this->label_volume_maximum->setVisible(group_expanded);
    this->spin_volume_maximum->setVisible(group_expanded);
    this->label_volume_curve->setVisible(group_expanded && is_volume_curve);
    this->combo_volume_curve->setVisible(group_expanded && is_volume_curve);

    this->label_diameter->setText(is_cylindrical ? "Diameter" : "Equivalent<br>Diameter");
    this->label_area->setText(is_uniform_area ? "Cross-Section Area" : "Derived Cross-Section Area");

    if (is_cylindrical)
    {
        setSpinEditable(this->spin_diameter, "Stored cylindrical tank diameter.");
        setSpinDerived(this->spin_area, std::nullopt);
        setSpinEditable(this->spin_volume_minimum,
                        "A positive value is the explicit volume at minimum level. "
                        "Zero lets EPANET derive it from area × minimum level.");
        setSpinDerived(this->spin_volume_maximum, std::nullopt);
    }
    else if (is_uniform_area)
    {
        setSpinDerived(this->spin_diameter, std::nullopt);
        setSpinEditable(this->spin_area, "Stored uniform cross-sectional tank area.");
        setSpinEditable(this->spin_volume_minimum,
                        "A positive value is the explicit volume at minimum level. "
                        "Zero lets EPANET derive it from area × minimum level.");
        setSpinDerived(this->spin_volume_maximum, std::nullopt);
    }
    else if (is_maximum_volume)
    {
        setSpinDerived(this->spin_diameter, std::nullopt);
        setSpinDerived(this->spin_area, std::nullopt);
        setSpinEditable(this->spin_volume_minimum,
                        "A positive value is the explicit volume at minimum level. "
                        "Zero derives geometry from maximum volume and maximum level.");
        setSpinEditable(this->spin_volume_maximum, "Stored tank volume at the maximum water level.");
    }
    else if (is_volume_curve)
    {
        setSpinDerived(this->spin_volume_minimum, std::nullopt);
        setSpinDerived(this->spin_volume_maximum, std::nullopt);
    }
}

void EntityInspectorTank::updateDerivedGeometry(const HydraulicNodeTank &tank)
{
    const double usable_height_m = tank.water_level_maximum_m - tank.water_level_minimum_m;

    switch (tank.geometry_input_type)
    {
    case HydraulicNodeTankGeometryInputType::Cylindrical:
    {
        std::optional<double> area_m2;
        std::optional<double> maximum_volume_m3;
        if (qIsFinite(tank.diameter_m) && tank.diameter_m >= 0.0 &&
            qIsFinite(tank.minimum_volume_m3) && tank.minimum_volume_m3 >= 0.0)
        {
            area_m2 = M_PI * tank.diameter_m * tank.diameter_m / 4.0;
            const double effective_minimum_volume_m3 = tank.minimum_volume_m3 > 0.0
                ? tank.minimum_volume_m3
                : area_m2.value() * tank.water_level_minimum_m;
            const double derived_volume_m3 =
                effective_minimum_volume_m3 + area_m2.value() * usable_height_m;
            if (qIsFinite(derived_volume_m3))
                maximum_volume_m3 = derived_volume_m3;
        }
        setSpinDerived(this->spin_area, area_m2, "Calculated from the cylindrical diameter.");
        setSpinDerived(this->spin_volume_maximum, maximum_volume_m3,
                       "Calculated using EPANET minimum-volume semantics and the maximum level.");
        break;
    }
    case HydraulicNodeTankGeometryInputType::UniformArea:
    {
        const std::optional<double> diameter_m = equivalentDiameter(tank.cross_section_area_m2);
        std::optional<double> maximum_volume_m3;
        if (qIsFinite(tank.cross_section_area_m2) && tank.cross_section_area_m2 >= 0.0 &&
            qIsFinite(tank.minimum_volume_m3) && tank.minimum_volume_m3 >= 0.0)
        {
            const double effective_minimum_volume_m3 = tank.minimum_volume_m3 > 0.0
                ? tank.minimum_volume_m3
                : tank.cross_section_area_m2 * tank.water_level_minimum_m;
            const double derived_volume_m3 =
                effective_minimum_volume_m3 + tank.cross_section_area_m2 * usable_height_m;
            if (qIsFinite(derived_volume_m3))
                maximum_volume_m3 = derived_volume_m3;
        }
        setSpinDerived(this->spin_diameter, diameter_m,
                       "Equivalent cylindrical diameter calculated from the stored area.");
        setSpinDerived(this->spin_volume_maximum, maximum_volume_m3,
                       "Calculated using EPANET minimum-volume semantics and the maximum level.");
        break;
    }
    case HydraulicNodeTankGeometryInputType::VolumeAtMaximumLevel:
    {
        std::optional<double> area_m2;
        double reference_height_m = 0.0;
        double reference_volume_m3 = 0.0;
        if (tank.minimum_volume_m3 > 0.0)
        {
            reference_height_m = usable_height_m;
            reference_volume_m3 =
                tank.volume_at_maximum_level_m3 - tank.minimum_volume_m3;
        }
        else if (tank.minimum_volume_m3 == 0.0)
        {
            reference_height_m = tank.water_level_maximum_m;
            reference_volume_m3 = tank.volume_at_maximum_level_m3;
        }

        if (qIsFinite(reference_height_m) && !qFuzzyIsNull(reference_height_m) &&
            qIsFinite(reference_volume_m3))
        {
            const double derived_area_m2 = reference_volume_m3 / reference_height_m;
            if (qIsFinite(derived_area_m2))
                area_m2 = derived_area_m2;
        }
        setSpinDerived(this->spin_area, area_m2,
                       "Calculated from the minimum/maximum volumes and levels.");
        setSpinDerived(this->spin_diameter,
                       area_m2.has_value() ? equivalentDiameter(area_m2.value()) : std::nullopt,
                       "Equivalent cylindrical diameter calculated from the derived area.");
        break;
    }
    case HydraulicNodeTankGeometryInputType::VolumeCurve:
    {
        const std::optional<double> minimum_volume_m3 =
            volumeCurveVolumeAtLevel(tank.volume_curve_uuid, tank.water_level_minimum_m);
        const std::optional<double> maximum_volume_m3 =
            volumeCurveVolumeAtLevel(tank.volume_curve_uuid, tank.water_level_maximum_m);
        setSpinDerived(this->spin_volume_minimum, minimum_volume_m3,
                       "Interpolated from the selected volume curve at the minimum level.");
        setSpinDerived(this->spin_volume_maximum, maximum_volume_m3,
                       "Interpolated from the selected volume curve at the maximum level.");
        break;
    }
    }
}

std::optional<double> EntityInspectorTank::volumeCurveVolumeAtLevel(
    const QUuid &curve_uuid, double water_level_m) const
{
    if (curve_uuid.isNull() || !qIsFinite(water_level_m))
        return std::nullopt;

    const QList<HydraulicCurveTankVolume> &curves =
        this->hydraulic_data->networkHydraulic().curves_tank_volume;
    const HydraulicCurveTankVolume *selected_curve = nullptr;
    for (const HydraulicCurveTankVolume &curve : curves)
    {
        if (curve.uuid == curve_uuid)
        {
            selected_curve = &curve;
            break;
        }
    }
    if (!selected_curve || selected_curve->points.size() < 2)
        return std::nullopt;

    const QList<HydraulicCurveTankVolumePoint> &points = selected_curve->points;
    for (int index = 0; index < points.size(); index++)
    {
        const HydraulicCurveTankVolumePoint &point = points.at(index);
        if (!qIsFinite(point.water_level_m) || !qIsFinite(point.volume_m3))
            return std::nullopt;
        if (index > 0)
        {
            const HydraulicCurveTankVolumePoint &previous = points.at(index - 1);
            if (point.water_level_m <= previous.water_level_m ||
                point.volume_m3 <= previous.volume_m3)
                return std::nullopt;
        }
        if (qFuzzyCompare(1.0 + point.water_level_m, 1.0 + water_level_m))
            return point.volume_m3;
    }

    if (water_level_m < points.first().water_level_m ||
        water_level_m > points.last().water_level_m)
        return std::nullopt;

    for (int index = 1; index < points.size(); index++)
    {
        const HydraulicCurveTankVolumePoint &lower = points.at(index - 1);
        const HydraulicCurveTankVolumePoint &upper = points.at(index);
        if (water_level_m > upper.water_level_m)
            continue;

        const double level_difference_m = upper.water_level_m - lower.water_level_m;
        if (qFuzzyIsNull(level_difference_m))
            return std::nullopt;

        const double position = (water_level_m - lower.water_level_m) / level_difference_m;
        const double volume_m3 =
            lower.volume_m3 + position * (upper.volume_m3 - lower.volume_m3);
        return qIsFinite(volume_m3) ? std::optional<double>(volume_m3) : std::nullopt;
    }

    return std::nullopt;
}

void EntityInspectorTank::setSpinEditable(QDoubleSpinBox *spin, const QString &tooltip)
{
    spin->setReadOnly(false);
    spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    spin->setSpecialValueText(QString());
    spin->setToolTip(tooltip);
}

void EntityInspectorTank::setSpinDerived(
    QDoubleSpinBox *spin, const std::optional<double> &value, const QString &tooltip)
{
    const QSignalBlocker blocker(spin);
    spin->setReadOnly(true);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setToolTip(tooltip);

    if (value.has_value() && qIsFinite(value.value()))
    {
        spin->setSpecialValueText(QString());
        spin->setValue(value.value());
        return;
    }

    spin->setSpecialValueText("[Not available]");
    spin->setValue(spin->minimum());
}

HydraulicNodeTankGeometryInputType EntityInspectorTank::tankGeometryInputType() const
{
    return static_cast<HydraulicNodeTankGeometryInputType>(
        this->combo_geometry_type->currentData().toInt());
}

void EntityInspectorTank::addGroupQuality()
{
    addGroupNodeQualityInputs();

    GroupBoxCollapsible *group = new GroupBoxCollapsible("Tank Mixing & Reactions");
    QGridLayout *grid = new QGridLayout(group);
    int row = 0;

    QLabel *label_mixing_model = new QLabel("Mixing Model");
    this->combo_quality_mixing = new QComboBox();
    this->combo_quality_mixing->addItem("Complete Mix", static_cast<int>(HydraulicNodeTankMixingModel::CompleteMix));
    this->combo_quality_mixing->addItem("Two-Compartment", static_cast<int>(HydraulicNodeTankMixingModel::TwoCompartment));
    this->combo_quality_mixing->addItem("First In, First Out", static_cast<int>(HydraulicNodeTankMixingModel::FirstInFirstOut));
    this->combo_quality_mixing->addItem("Last In, First Out", static_cast<int>(HydraulicNodeTankMixingModel::LastInFirstOut));

    QLabel *label_mixing_fraction = new QLabel("Mixing Fraction");
    this->spin_quality_mixing_fraction = new QDoubleSpinBox();
    this->spin_quality_mixing_fraction->setRange(0.0, 1.0);
    this->spin_quality_mixing_fraction->setDecimals(4);
    this->spin_quality_mixing_fraction->setSingleStep(0.05);
    this->spin_quality_mixing_fraction->setToolTip(
        "Fraction of tank volume assigned to the inlet/outlet compartment for the two-compartment model.");

    this->check_quality_override_bulk_reaction = new QCheckBox("Override global tank bulk reaction");

    QLabel *label_bulk_coefficient = new QLabel("Bulk Reaction Coefficient");
    label_bulk_coefficient->setWordWrap(true);
    this->spin_quality_bulk_reaction_coefficient = new QDoubleSpinBox();
    this->spin_quality_bulk_reaction_coefficient->setRange(-1000000.0, 1000000.0);
    this->spin_quality_bulk_reaction_coefficient->setDecimals(8);
    this->spin_quality_bulk_reaction_coefficient->setToolTip(
        "Coefficient dimensions depend on the network-wide tank bulk reaction order.");



    grid->addWidget(label_mixing_model, row, 0);
    grid->addWidget(this->combo_quality_mixing, row++, 1);
    grid->addWidget(label_mixing_fraction, row, 0);
    grid->addWidget(this->spin_quality_mixing_fraction, row++, 1);
    grid->addWidget(this->check_quality_override_bulk_reaction, row++, 0, 1, 2);
    grid->addWidget(label_bulk_coefficient, row, 0);
    grid->addWidget(this->spin_quality_bulk_reaction_coefficient, row++, 1);

    connect(this->combo_quality_mixing, &QComboBox::currentIndexChanged, this, [this](int)
    {
        const HydraulicNodeTankMixingModel mixing_model = static_cast<HydraulicNodeTankMixingModel>(
            this->combo_quality_mixing->currentData().toInt());
        this->hydraulic_data->setTankQualityMixingModel(this->tank_uuid, mixing_model);
        updateQualityUi();
    });
    connect(this->spin_quality_mixing_fraction, &QDoubleSpinBox::valueChanged,
            this, [this](double mixing_fraction)
    {
        this->hydraulic_data->setTankQualityMixingFraction(this->tank_uuid, mixing_fraction);
    });
    connect(this->check_quality_override_bulk_reaction, &QCheckBox::toggled,
            this, [this](bool enabled)
    {
        this->hydraulic_data->setTankOverrideBulkReaction(this->tank_uuid, enabled);
        updateQualityUi();
    });
    connect(this->spin_quality_bulk_reaction_coefficient, &QDoubleSpinBox::valueChanged,
            this, [this](double coefficient)
    {
        this->hydraulic_data->setTankBulkReactionCoefficient(this->tank_uuid, coefficient);
    });


    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this,
            [this](InfrastructureEntity entity_type, const QUuid &uuid)
    {
        if (entity_type == InfrastructureEntity::Tank && uuid == this->tank_uuid)
            refreshQuality();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded,
            this, &EntityInspectorTank::refreshQuality);

    layoutQuality()->addWidget(group);
    refreshQuality();
}

void EntityInspectorTank::refreshQuality()
{
    const std::optional<HydraulicNodeTank> tank = this->hydraulic_data->tank(this->tank_uuid);
    if (!tank.has_value())
        return;

    const QSignalBlocker mixing_blocker(this->combo_quality_mixing);
    const QSignalBlocker fraction_blocker(this->spin_quality_mixing_fraction);
    const QSignalBlocker override_blocker(this->check_quality_override_bulk_reaction);
    const QSignalBlocker coefficient_blocker(this->spin_quality_bulk_reaction_coefficient);

    const int mixing_index = this->combo_quality_mixing->findData(static_cast<int>(tank->mixing_model));
    this->combo_quality_mixing->setCurrentIndex(mixing_index >= 0 ? mixing_index : 0);
    this->spin_quality_mixing_fraction->setValue(tank->mixing_fraction);
    this->check_quality_override_bulk_reaction->setChecked(tank->override_bulk_reaction);
    this->spin_quality_bulk_reaction_coefficient->setValue(tank->bulk_reaction.coefficient);
    updateQualityUi();
}

void EntityInspectorTank::updateQualityUi()
{
    const HydraulicNodeTankMixingModel mixing_model = static_cast<HydraulicNodeTankMixingModel>(
        this->combo_quality_mixing->currentData().toInt());
    this->spin_quality_mixing_fraction->setEnabled(
        mixing_model == HydraulicNodeTankMixingModel::TwoCompartment);

    const bool override_reaction = this->check_quality_override_bulk_reaction->isChecked();
    this->spin_quality_bulk_reaction_coefficient->setEnabled(override_reaction);
}

void EntityInspectorTank::onGroupExpand(GroupBoxCollapsible *group)
{
    if (group == this->group_geometry)
        refreshGeometry();
}

void EntityInspectorTank::onGeometryTypeChanged(int index)
{
    Q_UNUSED(index)

    const HydraulicNodeTankGeometryInputType geometry_input_type = tankGeometryInputType();
    updateGeometryModeUi(geometry_input_type);
    this->hydraulic_data->setTankGeometryInputType(this->tank_uuid, geometry_input_type);
}
