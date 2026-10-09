#include "import/epanet_js_settings.h"
#include "import/epanet_js_conversion_common.h"
#include "import/epanet_js_schema.h"
#include "import/epanet_js_units.h"
#include <aowis/model/units/conversion.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
namespace EpanetJsSettings
{
using namespace EpanetJsConversionCommon;
using namespace EpanetJsSchema;
using namespace EpanetJsUnits;
QString projectPipeLibraryText(const EpanetJsProjectSnapshot &project)
{
    const EpanetJsTableSnapshot *project_table = tableByName(project, QStringLiteral("project"));
    if (project_table == nullptr || project_table->rows.isEmpty()
        || !project_table->columns.contains(QStringLiteral("pipe_library")))
    {
        return {};
    }
    return project_table->rows.first().value(QStringLiteral("pipe_library")).toString();
}

QString pipeMaterialLookupKey(const QString &label)
{
    return label.trimmed().toCaseFolded();
}


QJsonObject simulationSettingsObject(
    const EpanetJsProjectSnapshot &project,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("simulation_settings"));
    if (table == nullptr || table->rows.isEmpty())
        return {};

    const QString data = table->rows.first().value(QStringLiteral("data")).toString();
    if (data.trimmed().isEmpty())
        return {};

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("invalid-simulation-settings-json"),
            QStringLiteral("epanet-js simulation_settings.data is not a valid JSON object; AOWIS kept its simulation defaults."),
            QStringLiteral("simulation_settings"));
        return {};
    }
    return document.object();
}

void mapHeadlossFormula(
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const QString value = project_settings.value(QStringLiteral("headlossFormula"))
        .toString().trimmed().toUpper();
    if (value.isEmpty())
        return;

    if (value == QStringLiteral("H-W") || value == QStringLiteral("HW") ||
        value == QStringLiteral("HAZEN-WILLIAMS"))
    {
        result.network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::HazenWilliams;
        return;
    }
    if (value == QStringLiteral("D-W") || value == QStringLiteral("DW") ||
        value == QStringLiteral("DARCY-WEISBACH"))
    {
        result.network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::DarcyWeisbach;
        return;
    }
    if (value == QStringLiteral("C-M") || value == QStringLiteral("CM") ||
        value == QStringLiteral("CHEZY-MANNING"))
    {
        result.network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::ChezyManning;
        return;
    }

    appendDiagnostic(
        result,
        EpanetJsConversionDiagnosticSeverity::Warning,
        QStringLiteral("unknown-headloss-formula"),
        QStringLiteral("epanet-js headloss formula '%1' is unknown; AOWIS kept its default formula.").arg(value),
        QStringLiteral("project"));
}

std::optional<double> pipeLibraryRoughnessToCanonical(
    double value,
    HydraulicHeadlossFormula formula,
    const QString &roughness_unit,
    const QString &flow_unit)
{
    if (!std::isfinite(value) || value <= 0.0)
        return std::nullopt;

    switch (formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
    case HydraulicHeadlossFormula::ChezyManning:
        return value;
    case HydraulicHeadlossFormula::DarcyWeisbach:
        return darcyRoughnessToMm(value, roughness_unit, flow_unit);
    }
    return std::nullopt;
}

