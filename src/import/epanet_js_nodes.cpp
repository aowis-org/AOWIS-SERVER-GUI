#include "import/epanet_js_nodes.h"
#include "import/epanet_js_conversion_common.h"
#include "import/epanet_js_schema.h"
#include "import/epanet_js_units.h"

#include <algorithm>
#include <QHash>
#include <QSet>

namespace EpanetJsNodes
{
using namespace EpanetJsConversionCommon;
using namespace EpanetJsSchema;
using namespace EpanetJsUnits;

bool importLengthValue(
    const QVariantMap &row,
    const QString &column,
    const QString &unit,
    double &target,
    const QString &table_name,
    qint64 source_id,
    EpanetJsProjectConversionResult &result,
    bool required = false)
{
    if (!row.contains(column) || row.value(column).isNull())
    {
        if (required)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("missing-node-value"),
                QStringLiteral("epanet-js %1 id %2 is missing required field '%3'.")
                    .arg(table_name)
                    .arg(source_id)
                    .arg(column),
                table_name,
                source_id);
            return false;
        }
        return true;
    }

    const std::optional<double> value = finiteVariantDouble(row.value(column));
    const std::optional<double> converted = value.has_value() ? lengthToM(*value, unit) : std::nullopt;
    if (!converted.has_value())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("unsupported-node-length"),
            QStringLiteral("AOWIS cannot convert epanet-js %1 id %2 field '%3' with unit '%4'.")
                .arg(table_name)
                .arg(source_id)
                .arg(column)
                .arg(unit),
            table_name,
            source_id);
        return false;
    }
    target = *converted;
    return true;
}

bool importVolumeValue(
    const QVariantMap &row,
    const QString &column,
    const QString &unit,
    double &target,
    const QString &table_name,
    qint64 source_id,
    EpanetJsProjectConversionResult &result)
{
    if (!row.contains(column) || row.value(column).isNull())
        return true;

    const std::optional<double> value = finiteVariantDouble(row.value(column));
    const std::optional<double> converted = value.has_value() ? volumeToM3(*value, unit) : std::nullopt;
    if (!converted.has_value())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("unsupported-node-volume"),
            QStringLiteral("AOWIS cannot convert epanet-js %1 id %2 field '%3' with unit '%4'.")
                .arg(table_name)
                .arg(source_id)
                .arg(column)
                .arg(unit),
            table_name,
            source_id);
        return false;
    }
    target = *converted;
    return true;
}








void importJunctions(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("junctions"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString elevation_unit = firstUnit(
        units, QStringList{QStringLiteral("elevation"), QStringLiteral("head"), QStringLiteral("level")});

    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value() || !result.id_map.contains(QStringLiteral("junctions"), *source_id))
            continue;

        HydraulicNodeJunction junction;
        junction.id = importedEntityId(row, QStringLiteral("junctions"), *source_id, result);
        junction.uuid = result.id_map.uuidFor(QStringLiteral("junctions"), *source_id);
        junction.metadata.enabled = !row.contains(QStringLiteral("is_active"))
            || row.value(QStringLiteral("is_active")).toInt() != 0;
        junction.elevation_input_type = HydraulicNodeElevationInputType::TotalElevation;

        importStoredWgs84Coordinate(
            row, QStringLiteral("junctions"), *source_id,
            junction.coordinate_wgs84, result);
        importLengthValue(
            row, QStringLiteral("elevation"), elevation_unit, junction.elevation_m,
            QStringLiteral("junctions"), *source_id, result);

        result.network.nodes_junctions.append(junction);
    }
}

