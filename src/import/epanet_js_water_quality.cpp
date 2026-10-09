#include "import/epanet_js_water_quality.h"
#include "import/epanet_js_conversion_common.h"
#include "import/epanet_js_schema.h"
#include "import/epanet_js_units.h"
#include <aowis/model/units/conversion.h>

#include <QHash>
#include <QJsonObject>
#include <QVariant>
#include <cmath>
#include <limits>
#include <optional>

namespace EpanetJsWaterQuality
{
using namespace EpanetJsConversionCommon;
using namespace EpanetJsSchema;
using namespace EpanetJsUnits;

namespace
{
QString normalizedQualityToken(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QLatin1Char(' '));
    value.remove(QLatin1Char('_'));
    value.remove(QLatin1Char('-'));
    return value;
}

WaterQualityAnalysisType epanetJsQualityAnalysis(const QJsonObject &simulation_settings)
{
    const QString mode = normalizedQualityToken(
        simulation_settings.value(QStringLiteral("qualitySimulationType")).toString());
    if (mode == QStringLiteral("chemical") || mode == QStringLiteral("chem"))
        return WaterQualityAnalysisType::Chemical;
    if (mode == QStringLiteral("age") || mode == QStringLiteral("waterage"))
        return WaterQualityAnalysisType::WaterAge;
    if (mode == QStringLiteral("trace") || mode == QStringLiteral("sourcetrace"))
        return WaterQualityAnalysisType::SourceTrace;
    return WaterQualityAnalysisType::None;
}

double reactionCoefficientScaleToCanonicalMg(
    double chemical_scale_to_canonical_mg,
    double reaction_order)
{
    const double dimensional_order = reaction_order < 0.0 ? 0.0 : reaction_order;
    return std::pow(chemical_scale_to_canonical_mg, 1.0 - dimensional_order);
}

std::optional<double> wallReactionCoefficientScaleToCanonical(
    const std::optional<double> &chemical_scale_to_canonical_mg,
    double wall_order,
    const QString &flow_unit)
{
    double source_length_to_m = 0.0;
    if (flowUnitUsesMetricLength(flow_unit))
        source_length_to_m = 1.0;
    else if (flowUnitUsesFootLength(flow_unit))
        source_length_to_m = aowis::units::metres_per_international_foot;
    else
        return std::nullopt;

    if (wall_order == 0.0)
    {
        if (!chemical_scale_to_canonical_mg.has_value())
            return std::nullopt;
        return *chemical_scale_to_canonical_mg
            / (source_length_to_m * source_length_to_m);
    }

    return source_length_to_m;
}

std::optional<HydraulicNodeQualitySourceType> qualitySourceType(const QString &source_type)
{
    const QString normalized = normalizedQualityToken(source_type);
    if (normalized == QStringLiteral("concen")
        || normalized == QStringLiteral("concentration"))
    {
        return HydraulicNodeQualitySourceType::Concentration;
    }
    if (normalized == QStringLiteral("mass")
        || normalized == QStringLiteral("massbooster"))
    {
        return HydraulicNodeQualitySourceType::MassBooster;
    }
    if (normalized == QStringLiteral("flowpaced")
        || normalized == QStringLiteral("flowpacedbooster"))
    {
        return HydraulicNodeQualitySourceType::FlowPacedBooster;
    }
    if (normalized == QStringLiteral("setpoint")
        || normalized == QStringLiteral("setpointbooster"))
    {
        return HydraulicNodeQualitySourceType::SetpointBooster;
    }
    if (normalized.isEmpty() || normalized == QStringLiteral("none"))
        return HydraulicNodeQualitySourceType::None;
    return std::nullopt;
}

std::optional<HydraulicNodeTankMixingModel> tankMixingModel(const QString &source_model)
{
    const QString normalized = normalizedQualityToken(source_model);
    if (normalized.isEmpty() || normalized == QStringLiteral("mixed")
        || normalized == QStringLiteral("mix1")
        || normalized == QStringLiteral("completemix")
        || normalized == QStringLiteral("completemixing"))
    {
        return HydraulicNodeTankMixingModel::CompleteMix;
    }
    if (normalized == QStringLiteral("2comp")
        || normalized == QStringLiteral("mix2")
        || normalized == QStringLiteral("twocompartment")
        || normalized == QStringLiteral("twocompartmentmixing"))
    {
        return HydraulicNodeTankMixingModel::TwoCompartment;
    }
    if (normalized == QStringLiteral("fifo")
        || normalized == QStringLiteral("firstinfirstout"))
    {
        return HydraulicNodeTankMixingModel::FirstInFirstOut;
    }
    if (normalized == QStringLiteral("lifo")
        || normalized == QStringLiteral("lastinfirstout"))
    {
        return HydraulicNodeTankMixingModel::LastInFirstOut;
    }
    return std::nullopt;
}

template<typename NodeType>
void importNodeQualityRows(
    const EpanetJsProjectSnapshot &project,
    const QString &table_name,
    QList<NodeType> &nodes,
    const QJsonObject &project_settings,
    const QJsonObject &simulation_settings,
    const QSet<QUuid> &pattern_uuids,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, table_name);
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString chemical_unit = units.value(QStringLiteral("chemicalConcentration")).toString();
    const std::optional<double> chemical_scale =
        chemicalConcentrationScaleToMgPerL(chemical_unit);
    const QString water_age_unit = units.value(QStringLiteral("waterAge")).toString();
    const WaterQualityAnalysisType analysis = epanetJsQualityAnalysis(simulation_settings);