void importPipeMaterials(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const QString library_text = projectPipeLibraryText(project).trimmed();
    if (library_text.isEmpty())
        return;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(library_text.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isArray())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-pipe-library-json"),
            QStringLiteral("epanet-js project.pipe_library is not a valid JSON array."),
            QStringLiteral("project"));
        return;
    }

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString roughness_unit = units.value(QStringLiteral("roughness")).toString().trimmed();
    const QString flow_unit = units.value(QStringLiteral("flow")).toString().trimmed();
    const HydraulicHeadlossFormula formula = result.network.options_hydraulic.headloss_formula;
    QSet<QString> material_keys;

    const QJsonArray materials = document.array();
    for (qsizetype material_index = 0; material_index < materials.size(); ++material_index)
    {
        const QJsonValue material_value = materials.at(material_index);
        if (!material_value.isObject())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-pipe-material"),
                QStringLiteral("epanet-js pipe library entry %1 is not an object.")
                    .arg(material_index),
                QStringLiteral("project"));
            continue;
        }

        const QJsonObject material_object = material_value.toObject();
        const QString label = material_object.value(QStringLiteral("label")).toString().trimmed();
        const QString material_key = pipeMaterialLookupKey(label);
        if (material_key.isEmpty())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-pipe-material-label"),
                QStringLiteral("epanet-js pipe library entry %1 has no material label.")
                    .arg(material_index),
                QStringLiteral("project"));
            continue;
        }
        if (material_keys.contains(material_key))
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("duplicate-pipe-material-label"),
                QStringLiteral("epanet-js pipe library contains duplicate material label '%1'.")
                    .arg(label),
                QStringLiteral("project"));
            continue;
        }
        material_keys.insert(material_key);

        const QJsonValue entries_value = material_object.value(QStringLiteral("entries"));
        if (!entries_value.isArray() || entries_value.toArray().isEmpty())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-pipe-material-entries"),
                QStringLiteral("epanet-js pipe material '%1' has no roughness entries.")
                    .arg(label),
                QStringLiteral("project"));
            continue;
        }

        HydraulicPipeMaterial material;
        material.id = label;
        material.uuid = uuidV5(
            result.network.uuid,
            QStringLiteral("epanet-js/pipe-materials/%1").arg(material_key).toUtf8());
        material.description = QStringLiteral("Imported from epanet-js pipe library");

        QSet<int> ages;
        const QJsonArray entries = entries_value.toArray();
        for (qsizetype entry_index = 0; entry_index < entries.size(); ++entry_index)
        {
            const QJsonValue entry_value = entries.at(entry_index);
            if (!entry_value.isObject())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-pipe-material-entry"),
                    QStringLiteral("epanet-js pipe material '%1' entry %2 is not an object.")
                        .arg(label)
                        .arg(entry_index),
                    QStringLiteral("project"));
                continue;
            }

            const QJsonObject entry_object = entry_value.toObject();
            const std::optional<double> source_age = finiteDouble(entry_object.value(QStringLiteral("age")));
            const std::optional<double> source_roughness = finiteDouble(
                entry_object.value(QStringLiteral("roughness")));
            if (!source_age.has_value() || *source_age < 0.0
                || std::floor(*source_age) != *source_age
                || *source_age > static_cast<double>(std::numeric_limits<int>::max()))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-pipe-material-age"),
                    QStringLiteral("epanet-js pipe material '%1' entry %2 has an invalid age.")
                        .arg(label)
                        .arg(entry_index),
                    QStringLiteral("project"));
                continue;
            }

            const int age_years = static_cast<int>(*source_age);
            if (ages.contains(age_years))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("duplicate-pipe-material-age"),
                    QStringLiteral("epanet-js pipe material '%1' contains duplicate age %2.")
                        .arg(label)
                        .arg(age_years),
                    QStringLiteral("project"));
                continue;
            }
            ages.insert(age_years);

            const std::optional<double> roughness = source_roughness.has_value()
                ? pipeLibraryRoughnessToCanonical(
                    *source_roughness, formula, roughness_unit, flow_unit)
                : std::nullopt;
            if (!roughness.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("unsupported-pipe-material-roughness"),
                    QStringLiteral("AOWIS cannot convert roughness for epanet-js pipe material '%1' at age %2.")
                        .arg(label)
                        .arg(age_years),
                    QStringLiteral("project"));
                continue;
            }

            HydraulicPipeMaterialRoughnessAtAge entry;
            entry.age_years = age_years;
            switch (formula)
            {
            case HydraulicHeadlossFormula::HazenWilliams:
                entry.roughness_hazen_williams = *roughness;
                break;
            case HydraulicHeadlossFormula::DarcyWeisbach:
                entry.roughness_darcy_weisbach_mm = *roughness;
                break;
            case HydraulicHeadlossFormula::ChezyManning:
                entry.roughness_chezy_manning = *roughness;
                break;
            }
            material.roughness_by_age.append(entry);
        }

        std::sort(
            material.roughness_by_age.begin(),
            material.roughness_by_age.end(),
            [](const HydraulicPipeMaterialRoughnessAtAge &left,
               const HydraulicPipeMaterialRoughnessAtAge &right)
            {
                return left.age_years < right.age_years;
            });

        if (!material.roughness_by_age.isEmpty())
            result.network.pipe_materials.append(material);
    }
}