void importReservoirs(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("reservoirs"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString head_unit = firstUnit(
        units, QStringList{QStringLiteral("head"), QStringLiteral("elevation"), QStringLiteral("level")});

    const QSet<QUuid> pattern_uuids = uuidMembershipIndex(result.network.patterns_time);

    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value() || !result.id_map.contains(QStringLiteral("reservoirs"), *source_id))
            continue;

        HydraulicNodeReservoir reservoir;
        reservoir.id = importedEntityId(row, QStringLiteral("reservoirs"), *source_id, result);
        reservoir.uuid = result.id_map.uuidFor(QStringLiteral("reservoirs"), *source_id);
        reservoir.metadata.enabled = !row.contains(QStringLiteral("is_active"))
            || row.value(QStringLiteral("is_active")).toInt() != 0;
        reservoir.head_input_type = HydraulicNodeElevationInputType::TotalHead;

        importStoredWgs84Coordinate(
            row, QStringLiteral("reservoirs"), *source_id,
            reservoir.coordinate_wgs84, result);
        importLengthValue(
            row, QStringLiteral("head"), head_unit, reservoir.hydraulic_head_m,
            QStringLiteral("reservoirs"), *source_id, result, true);

        if (row.contains(QStringLiteral("head_pattern_id"))
            && !row.value(QStringLiteral("head_pattern_id")).isNull())
        {
            const std::optional<qint64> pattern_id = integerValue(
                row.value(QStringLiteral("head_pattern_id")));
            const QUuid pattern_uuid = pattern_id.has_value()
                ? result.id_map.uuidFor(QStringLiteral("patterns"), *pattern_id)
                : QUuid();
            if (pattern_id.has_value() && *pattern_id <= 0)
            {
                // EPANET-style zero references mean no pattern.
            }
            else if (!pattern_id.has_value() || pattern_uuid.isNull()
                || !pattern_uuids.contains(pattern_uuid))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-reservoir-pattern-reference"),
                    QStringLiteral("epanet-js reservoir id %1 references a missing head pattern.")
                        .arg(*source_id),
                    QStringLiteral("reservoirs"),
                    *source_id);
            }
            else
            {
                reservoir.head_pattern_mode = HydraulicTimePatternMode::TimePattern;
                reservoir.head_pattern_uuid = pattern_uuid;
            }
        }

        result.network.nodes_reservoirs.append(reservoir);
    }
}

void importTanks(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("tanks"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString elevation_unit = firstUnit(
        units, QStringList{QStringLiteral("elevation"), QStringLiteral("head"), QStringLiteral("level")});
    const QString initial_level_unit = firstUnit(
        units, QStringList{QStringLiteral("initialLevel"), QStringLiteral("level"), QStringLiteral("elevation")});
    const QString minimum_level_unit = firstUnit(
        units, QStringList{QStringLiteral("minLevel"), QStringLiteral("level"), QStringLiteral("elevation")});
    const QString maximum_level_unit = firstUnit(
        units, QStringList{QStringLiteral("maxLevel"), QStringLiteral("level"), QStringLiteral("elevation")});
    const QString diameter_unit = firstUnit(
        units, QStringList{QStringLiteral("tankDiameter"), QStringLiteral("length"), QStringLiteral("elevation")});
    const QString volume_unit = firstUnit(
        units, QStringList{QStringLiteral("minVolume"), QStringLiteral("volume")});

    constexpr double pi = 3.141592653589793238462643383279502884;

    const QSet<QUuid> volume_curve_uuids = uuidMembershipIndex(result.network.curves_tank_volume);

    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value() || !result.id_map.contains(QStringLiteral("tanks"), *source_id))
            continue;

        HydraulicNodeTank tank;
        tank.id = importedEntityId(row, QStringLiteral("tanks"), *source_id, result);
        tank.uuid = result.id_map.uuidFor(QStringLiteral("tanks"), *source_id);
        tank.metadata.enabled = !row.contains(QStringLiteral("is_active"))
            || row.value(QStringLiteral("is_active")).toInt() != 0;
        tank.elevation_input_type = HydraulicNodeTankElevationInputType::BottomElevation;
        tank.geometry_input_type = HydraulicNodeTankGeometryInputType::Cylindrical;

        importStoredWgs84Coordinate(
            row, QStringLiteral("tanks"), *source_id,
            tank.coordinate_wgs84, result);
        importLengthValue(
            row, QStringLiteral("elevation"), elevation_unit, tank.bottom_elevation_m,
            QStringLiteral("tanks"), *source_id, result, true);
        importLengthValue(
            row, QStringLiteral("initial_level"), initial_level_unit, tank.water_level_initial_m,
            QStringLiteral("tanks"), *source_id, result, true);
        importLengthValue(
            row, QStringLiteral("min_level"), minimum_level_unit, tank.water_level_minimum_m,
            QStringLiteral("tanks"), *source_id, result, true);
        importLengthValue(
            row, QStringLiteral("max_level"), maximum_level_unit, tank.water_level_maximum_m,
            QStringLiteral("tanks"), *source_id, result, true);
        importLengthValue(
            row, QStringLiteral("diameter"), diameter_unit, tank.diameter_m,
            QStringLiteral("tanks"), *source_id, result);
        importVolumeValue(
            row, QStringLiteral("min_volume"), volume_unit, tank.minimum_volume_m3,
            QStringLiteral("tanks"), *source_id, result);

        if (tank.diameter_m > 0.0)
        {
            tank.cross_section_area_m2 = pi * tank.diameter_m * tank.diameter_m / 4.0;
            tank.volume_at_maximum_level_m3 = tank.minimum_volume_m3
                + tank.cross_section_area_m2
                    * (tank.water_level_maximum_m - tank.water_level_minimum_m);
        }

        if (row.contains(QStringLiteral("volume_curve_id"))
            && !row.value(QStringLiteral("volume_curve_id")).isNull())
        {
            const std::optional<qint64> curve_id = integerValue(
                row.value(QStringLiteral("volume_curve_id")));
            const QUuid curve_uuid = curve_id.has_value()
                ? result.id_map.uuidFor(QStringLiteral("curves"), *curve_id)
                : QUuid();
            if (curve_id.has_value() && *curve_id <= 0)
            {
                // EPANET-style zero references mean no volume curve.
            }
            else if (!curve_id.has_value() || curve_uuid.isNull()
                || !volume_curve_uuids.contains(curve_uuid))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-tank-volume-curve-reference"),
                    QStringLiteral("epanet-js tank id %1 references a missing or non-volume curve.")
                        .arg(*source_id),
                    QStringLiteral("tanks"),
                    *source_id);
            }
            else
            {
                tank.geometry_input_type = HydraulicNodeTankGeometryInputType::VolumeCurve;
                tank.volume_curve_uuid = curve_uuid;
            }
        }

        if (row.contains(QStringLiteral("overflow")) && !row.value(QStringLiteral("overflow")).isNull())
            tank.can_overflow = row.value(QStringLiteral("overflow")).toInt() != 0;

        result.network.nodes_tanks.append(tank);
    }
}