    const QHash<QUuid, qsizetype> node_positions = uuidPositionIndex(nodes);
    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value())
            continue;

        const QUuid uuid = result.id_map.uuidFor(table_name, *source_id);
        const QHash<QUuid, qsizetype>::const_iterator position = node_positions.constFind(uuid);
        if (position == node_positions.cend())
            continue;
        NodeType *node = &nodes[position.value()];

        if (row.contains(QStringLiteral("initial_quality"))
            && !row.value(QStringLiteral("initial_quality")).isNull())
        {
            const std::optional<double> source_quality = finiteVariantDouble(
                row.value(QStringLiteral("initial_quality")));
            if (!source_quality.has_value() || *source_quality < 0.0)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-initial-quality"),
                    QStringLiteral("epanet-js %1 id %2 has invalid initial_quality.")
                        .arg(table_name).arg(*source_id),
                    table_name,
                    *source_id);
            }
            else if (analysis == WaterQualityAnalysisType::Chemical)
            {
                if (!chemical_scale.has_value())
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("unsupported-initial-quality-unit"),
                        QStringLiteral("AOWIS cannot convert epanet-js %1 id %2 initial chemical quality unit '%3'.")
                            .arg(table_name).arg(*source_id).arg(chemical_unit),
                        table_name,
                        *source_id);
                }
                else
                {
                    node->initial_chemical_concentration_mg_per_l =
                        *source_quality * *chemical_scale;
                }
            }
            else if (analysis == WaterQualityAnalysisType::WaterAge)
            {
                const std::optional<double> age_h = waterAgeToHours(
                    *source_quality, water_age_unit);
                if (!age_h.has_value())
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("unsupported-initial-water-age-unit"),
                        QStringLiteral("AOWIS cannot convert epanet-js %1 id %2 initial water-age unit '%3'.")
                            .arg(table_name).arg(*source_id).arg(water_age_unit),
                        table_name,
                        *source_id);
                }
                else
                {
                    node->initial_water_age_h = *age_h;
                }
            }
            else if (*source_quality != 0.0)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Warning,
                    analysis == WaterQualityAnalysisType::SourceTrace
                        ? QStringLiteral("ignored-trace-initial-quality")
                        : QStringLiteral("inactive-initial-quality-not-imported"),
                    analysis == WaterQualityAnalysisType::SourceTrace
                        ? QStringLiteral("epanet-js %1 id %2 has initial_quality, but EPANET ignores initial node quality during source-trace analysis.")
                            .arg(table_name).arg(*source_id)
                        : QStringLiteral("epanet-js %1 id %2 has initial_quality while quality analysis is disabled; AOWIS cannot infer whether the stored value is chemical concentration or water age.")
                            .arg(table_name).arg(*source_id),
                    table_name,
                    *source_id);
            }
        }

        const bool has_source_type = row.contains(QStringLiteral("chemical_source_type"))
            && !row.value(QStringLiteral("chemical_source_type")).isNull()
            && !row.value(QStringLiteral("chemical_source_type")).toString().trimmed().isEmpty();
        const bool has_source_strength = row.contains(QStringLiteral("chemical_source_strength"))
            && !row.value(QStringLiteral("chemical_source_strength")).isNull();
        const bool has_source_pattern = row.contains(QStringLiteral("chemical_source_pattern_id"))
            && !row.value(QStringLiteral("chemical_source_pattern_id")).isNull();

        if (!has_source_type)
        {
            if (has_source_strength || has_source_pattern)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-quality-source-type"),
                    QStringLiteral("epanet-js %1 id %2 has water-quality source data without chemical_source_type.")
                        .arg(table_name).arg(*source_id),
                    table_name,
                    *source_id);
            }
            continue;
        }

        const QString source_type_text = row.value(QStringLiteral("chemical_source_type")).toString();
        const std::optional<HydraulicNodeQualitySourceType> source_type =
            qualitySourceType(source_type_text);
        if (!source_type.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unknown-quality-source-type"),
                QStringLiteral("epanet-js %1 id %2 has unknown chemical source type '%3'.")
                    .arg(table_name).arg(*source_id).arg(source_type_text),
                table_name,
                *source_id);
            continue;
        }
        if (*source_type == HydraulicNodeQualitySourceType::None)
            continue;

        const std::optional<double> source_strength = has_source_strength
            ? finiteVariantDouble(row.value(QStringLiteral("chemical_source_strength")))
            : std::nullopt;
        if (!source_strength.has_value() || *source_strength < 0.0)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-quality-source-strength"),
                QStringLiteral("epanet-js %1 id %2 has invalid chemical source strength.")
                    .arg(table_name).arg(*source_id),
                table_name,
                *source_id);
            continue;
        }
        if (!chemical_scale.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unsupported-quality-source-unit"),
                QStringLiteral("AOWIS cannot convert epanet-js %1 id %2 chemical source unit '%3'.")
                    .arg(table_name).arg(*source_id).arg(chemical_unit),
                table_name,
                *source_id);
            continue;
        }

        HydraulicNodeQualitySource source;
        source.type = *source_type;
        if (*source_type == HydraulicNodeQualitySourceType::MassBooster)
            source.chemical_mass_flow_mg_per_min = *source_strength * *chemical_scale;
        else
            source.chemical_concentration_mg_per_l = *source_strength * *chemical_scale;

        if (has_source_pattern)
        {
            const std::optional<qint64> pattern_id = integerValue(
                row.value(QStringLiteral("chemical_source_pattern_id")));
            if (pattern_id.has_value() && *pattern_id <= 0)
            {
                // EPANET-style zero references mean no pattern.
            }
            else
            {
                const QUuid pattern_uuid = pattern_id.has_value()
                    ? result.id_map.uuidFor(QStringLiteral("patterns"), *pattern_id)
                    : QUuid();
                if (!pattern_id.has_value() || pattern_uuid.isNull()
                    || !pattern_uuids.contains(pattern_uuid))
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("missing-quality-source-pattern-reference"),
                        QStringLiteral("epanet-js %1 id %2 references a missing water-quality source pattern.")
                            .arg(table_name).arg(*source_id),
                        table_name,
                        *source_id);
                    continue;
                }
                source.pattern_uuid = pattern_uuid;
            }
        }

        node->quality_source = source;
    }
}

 } // namespace