void setUnsignedSecondsIfPresent(
    const QJsonObject &object,
    const QString &key,
    quint64 &target,
    EpanetJsProjectConversionResult &result)
{
    if (!object.contains(key))
        return;
    const std::optional<double> value = finiteDouble(object.value(key));
    if (!value.has_value() || *value < 0.0 || std::floor(*value) != *value)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("invalid-time-setting"),
            QStringLiteral("epanet-js timing.%1 is not a non-negative whole number of seconds; AOWIS kept its default.").arg(key),
            QStringLiteral("simulation_settings"));
        return;
    }
    target = static_cast<quint64>(*value);
}

void mapSimulationSettings(
    const QJsonObject &settings,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    if (settings.isEmpty())
        return;

    const QJsonObject timing = settings.value(QStringLiteral("timing")).toObject();
    setUnsignedSecondsIfPresent(timing, QStringLiteral("duration"), result.network.duration_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("hydraulicTimestep"), result.network.timestep_hydraulic_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("qualityTimestep"), result.network.timestep_quality_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("patternTimestep"), result.network.timestep_pattern_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("patternStart"), result.network.start_pattern_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("reportTimestep"), result.network.timestep_report_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("reportStart"), result.network.start_report_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("ruleTimestep"), result.network.timestep_rule_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("startClockTime"), result.network.start_time_of_day_s, result);

    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("globalDemandMultiplier"))); value.has_value())
        result.network.options_hydraulic.demand_multiplier = *value;

    const QString demand_model = settings.value(QStringLiteral("demandModel")).toString().trimmed().toUpper();
    if (demand_model == QStringLiteral("DDA"))
        result.network.options_hydraulic.demand_model = HydraulicDemandModel::DemandDriven;
    else if (demand_model == QStringLiteral("PDA"))
        result.network.options_hydraulic.demand_model = HydraulicDemandModel::PressureDriven;
    else if (!demand_model.isEmpty())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("unknown-demand-model"),
            QStringLiteral("epanet-js demand model '%1' is unknown; AOWIS kept its default demand model.").arg(demand_model),
            QStringLiteral("simulation_settings"));
    }

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString pressure_unit = units.value(QStringLiteral("pressure")).toString();
    const QString flow_unit = units.value(QStringLiteral("flow")).toString();
    QString head_unit = units.value(QStringLiteral("head")).toString();
    if (head_unit.isEmpty())
        head_unit = units.value(QStringLiteral("elevation")).toString();

    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("specificGravity"))); value.has_value())
        result.network.options_hydraulic.specific_gravity = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("viscosity"))); value.has_value())
        result.network.options_hydraulic.relative_viscosity = *value;

    const QList<QPair<QString, double *>> pressure_fields = {
        {QStringLiteral("minimumPressure"), &result.network.options_hydraulic.minimum_pressure_head_m},
        {QStringLiteral("requiredPressure"), &result.network.options_hydraulic.required_pressure_head_m}
    };
    for (const QPair<QString, double *> &field : pressure_fields)
    {
        if (!settings.contains(field.first))
            continue;
        const std::optional<double> value = finiteDouble(settings.value(field.first));
        if (!value.has_value())
            continue;
        const std::optional<double> converted = pressureToHeadM(
            *value, pressure_unit, result.network.options_hydraulic.specific_gravity);
        if (converted.has_value())
            *field.second = *converted;
        else
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Warning,
                QStringLiteral("unsupported-pressure-unit"),
                QStringLiteral("AOWIS cannot convert epanet-js pressure unit '%1' for %2; the default was kept.")
                    .arg(pressure_unit, field.first),
                QStringLiteral("simulation_settings"));
        }
    }

    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("pressureExponent"))); value.has_value())
        result.network.options_hydraulic.pressure_exponent = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("accuracy"))); value.has_value())
        result.network.options_hydraulic.accuracy = *value;
    if (settings.contains(QStringLiteral("backflowAllowed")) && settings.value(QStringLiteral("backflowAllowed")).isBool())
        result.network.options_hydraulic.emitters_can_backflow = settings.value(QStringLiteral("backflowAllowed")).toBool();

    const QString unbalanced = settings.value(QStringLiteral("unbalancedMode")).toString().trimmed().toUpper();
    if (unbalanced == QStringLiteral("CONTINUE"))
        result.network.options_hydraulic.unbalanced_action = HydraulicUnbalancedAction::Continue;
    else if (unbalanced == QStringLiteral("STOP"))
        result.network.options_hydraulic.unbalanced_action = HydraulicUnbalancedAction::Stop;
    if (settings.contains(QStringLiteral("unbalancedExtraTrials")))
        result.network.options_hydraulic.unbalanced_extra_trials = settings.value(QStringLiteral("unbalancedExtraTrials")).toInt(result.network.options_hydraulic.unbalanced_extra_trials);
    if (settings.contains(QStringLiteral("maximumTrials")))
        result.network.options_hydraulic.maximum_trials = settings.value(QStringLiteral("maximumTrials")).toInt(result.network.options_hydraulic.maximum_trials);
    if (settings.contains(QStringLiteral("checkFrequency")))
        result.network.options_hydraulic.check_frequency = settings.value(QStringLiteral("checkFrequency")).toInt(result.network.options_hydraulic.check_frequency);
    if (settings.contains(QStringLiteral("maximumCheck")))
        result.network.options_hydraulic.maximum_check = settings.value(QStringLiteral("maximumCheck")).toInt(result.network.options_hydraulic.maximum_check);
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("dampingLimit"))); value.has_value())
        result.network.options_hydraulic.damping_limit = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("maximumHeadError"))); value.has_value())
    {
        const std::optional<double> converted = lengthToM(*value, head_unit);
        if (converted.has_value())
            result.network.options_hydraulic.maximum_head_error_m = *converted;
    }
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("maximumFlowChange"))); value.has_value())
    {
        const std::optional<double> converted = flowToM3PerH(*value, flow_unit);
        if (converted.has_value())
            result.network.options_hydraulic.maximum_flow_change_m3_per_h = *converted;
    }

    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionBulkOrder"))); value.has_value())
        result.network.options_reaction.global_pipe_bulk_reaction.order = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionWallOrder"))); value.has_value())
        result.network.options_reaction.global_pipe_wall_reaction.order = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionTankOrder"))); value.has_value())
        result.network.options_reaction.global_tank_bulk_reaction.order = *value;

    QString chemical_unit = units.value(QStringLiteral("chemicalConcentration")).toString();
    if (chemical_unit.isEmpty())
        chemical_unit = settings.value(QStringLiteral("qualityMassUnit")).toString();
    const std::optional<double> chemical_scale = chemicalConcentrationScaleToMgPerL(chemical_unit);
    double source_length_to_m = 1.0;
    bool source_length_known = true;
    if (flowUnitUsesFootLength(flow_unit))
        source_length_to_m = aowis::units::metres_per_international_foot;
    else if (!flowUnitUsesMetricLength(flow_unit))
        source_length_known = false;

    if (chemical_scale.has_value())
    {
        const double pipe_bulk_order = result.network.options_reaction.global_pipe_bulk_reaction.order;
        const double tank_bulk_order = result.network.options_reaction.global_tank_bulk_reaction.order;
        const double pipe_bulk_scale = std::pow(*chemical_scale, 1.0 - std::max(pipe_bulk_order, 0.0));
        const double tank_bulk_scale = std::pow(*chemical_scale, 1.0 - std::max(tank_bulk_order, 0.0));

        if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionGlobalBulk"))); value.has_value())
        {
            result.network.options_reaction.global_pipe_bulk_reaction.coefficient = *value * pipe_bulk_scale;
            result.network.options_reaction.global_tank_bulk_reaction.coefficient = *value * tank_bulk_scale;
        }
        if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionLimitingPotential"))); value.has_value())
            result.network.options_reaction.limiting_concentration_mg_per_l = *value * *chemical_scale;
    }
    else if (settings.contains(QStringLiteral("reactionGlobalBulk")) ||
             settings.contains(QStringLiteral("reactionLimitingPotential")))
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("unsupported-chemical-unit"),
            QStringLiteral("AOWIS cannot convert epanet-js chemical concentration unit '%1'; concentration-dependent reaction settings kept their defaults.").arg(chemical_unit),
            QStringLiteral("simulation_settings"));
    }

    if (chemical_scale.has_value() && source_length_known)
    {
        const double wall_order = result.network.options_reaction.global_pipe_wall_reaction.order;
        const double wall_scale = wall_order == 0.0
            ? *chemical_scale / (source_length_to_m * source_length_to_m)
            : source_length_to_m;
        if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionGlobalWall"))); value.has_value())
            result.network.options_reaction.global_pipe_wall_reaction.coefficient = *value * wall_scale;
        if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionRoughnessCorrelation"))); value.has_value())
            result.network.options_reaction.roughness_reaction_factor = *value * wall_scale;
    }
    else if (settings.contains(QStringLiteral("reactionGlobalWall")) ||
             settings.contains(QStringLiteral("reactionRoughnessCorrelation")))
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("unsupported-reaction-unit-system"),
            QStringLiteral("AOWIS cannot determine the epanet-js reaction unit system from flow unit '%1'; wall reaction settings kept their defaults.").arg(flow_unit),
            QStringLiteral("simulation_settings"));
    }

    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("energyGlobalEfficiency"))); value.has_value())
        result.network.options_energy.global_pump_efficiency_percent = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("energyGlobalPrice"))); value.has_value())
        result.network.options_energy.global_energy_price_per_kw_h = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("energyDemandCharge"))); value.has_value())
        result.network.options_energy.demand_charge_per_kw = *value;

    if (settings.contains(QStringLiteral("energyGlobalPatternId")) && !settings.value(QStringLiteral("energyGlobalPatternId")).isNull())
    {
        const std::optional<qint64> source_id = integerValue(settings.value(QStringLiteral("energyGlobalPatternId")).toVariant());
        bool imported = false;
        QUuid pattern_uuid;
        if (source_id.has_value() && result.id_map.contains(QStringLiteral("patterns"), *source_id))
        {
            pattern_uuid = result.id_map.uuidFor(QStringLiteral("patterns"), *source_id);
            for (const HydraulicPatternTime &pattern : result.network.patterns_time)
            {
                if (pattern.uuid == pattern_uuid)
                {
                    imported = true;
                    break;
                }
            }
        }
        if (imported)
            result.network.options_energy.global_energy_price_pattern_uuid = pattern_uuid;
        else
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Warning,
                QStringLiteral("missing-energy-pattern-reference"),
                QStringLiteral("epanet-js global energy price pattern does not reference an existing pattern; AOWIS left it unset."),
                QStringLiteral("simulation_settings"));
        }
    }

    // epanet-js intentionally disables EPANET's node/link summary tables and uses
    // its own result views instead. Preserve that report behavior on import.
    result.network.options_report.summary = false;
    if (settings.contains(QStringLiteral("reportEnergy")) && settings.value(QStringLiteral("reportEnergy")).isBool())
        result.network.options_report.energy = settings.value(QStringLiteral("reportEnergy")).toBool();

    const QString status = settings.value(QStringLiteral("statusReport")).toString().trimmed().toUpper();
    if (status == QStringLiteral("NO") || status == QStringLiteral("NONE"))
        result.network.options_report.status = HydraulicSimulationReportStatus::None;
    else if (status == QStringLiteral("YES") || status == QStringLiteral("NORMAL"))
        result.network.options_report.status = HydraulicSimulationReportStatus::Normal;
    else if (status == QStringLiteral("FULL"))
        result.network.options_report.status = HydraulicSimulationReportStatus::Full;

    const QString statistic = settings.value(QStringLiteral("reportStatistic")).toString().trimmed().toUpper();
    if (statistic == QStringLiteral("AVERAGE"))
        result.network.report_statistic = HydraulicSimulationReportStatistic::Average;
    else if (statistic == QStringLiteral("MINIMUM"))
        result.network.report_statistic = HydraulicSimulationReportStatistic::Minimum;
    else if (statistic == QStringLiteral("MAXIMUM"))
        result.network.report_statistic = HydraulicSimulationReportStatistic::Maximum;
    else if (statistic == QStringLiteral("RANGE"))
        result.network.report_statistic = HydraulicSimulationReportStatistic::Range;
    else if (statistic == QStringLiteral("SERIES") || statistic == QStringLiteral("NONE"))
        result.network.report_statistic = HydraulicSimulationReportStatistic::Series;
}