void importNodes(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    importJunctions(project, project_settings, result);
    importReservoirs(project, project_settings, result);
    importTanks(project, project_settings, result);
    importJunctionDemands(project, project_settings, result);
}


void importJunctionDemands(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("junction_demands"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString demand_unit = firstUnit(
        units, QStringList{QStringLiteral("baseDemand"), QStringLiteral("flow")});

    struct SourceDemand
    {
        qint64 junction_id = 0;
        qint64 ordinal = 0;
        QVariantMap row;
    };

    QList<SourceDemand> demands;
    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> junction_id = integerValue(
            row.value(QStringLiteral("junction_id")));
        const std::optional<qint64> ordinal = integerValue(row.value(QStringLiteral("ordinal")));
        if (!junction_id.has_value() || !ordinal.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-junction-demand-reference"),
                QStringLiteral("epanet-js junction_demands contains an invalid junction_id or ordinal."),
                QStringLiteral("junction_demands"));
            continue;
        }
        SourceDemand source;
        source.junction_id = *junction_id;
        source.ordinal = *ordinal;
        source.row = row;
        demands.append(source);
    }

    const QSet<QUuid> pattern_uuids = uuidMembershipIndex(result.network.patterns_time);

    const QHash<QUuid, qsizetype> junction_positions = uuidPositionIndex(result.network.nodes_junctions);

    std::sort(
        demands.begin(), demands.end(),
        [](const SourceDemand &left, const SourceDemand &right)
        {
            if (left.junction_id != right.junction_id)
                return left.junction_id < right.junction_id;
            return left.ordinal < right.ordinal;
        });

    for (const SourceDemand &source : demands)
    {
        const QUuid junction_uuid = result.id_map.uuidFor(
            QStringLiteral("junctions"), source.junction_id);
        const auto junction_position = junction_positions.constFind(junction_uuid);
        if (junction_position == junction_positions.cend())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("missing-junction-demand-reference"),
                QStringLiteral("epanet-js junction demand references missing junction id %1.")
                    .arg(source.junction_id),
                QStringLiteral("junction_demands"),
                source.junction_id);
            continue;
        }

        const std::optional<double> source_base_demand = finiteVariantDouble(
            source.row.value(QStringLiteral("base_demand")));
        const std::optional<double> base_demand = source_base_demand.has_value()
            ? flowToM3PerH(*source_base_demand, demand_unit)
            : std::nullopt;
        if (!base_demand.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unsupported-junction-demand"),
                QStringLiteral("AOWIS cannot convert epanet-js junction %1 demand ordinal %2 with unit '%3'.")
                    .arg(source.junction_id)
                    .arg(source.ordinal)
                    .arg(demand_unit),
                QStringLiteral("junction_demands"),
                source.junction_id);
            continue;
        }

        HydraulicDemand demand;
        demand.base_demand_m3_per_h = *base_demand;

        if (source.row.contains(QStringLiteral("pattern_id"))
            && !source.row.value(QStringLiteral("pattern_id")).isNull())
        {
            const std::optional<qint64> pattern_id = integerValue(
                source.row.value(QStringLiteral("pattern_id")));
            const QUuid pattern_uuid = pattern_id.has_value()
                ? result.id_map.uuidFor(QStringLiteral("patterns"), *pattern_id)
                : QUuid();
            if (pattern_id.has_value() && *pattern_id <= 0)
            {
                // EPANET-style zero references mean no pattern.
            }
            else if (!pattern_id.has_value() || pattern_uuid.isNull()
                || !pattern_uuids.contains(pattern_uuid))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-demand-pattern-reference"),
                    QStringLiteral("epanet-js junction %1 demand ordinal %2 references a missing pattern.")
                        .arg(source.junction_id)
                        .arg(source.ordinal),
                    QStringLiteral("junction_demands"),
                    source.junction_id);
            }
            else
            {
                demand.pattern_mode = HydraulicTimePatternMode::TimePattern;
                demand.pattern_uuid = pattern_uuid;
            }
        }

        result.network.nodes_junctions[*junction_position].demands.append(demand);
    }
}