void importWaterQualityEntityData(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    const QJsonObject &simulation_settings,
    EpanetJsProjectConversionResult &result)
{
    const QSet<QUuid> pattern_uuids = uuidMembershipIndex(result.network.patterns_time);
    importNodeQualityRows(
        project, QStringLiteral("junctions"), result.network.nodes_junctions,
        project_settings, simulation_settings, pattern_uuids, result);
    importNodeQualityRows(
        project, QStringLiteral("reservoirs"), result.network.nodes_reservoirs,
        project_settings, simulation_settings, pattern_uuids, result);
    importNodeQualityRows(
        project, QStringLiteral("tanks"), result.network.nodes_tanks,
        project_settings, simulation_settings, pattern_uuids, result);

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString flow_unit = firstUnit(units, QStringList{QStringLiteral("flow")});
    const QString chemical_unit = units.value(QStringLiteral("chemicalConcentration")).toString();
    const std::optional<double> chemical_scale =
        chemicalConcentrationScaleToMgPerL(chemical_unit);

    for (HydraulicLinkPipe &pipe : result.network.links_pipes)
    {
        pipe.bulk_reaction.order =
            result.network.options_reaction.global_pipe_bulk_reaction.order;
        pipe.wall_reaction.order =
            result.network.options_reaction.global_pipe_wall_reaction.order;
    }
    for (HydraulicNodeTank &tank : result.network.nodes_tanks)
    {
        tank.bulk_reaction.order =
            result.network.options_reaction.global_tank_bulk_reaction.order;
    }

    const EpanetJsTableSnapshot *tank_table = tableByName(project, QStringLiteral("tanks"));
    if (tank_table != nullptr)
    {
        const double tank_bulk_order =
            result.network.options_reaction.global_tank_bulk_reaction.order;
        const bool tank_bulk_scale_available = chemical_scale.has_value()
            || std::abs(tank_bulk_order - 1.0) <= 1.0e-12;
        const double tank_bulk_scale = chemical_scale.has_value()
            ? reactionCoefficientScaleToCanonicalMg(*chemical_scale, tank_bulk_order)
            : 1.0;

        const QHash<QUuid, qsizetype> tank_positions = uuidPositionIndex(result.network.nodes_tanks);

        for (const QVariantMap &row : tank_table->rows)
        {
            const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
            if (!source_id.has_value())
                continue;
            const QUuid uuid = result.id_map.uuidFor(QStringLiteral("tanks"), *source_id);
            const QHash<QUuid, qsizetype>::const_iterator position = tank_positions.constFind(uuid);
            if (position == tank_positions.cend())
                continue;
            HydraulicNodeTank *tank = &result.network.nodes_tanks[position.value()];

            if (row.contains(QStringLiteral("mixing_model"))
                && !row.value(QStringLiteral("mixing_model")).isNull())
            {
                const QString source_model = row.value(QStringLiteral("mixing_model")).toString();
                const std::optional<HydraulicNodeTankMixingModel> mixing_model =
                    tankMixingModel(source_model);
                if (!mixing_model.has_value())
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("unknown-tank-mixing-model"),
                        QStringLiteral("epanet-js tank id %1 has unknown mixing model '%2'.")
                            .arg(*source_id).arg(source_model),
                        QStringLiteral("tanks"),
                        *source_id);
                }
                else
                {
                    tank->mixing_model = *mixing_model;
                }
            }

            const bool has_mixing_fraction = row.contains(QStringLiteral("mixing_fraction"))
                && !row.value(QStringLiteral("mixing_fraction")).isNull();
            if (has_mixing_fraction)
            {
                const std::optional<double> fraction = finiteVariantDouble(
                    row.value(QStringLiteral("mixing_fraction")));
                if (!fraction.has_value() || *fraction < 0.0 || *fraction > 1.0)
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("invalid-tank-mixing-fraction"),
                        QStringLiteral("epanet-js tank id %1 has mixing_fraction outside [0, 1].")
                            .arg(*source_id),
                        QStringLiteral("tanks"),
                        *source_id);
                }
                else
                {
                    tank->mixing_fraction = *fraction;
                }
            }
            else if (tank->mixing_model == HydraulicNodeTankMixingModel::TwoCompartment)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-tank-mixing-fraction"),
                    QStringLiteral("epanet-js tank id %1 uses two-compartment mixing without mixing_fraction.")
                        .arg(*source_id),
                    QStringLiteral("tanks"),
                    *source_id);
            }

            if (row.contains(QStringLiteral("bulk_reaction_coeff"))
                && !row.value(QStringLiteral("bulk_reaction_coeff")).isNull())
            {
                const std::optional<double> coefficient = finiteVariantDouble(
                    row.value(QStringLiteral("bulk_reaction_coeff")));
                if (!coefficient.has_value())
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("invalid-tank-bulk-reaction"),
                        QStringLiteral("epanet-js tank id %1 has invalid bulk_reaction_coeff.")
                            .arg(*source_id),
                        QStringLiteral("tanks"),
                        *source_id);
                }
                else if (!tank_bulk_scale_available)
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("unsupported-tank-bulk-reaction-unit"),
                        QStringLiteral("AOWIS cannot convert epanet-js tank id %1 bulk reaction coefficient for chemical unit '%2' and reaction order %3.")
                            .arg(*source_id).arg(chemical_unit).arg(tank_bulk_order),
                        QStringLiteral("tanks"),
                        *source_id);
                }
                else
                {
                    tank->override_bulk_reaction = true;
                    tank->bulk_reaction.coefficient = *coefficient * tank_bulk_scale;
                }
            }
        }
    }

    const EpanetJsTableSnapshot *pipe_table = tableByName(project, QStringLiteral("pipes"));
    if (pipe_table == nullptr)
        return;

    const double pipe_bulk_order =
        result.network.options_reaction.global_pipe_bulk_reaction.order;
    const bool pipe_bulk_scale_available = chemical_scale.has_value()
        || std::abs(pipe_bulk_order - 1.0) <= 1.0e-12;
    const double pipe_bulk_scale = chemical_scale.has_value()
        ? reactionCoefficientScaleToCanonicalMg(*chemical_scale, pipe_bulk_order)
        : 1.0;
    const std::optional<double> pipe_wall_scale = wallReactionCoefficientScaleToCanonical(
        chemical_scale,
        result.network.options_reaction.global_pipe_wall_reaction.order,
        flow_unit);

    const QHash<QUuid, qsizetype> pipe_positions = uuidPositionIndex(result.network.links_pipes);

    for (const QVariantMap &row : pipe_table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value())
            continue;
        const QUuid uuid = result.id_map.uuidFor(QStringLiteral("pipes"), *source_id);
        const QHash<QUuid, qsizetype>::const_iterator position = pipe_positions.constFind(uuid);
        if (position == pipe_positions.cend())
            continue;
        HydraulicLinkPipe *pipe = &result.network.links_pipes[position.value()];

        if (row.contains(QStringLiteral("bulk_reaction_coeff"))
            && !row.value(QStringLiteral("bulk_reaction_coeff")).isNull())
        {
            const std::optional<double> coefficient = finiteVariantDouble(
                row.value(QStringLiteral("bulk_reaction_coeff")));
            if (!coefficient.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-pipe-bulk-reaction"),
                    QStringLiteral("epanet-js pipe id %1 has invalid bulk_reaction_coeff.")
                        .arg(*source_id),
                    QStringLiteral("pipes"),
                    *source_id);
            }
            else if (!pipe_bulk_scale_available)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("unsupported-pipe-bulk-reaction-unit"),
                    QStringLiteral("AOWIS cannot convert epanet-js pipe id %1 bulk reaction coefficient for chemical unit '%2' and reaction order %3.")
                        .arg(*source_id).arg(chemical_unit).arg(pipe_bulk_order),
                    QStringLiteral("pipes"),
                    *source_id);
            }
            else
            {
                pipe->override_bulk_reaction = true;
                pipe->bulk_reaction.coefficient = *coefficient * pipe_bulk_scale;
            }
        }

        if (row.contains(QStringLiteral("wall_reaction_coeff"))
            && !row.value(QStringLiteral("wall_reaction_coeff")).isNull())
        {
            const std::optional<double> coefficient = finiteVariantDouble(
                row.value(QStringLiteral("wall_reaction_coeff")));
            if (!coefficient.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-pipe-wall-reaction"),
                    QStringLiteral("epanet-js pipe id %1 has invalid wall_reaction_coeff.")
                        .arg(*source_id),
                    QStringLiteral("pipes"),
                    *source_id);
            }
            else if (!pipe_wall_scale.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("unsupported-pipe-wall-reaction-unit"),
                    QStringLiteral("AOWIS cannot convert epanet-js pipe id %1 wall reaction coefficient for flow unit '%2', chemical unit '%3', and reaction order %4.")
                        .arg(*source_id).arg(flow_unit, chemical_unit)
                        .arg(result.network.options_reaction.global_pipe_wall_reaction.order),
                    QStringLiteral("pipes"),
                    *source_id);
            }
            else
            {
                pipe->override_wall_reaction = true;
                pipe->wall_reaction.coefficient = *coefficient * *pipe_wall_scale;
            }
        }
    }
}


} // namespace EpanetJsWaterQuality