QString normalizedSettingToken(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QLatin1Char(' '));
    value.remove(QLatin1Char('_'));
    value.remove(QLatin1Char('-'));
    return value;
}

void mapQualitySimulationSettings(
    const QJsonObject &settings,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    if (settings.isEmpty())
        return;

    const QString mode = normalizedSettingToken(
        settings.value(QStringLiteral("qualitySimulationType")).toString());
    if (mode.isEmpty() || mode == QStringLiteral("none"))
        return;

    WaterQualitySolverOptions options;
    if (mode == QStringLiteral("chemical") || mode == QStringLiteral("chem"))
    {
        options.analysis = WaterQualityAnalysisType::Chemical;
        options.chemical_name = settings.value(QStringLiteral("qualityChemicalName"))
            .toString().trimmed();
        if (options.chemical_name.isEmpty())
        {
            options.chemical_name = QStringLiteral("Chemical");
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Warning,
                QStringLiteral("missing-quality-chemical-name"),
                QStringLiteral("epanet-js chemical quality analysis has no chemical name; AOWIS uses 'Chemical'."),
                QStringLiteral("simulation_settings"));
        }
    }
    else if (mode == QStringLiteral("age") || mode == QStringLiteral("waterage"))
    {
        options.analysis = WaterQualityAnalysisType::WaterAge;
    }
    else if (mode == QStringLiteral("trace") || mode == QStringLiteral("sourcetrace"))
    {
        options.analysis = WaterQualityAnalysisType::SourceTrace;
        const std::optional<qint64> source_node_id = integerValue(
            settings.value(QStringLiteral("qualityTraceNodeId")).toVariant());
        const QUuid trace_node_uuid = source_node_id.has_value()
            ? result.id_map.nodeUuid(*source_node_id)
            : QUuid();
        if (!source_node_id.has_value() || trace_node_uuid.isNull())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-quality-trace-node"),
                QStringLiteral("epanet-js source-trace quality analysis does not reference an existing unambiguous node."),
                QStringLiteral("simulation_settings"));
            return;
        }
        options.trace_node_uuid = trace_node_uuid;
    }
    else
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("unknown-quality-simulation-type"),
            QStringLiteral("epanet-js quality simulation type '%1' is unknown; AOWIS cannot preserve the configured water-quality run.")
                .arg(settings.value(QStringLiteral("qualitySimulationType")).toString()),
            QStringLiteral("simulation_settings"));
        return;
    }

    if (settings.contains(QStringLiteral("diffusivity")))
    {
        const std::optional<double> diffusivity = finiteDouble(
            settings.value(QStringLiteral("diffusivity")));
        if (!diffusivity.has_value() || *diffusivity < 0.0)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-quality-diffusivity"),
                QStringLiteral("epanet-js quality diffusivity must be a finite non-negative value."),
                QStringLiteral("simulation_settings"));
            return;
        }
        options.relative_diffusivity = *diffusivity;
    }

    if (settings.contains(QStringLiteral("tolerance")))
    {
        const std::optional<double> tolerance = finiteDouble(
            settings.value(QStringLiteral("tolerance")));
        if (!tolerance.has_value() || *tolerance < 0.0)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-quality-tolerance"),
                QStringLiteral("epanet-js quality tolerance must be a finite non-negative value."),
                QStringLiteral("simulation_settings"));
            return;
        }

        switch (options.analysis)
        {
        case WaterQualityAnalysisType::Chemical:
        {
            const QJsonObject units = projectUnitsObject(project_settings);
            QString chemical_unit = units.value(QStringLiteral("chemicalConcentration")).toString();
            if (chemical_unit.isEmpty())
                chemical_unit = settings.value(QStringLiteral("qualityMassUnit")).toString();
            const std::optional<double> chemical_scale =
                chemicalConcentrationScaleToMgPerL(chemical_unit);
            if (!chemical_scale.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("unsupported-quality-chemical-unit"),
                    QStringLiteral("AOWIS cannot convert epanet-js chemical quality tolerance unit '%1'.")
                        .arg(chemical_unit),
                    QStringLiteral("simulation_settings"));
                return;
            }
            options.chemical_tolerance_mg_per_l = *tolerance * *chemical_scale;
            break;
        }
        case WaterQualityAnalysisType::WaterAge:
            options.water_age_tolerance_h = *tolerance;
            break;
        case WaterQualityAnalysisType::SourceTrace:
            options.source_trace_tolerance_percent = *tolerance;
            break;
        case WaterQualityAnalysisType::None:
            break;
        }
    }

    result.quality_runs.append(options);
}


}