void applyEmitterSettings(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    const QJsonObject &simulation_settings,
    EpanetJsProjectConversionResult &result)
{
    double pressure_exponent = 0.5;
    if (simulation_settings.contains(QStringLiteral("emitterExponent")))
    {
        const std::optional<double> value = finiteDouble(
            simulation_settings.value(QStringLiteral("emitterExponent")));
        if (!value.has_value() || *value <= 0.0)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-emitter-exponent"),
                QStringLiteral("epanet-js emitter exponent must be a finite positive value."),
                QStringLiteral("simulation_settings"));
            return;
        }
        pressure_exponent = *value;
    }

    for (HydraulicNodeJunction &junction : result.network.nodes_junctions)
        junction.emitter.pressure_exponent = pressure_exponent;

    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("junctions"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString flow_unit = firstUnit(
        units, QStringList{QStringLiteral("flow"), QStringLiteral("baseDemand")});
    const QString pressure_unit = firstUnit(
        units, QStringList{QStringLiteral("pressure"), QStringLiteral("head")});

    const QHash<QUuid, qsizetype> junction_positions = uuidPositionIndex(result.network.nodes_junctions);

    for (const QVariantMap &row : table->rows)
    {
        if (!row.contains(QStringLiteral("emitter_coefficient"))
            || row.value(QStringLiteral("emitter_coefficient")).isNull())
            continue;

        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        const std::optional<double> source_coefficient = finiteVariantDouble(
            row.value(QStringLiteral("emitter_coefficient")));
        if (!source_id.has_value() || !source_coefficient.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-emitter-coefficient"),
                QStringLiteral("epanet-js junction emitter coefficient is not a finite number."),
                QStringLiteral("junctions"),
                source_id);
            continue;
        }

        const QUuid junction_uuid = result.id_map.uuidFor(QStringLiteral("junctions"), *source_id);
        const auto junction_position = junction_positions.constFind(junction_uuid);
        if (junction_position == junction_positions.cend())
            continue;
        HydraulicNodeJunction &junction = result.network.nodes_junctions[*junction_position];

        const std::optional<double> canonical = emitterCoefficientToCanonical(
            *source_coefficient,
            pressure_exponent,
            flow_unit,
            pressure_unit,
            result.network.options_hydraulic.specific_gravity);
        if (!canonical.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unsupported-emitter-coefficient-unit"),
                QStringLiteral("AOWIS cannot convert epanet-js junction %1 emitter coefficient using flow unit '%2', pressure unit '%3', and emitter exponent %4.")
                    .arg(*source_id)
                    .arg(flow_unit, pressure_unit)
                    .arg(pressure_exponent),
                QStringLiteral("junctions"),
                *source_id);
            continue;
        }

        junction.emitter.coefficient = *canonical;
    }
}

}
