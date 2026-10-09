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

namespace
{
// Explicit source units always win. General quantities fall back to sibling
// keys, then to the epanet-js preset of the EPANET unit system that the flow
// unit selects (libs/project-settings quantities-spec.ts: metricSpec and
// usCustomarySpec). Quantity-specific keys follow their general quantity.
QJsonObject resolveProjectUnits(
    const QJsonObject &source_units,
    const QJsonObject &simulation_settings,
    EpanetJsProjectConversionResult &result)
{
    QJsonObject units = source_units;

    // epanet-js passes simulation_settings.qualityMassUnit to EPANET's QUALITY
    // option (build-inp.ts buildQualityValue). units.chemicalConcentration is
    // only copied from it when a project is created and goes stale when the
    // mass unit is changed later, so the simulation setting is authoritative.
    const QString quality_mass_unit = simulation_settings.value(
        QStringLiteral("qualityMassUnit")).toString().trimmed();
    if (!quality_mass_unit.isEmpty())
        units.insert(QStringLiteral("chemicalConcentration"), quality_mass_unit);

    const auto resolve = [&units](const QString &key, const QStringList &alternates,
                                  const QString &fallback) {
        if (!units.value(key).toString().trimmed().isEmpty())
            return;
        const QString candidate = EpanetJsConversionCommon::firstUnit(units, alternates);
        const QString resolved = candidate.isEmpty() ? fallback : candidate;
        if (!resolved.isEmpty())
            units.insert(key, resolved);
    };

    // The flow unit selects the EPANET unit system (epanet-js chooseUnitSystem).
    resolve(QStringLiteral("flow"),
            {QStringLiteral("baseDemand"), QStringLiteral("customerDemand")}, QString());
    const QString flow_unit = units.value(QStringLiteral("flow")).toString();
    const bool metric = EpanetJsUnits::flowUnitUsesMetricLength(flow_unit);
    const bool customary = EpanetJsUnits::flowUnitUsesFootLength(flow_unit);
    const auto by_system = [metric, customary](const QString &metric_unit,
                                               const QString &customary_unit) {
        return metric ? metric_unit : customary ? customary_unit : QString();
    };

    // General quantities.
    resolve(QStringLiteral("elevation"), {QStringLiteral("head"), QStringLiteral("level")},
            by_system(QStringLiteral("m"), QStringLiteral("ft")));
    resolve(QStringLiteral("head"), {QStringLiteral("elevation"), QStringLiteral("level")},
            by_system(QStringLiteral("m"), QStringLiteral("ft")));
    resolve(QStringLiteral("level"), {QStringLiteral("elevation"), QStringLiteral("head")},
            by_system(QStringLiteral("m"), QStringLiteral("ft")));
    resolve(QStringLiteral("length"), {QStringLiteral("elevation")},
            by_system(QStringLiteral("m"), QStringLiteral("ft")));
    resolve(QStringLiteral("headloss"), {QStringLiteral("head")},
            by_system(QStringLiteral("m"), QStringLiteral("ft")));
    resolve(QStringLiteral("diameter"), {},
            by_system(QStringLiteral("mm"), QStringLiteral("in")));
    resolve(QStringLiteral("pressure"), {},
            by_system(QStringLiteral("mwc"), QStringLiteral("psi")));
    // epanet-js stores tank min_volume as 0.0 rather than NULL, so every tank
    // needs a resolvable volume unit even when no explicit volume is used.
    resolve(QStringLiteral("volume"), {QStringLiteral("minVolume")},
            by_system(QStringLiteral("m^3"), QStringLiteral("ft^3")));
    resolve(QStringLiteral("power"), {},
            by_system(QStringLiteral("kW"), QStringLiteral("hp")));

    if (metric || customary)
    {
        // Source metadata was incomplete; defaults follow the declared EPANET flow family.
        const QStringList general_keys = {
            QStringLiteral("elevation"), QStringLiteral("head"), QStringLiteral("level"),
            QStringLiteral("length"), QStringLiteral("headloss"), QStringLiteral("diameter"),
            QStringLiteral("pressure"), QStringLiteral("volume"), QStringLiteral("power")};
        for (const QString &key : general_keys)
        {
            if (source_units.value(key).toString().trimmed().isEmpty())
                EpanetJsConversionCommon::appendDiagnostic(
                    result, EpanetJsConversionDiagnosticSeverity::Warning,
                    QStringLiteral("inferred-unit"),
                    QStringLiteral("Missing epanet-js unit '%1'; using '%2'.")
                        .arg(key, units.value(key).toString()));
        }
    }

    // Quantity-specific keys follow their general quantity.
    resolve(QStringLiteral("baseDemand"), {QStringLiteral("flow")}, QString());
    resolve(QStringLiteral("customerDemand"), {QStringLiteral("baseDemand")}, QString());
    resolve(QStringLiteral("initialLevel"), {QStringLiteral("level")}, QString());
    resolve(QStringLiteral("minLevel"), {QStringLiteral("level")}, QString());
    resolve(QStringLiteral("maxLevel"), {QStringLiteral("level")}, QString());
    resolve(QStringLiteral("tankDiameter"), {QStringLiteral("length")}, QString());
    resolve(QStringLiteral("minVolume"), {QStringLiteral("volume")}, QString());
    return units;
}
}

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

    const QJsonObject simulation_settings = EpanetJsSettings::simulationSettingsObject(project, result);

    // Resolve every unit key once, before any conversion stage runs. Stages
    // read exactly one key each and never apply fallbacks of their own.
    settings.insert(
        QStringLiteral("units"),
        resolveProjectUnits(
            EpanetJsConversionCommon::projectUnitsObject(settings), simulation_settings, result));

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
