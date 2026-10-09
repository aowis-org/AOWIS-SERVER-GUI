#include "import/epanet_js_project_converter.h"
#include "import/epanet_js_conversion_common.h"
#include "import/epanet_js_project_identity.h"
#include "import/epanet_js_settings.h"
#include "import/epanet_js_patterns_curves.h"
#include "import/epanet_js_nodes.h"
#include "import/epanet_js_pipes.h"
#include "import/epanet_js_water_quality.h"
#include "import/epanet_js_demand_points.h"
#include "import/epanet_js_pumps_valves.h"
#include "import/epanet_js_controls.h"
#include "import/epanet_js_validation.h"
#include "import/epanet_js_units.h"

#include <QJsonObject>
#include <QStringList>

bool EpanetJsProjectConversionResult::hasErrors() const
{
    for (const EpanetJsConversionDiagnostic &diagnostic : this->diagnostics)
    {
        if (diagnostic.severity == EpanetJsConversionDiagnosticSeverity::Error)
            return true;
    }
    return false;
}

QString EpanetJsProjectConversionResult::errorSummary() const
{
    QStringList messages;
    for (const EpanetJsConversionDiagnostic &diagnostic : this->diagnostics)
    {
        if (diagnostic.severity == EpanetJsConversionDiagnosticSeverity::Error)
            messages.append(diagnostic.message);
    }
    return messages.join(QStringLiteral("\n"));
}

EpanetJsProjectConversionResult EpanetJsProjectConverter::convert(
    const EpanetJsProjectSnapshot &project)
{
    QJsonObject settings = EpanetJsProjectIdentity::projectSettingsObject(project);
    EpanetJsProjectConversionResult result;
    const QString unique_id_text = settings.value(QStringLiteral("uniqueId")).toString();
    const QUuid source_project_uuid(unique_id_text);

    QUuid project_uuid = source_project_uuid;
    if (project_uuid.isNull())
    {
        const QByteArray identity_seed = EpanetJsProjectIdentity::fallbackProjectIdentitySeed(project);
        project_uuid = EpanetJsConversionCommon::uuidV5(
            EpanetJsProjectIdentity::aowisEpanetJsNamespace(), identity_seed);
        EpanetJsConversionCommon::appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("generated-project-uuid"),
            QStringLiteral(
                "The epanet-js project has no valid settings.uniqueId; AOWIS generated a deterministic project UUID from the project contents."));
    }

    result.network.uuid = project_uuid;
    result.network.id = settings.value(QStringLiteral("name")).toString().trimmed();
    if (result.network.id.isEmpty())
        result.network.id = QStringLiteral("epanet-js-project");
    result.id_map = EpanetJsProjectIdMap(project_uuid);

    EpanetJsValidation::registerEntityIds(project, result);
    EpanetJsValidation::validateReferenceDomains(project, result);
    EpanetJsValidation::validateLinkEndpointReferences(project, result);
    EpanetJsValidation::validateProjectCoordinateStorage(settings, result);

    // Resolve shared unit defaults once, before any conversion stage runs.
    // Explicit source units always take precedence over inferred values.
    QJsonObject units = EpanetJsConversionCommon::projectUnitsObject(settings);
    const QString flow_unit = EpanetJsConversionCommon::firstUnit(
        units, QStringList{QStringLiteral("flow")});
    const bool metric = EpanetJsUnits::flowUnitUsesMetricLength(flow_unit);
    const bool customary = EpanetJsUnits::flowUnitUsesFootLength(flow_unit);
    const QString distance_unit = metric ? QStringLiteral("m")
        : customary ? QStringLiteral("ft") : QString();
    const QString diameter_unit = metric ? QStringLiteral("mm")
        : customary ? QStringLiteral("in") : QString();
    const auto resolve = [&units](const QString &key, const QStringList &alternates,
                                  const QString &fallback) {
        if (!units.value(key).toString().trimmed().isEmpty())
            return;
        const QString candidate = EpanetJsConversionCommon::firstUnit(units, alternates);
        const QString resolved = candidate.isEmpty() ? fallback : candidate;
        if (!resolved.isEmpty())
            units.insert(key, resolved);
    };
    resolve(QStringLiteral("elevation"), {QStringLiteral("head"), QStringLiteral("level")}, distance_unit);
    resolve(QStringLiteral("head"), {QStringLiteral("elevation"), QStringLiteral("level")}, distance_unit);
    resolve(QStringLiteral("level"), {QStringLiteral("elevation"), QStringLiteral("head")}, distance_unit);
    resolve(QStringLiteral("length"), {QStringLiteral("elevation")}, distance_unit);
    resolve(QStringLiteral("diameter"), {}, diameter_unit);
    resolve(QStringLiteral("pressure"), {}, metric ? QStringLiteral("m")
        : customary ? QStringLiteral("psi") : QString());
    if (!distance_unit.isEmpty())
    {
        // Source metadata was incomplete; defaults follow the declared EPANET flow family.
        const QJsonObject original_units = EpanetJsConversionCommon::projectUnitsObject(settings);
        for (const QString &key : {QStringLiteral("elevation"), QStringLiteral("head"),
                                   QStringLiteral("length"), QStringLiteral("diameter"),
                                   QStringLiteral("pressure")})
        {
            if (original_units.value(key).toString().trimmed().isEmpty())
                EpanetJsConversionCommon::appendDiagnostic(
                    result, EpanetJsConversionDiagnosticSeverity::Warning,
                    QStringLiteral("inferred-unit"),
                    QStringLiteral("Missing epanet-js unit '%1'; using '%2'.")
                        .arg(key, units.value(key).toString()));
        }
    }
    settings.insert(QStringLiteral("units"), units);

    const QJsonObject simulation_settings = EpanetJsSettings::simulationSettingsObject(project, result);

    EpanetJsSettings::mapHeadlossFormula(settings, result);
    EpanetJsSettings::importPipeMaterials(project, settings, result);
    EpanetJsPatternsCurves::importPatterns(project, result);
    EpanetJsPatternsCurves::importCurves(project, settings, result);
    EpanetJsSettings::mapSimulationSettings(simulation_settings, settings, result);
    EpanetJsNodes::importNodes(project, settings, result);
    EpanetJsNodes::applyEmitterSettings(project, settings, simulation_settings, result);
    EpanetJsSettings::mapQualitySimulationSettings(simulation_settings, settings, result);
    EpanetJsPipes::importPipes(project, settings, result);
    EpanetJsWaterQuality::importWaterQualityEntityData(project, settings, simulation_settings, result);
    EpanetJsDemandPoints::importCustomerPoints(project, settings, result);
    EpanetJsPumpsValves::importPumpsAndValves(project, settings, result);
    EpanetJsControls::importControls(project, settings, result);

    if (result.id_map.size() == 0)
    {
        EpanetJsConversionCommon::appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("no-convertible-entities"),
            QStringLiteral("The epanet-js project contains no entity ids that AOWIS can convert."));
    }

    result.success = !result.hasErrors();
    return result;
}
