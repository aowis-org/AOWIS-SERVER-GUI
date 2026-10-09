#include "import/epanet_js_project_converter.h"
#include "import/epanet_js_units.h"
#include "test_harness.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>
#include <cmath>

namespace
{
AowisTestHarness test_harness;

void expectTrue(bool condition, const char *message)
{
    test_harness.expectTrue(condition, message);
}

void expectNear(double actual, double expected, double tolerance, const char *message)
{
    test_harness.expectNear(actual, expected, tolerance, message);
}

EpanetJsTableSnapshot makeTable(
    const QString &name,
    const QStringList &columns,
    const QList<QVariantMap> &rows)
{
    EpanetJsTableSnapshot table;
    table.name = name;
    table.columns = columns;
    table.rows = rows;
    return table;
}

EpanetJsProjectSnapshot makeProjectWithUniqueId(const QString &unique_id)
{
    EpanetJsProjectSnapshot project;

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("converter-test"));
    settings.insert(QStringLiteral("uniqueId"), unique_id);

    const QVariantMap project_row = {
        {QStringLiteral("id"), 1},
        {QStringLiteral("settings"), QString::fromUtf8(
            QJsonDocument(settings).toJson(QJsonDocument::Compact))}
    };
    project.tables.insert(
        QStringLiteral("project"),
        makeTable(
            QStringLiteral("project"),
            {QStringLiteral("id"), QStringLiteral("settings")},
            {project_row}));

    const QVariantMap junction_1 = {
        {QStringLiteral("id"), 1},
        {QStringLiteral("label"), QStringLiteral("J1")},
        {QStringLiteral("coord_x"), 18.0},
        {QStringLiteral("coord_y"), 11.0}
    };
    const QVariantMap junction_2 = {
        {QStringLiteral("id"), 2},
        {QStringLiteral("label"), QStringLiteral("J2")},
        {QStringLiteral("coord_x"), 18.1},
        {QStringLiteral("coord_y"), 11.1}
    };
    project.tables.insert(
        QStringLiteral("junctions"),
        makeTable(
            QStringLiteral("junctions"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y")},
            {junction_1, junction_2}));

    const QVariantMap pipe = {
        {QStringLiteral("id"), 10},
        {QStringLiteral("label"), QStringLiteral("P1")},
        {QStringLiteral("start_node_id"), 1},
        {QStringLiteral("end_node_id"), 2}
    };
    project.tables.insert(
        QStringLiteral("pipes"),
        makeTable(
            QStringLiteral("pipes"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("start_node_id"), QStringLiteral("end_node_id")},
            {pipe}));

    return project;
}


void setProjectSettings(EpanetJsProjectSnapshot &project, const QJsonObject &settings)
{
    EpanetJsTableSnapshot table = project.tables.value(QStringLiteral("project"));
    if (table.rows.isEmpty())
        table.rows.append(QVariantMap());
    table.rows[0].insert(
        QStringLiteral("settings"),
        QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
    project.tables.insert(QStringLiteral("project"), table);
}

void setPipeLibrary(EpanetJsProjectSnapshot &project, const QJsonArray &materials)
{
    EpanetJsTableSnapshot table = project.tables.value(QStringLiteral("project"));
    if (table.rows.isEmpty())
        table.rows.append(QVariantMap());
    if (!table.columns.contains(QStringLiteral("pipe_library")))
        table.columns.append(QStringLiteral("pipe_library"));
    table.rows[0].insert(
        QStringLiteral("pipe_library"),
        QString::fromUtf8(QJsonDocument(materials).toJson(QJsonDocument::Compact)));
    project.tables.insert(QStringLiteral("project"), table);
}

QJsonObject pipeMaterialEntry(int age_years, double roughness)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("age"), age_years);
    entry.insert(QStringLiteral("roughness"), roughness);
    return entry;
}

QJsonObject pipeMaterialDefinition(
    const QString &label,
    const QList<QPair<int, double>> &entries)
{
    QJsonArray roughness_entries;
    for (const QPair<int, double> &entry : entries)
        roughness_entries.append(pipeMaterialEntry(entry.first, entry.second));

    QJsonObject material;
    material.insert(QStringLiteral("label"), label);
    material.insert(QStringLiteral("entries"), roughness_entries);
    return material;
}

void setSimulationSettings(EpanetJsProjectSnapshot &project, const QJsonObject &settings)
{
    const QVariantMap row = {
        {QStringLiteral("id"), 1},
        {QStringLiteral("data"), QString::fromUtf8(
            QJsonDocument(settings).toJson(QJsonDocument::Compact))}
    };
    project.tables.insert(
        QStringLiteral("simulation_settings"),
        makeTable(
            QStringLiteral("simulation_settings"),
            {QStringLiteral("id"), QStringLiteral("data")},
            {row}));
}

bool hasDiagnosticCode(
    const EpanetJsProjectConversionResult &result,
    const QString &code)
{
    for (const EpanetJsConversionDiagnostic &diagnostic : result.diagnostics)
    {
        if (diagnostic.code == code)
            return true;
    }
    return false;
}

#include "epanet_js_converter_identity.inc"
#include "epanet_js_converter_settings_nodes.inc"
#include "epanet_js_converter_quality.inc"
#include "epanet_js_converter_correctness_regression.inc"
#include "epanet_js_converter_demand_materials_pipes.inc"
#include "epanet_js_converter_pumps_valves.inc"
#include "epanet_js_converter_controls.inc"
#include "epanet_js_converter_unit_reference.inc"

}

int main()
{
    test_harness.runCase(
        "stable epanet-js id mapping",
        testUsesSourceProjectIdentityAndStableEntityUuids);
    test_harness.runCase(
        "epanet-js named projected project internal coordinates",
        testAcceptsNamedProjectedProjectWithStoredWgs84Coordinates);
    test_harness.runCase(
        "epanet-js local X-Y grid rejection",
        testRejectsNonGeoreferencedLocalGridProject);
    test_harness.runCase(
        "fallback epanet-js project identity",
        testGeneratesStableFallbackProjectIdentity);
    test_harness.runCase(
        "invalid epanet-js source ids",
        testRejectsInvalidAndDuplicateSourceIds);
    test_harness.runCase(
        "missing epanet-js node reference",
        testRejectsMissingEndpointReference);
    test_harness.runCase(
        "ambiguous epanet-js node ids",
        testRejectsAmbiguousNodeReferenceDomain);
    test_harness.runCase(
        "epanet-js project settings and patterns",
        testImportsProjectSettingsAndPatterns);
    test_harness.runCase(
        "epanet-js typed curves",
        testImportsTypedCurvesInCanonicalUnits);
    test_harness.runCase(
        "epanet-js nodes and native junction demands",
        testImportsNodesAndNativeDemands);
    test_harness.runCase(
        "epanet-js US node and demand units",
        testConvertsUsNodeAndDemandUnits);
    test_harness.runCase(
        "epanet-js tank zero minimum volume",
        testTankZeroMinimumVolumeIncludesBottomWaterColumn);
    test_harness.runCase(
        "epanet-js global first-order reactions with unknown concentration unit",
        testGlobalFirstOrderReactionsSurviveUnrecognizedChemicalUnit);
    test_harness.runCase(
        "epanet-js global reactions with Greek-mu concentration unit",
        testGlobalReactionsWithGreekMuConcentrationUnit);
    test_harness.runCase(
        "epanet-js water-quality simulation settings",
        testImportsWaterQualitySimulationSettings);
    test_harness.runCase(
        "epanet-js water-quality entity data",
        testImportsWaterQualityEntityData);
    test_harness.runCase(
        "epanet-js tank mixing models",
        testImportsAllTankMixingModels);
    test_harness.runCase(
        "epanet-js water-age initial quality",
        testImportsWaterAgeInitialQuality);
    test_harness.runCase(
        "epanet-js invalid source-trace node",
        testRejectsInvalidWaterQualityTraceNode);
    test_harness.runCase(
        "broken epanet-js node references",
        testRejectsBrokenNodeReferences);
    test_harness.runCase(
        "epanet-js customer points and demands",
        testImportsCustomerPointsWithAssignedJunctionDemands);
    test_harness.runCase(
        "epanet-js US customer-demand units",
        testImportsUsCustomerDemandUnits);
    test_harness.runCase(
        "epanet-js invalid customer assigned junction",
        testRejectsCustomerAssignedJunctionOutsidePipeEndpoints);
    test_harness.runCase(
        "epanet-js embedded pipe material library",
        testImportsPipeMaterialLibraryAndAssignsNullRoughnessPipes);
    test_harness.runCase(
        "epanet-js missing material for NULL pipe roughness",
        testRejectsMissingMaterialForNullRoughnessPipe);
    test_harness.runCase(
        "epanet-js pipes",
        testImportsPipes);
    test_harness.runCase(
        "epanet-js US pipe units and Darcy roughness",
        testConvertsUsPipeUnitsAndDarcyRoughness);
    test_harness.runCase(
        "malformed epanet-js pipe data",
        testRejectsMalformedPipeData);
    test_harness.runCase(
        "epanet-js pumps and valves",
        testImportsPumpsAndValves);
    test_harness.runCase(
        "epanet-js US pump and valve units",
        testConvertsUsPumpAndValveUnits);
    test_harness.runCase(
        "epanet-js referenced pump head curve",
        testImportsReferencedPumpHeadCurve);
    test_harness.runCase(
        "epanet-js valve types and curves",
        testImportsAllValveTypesAndCurves);
    test_harness.runCase(
        "epanet-js structured pump level-setting controls",
        testImportsStructuredPumpLevelSettingControls);
    test_harness.runCase(
        "broken epanet-js structured control references",
        testRejectsBrokenStructuredLevelSettingReference);
    test_harness.runCase(
        "epanet-js raw simple level controls",
        testImportsRawSimpleLevelControls);
    test_harness.runCase(
        "epanet-js raw EPANET rules",
        testImportsRawRules);
    test_harness.runCase(
        "unsupported epanet-js raw POWER premise",
        testRejectsUnsupportedRawRulePowerPremise);
    test_harness.runCase(
        "broken epanet-js pump and valve references",
        testRejectsBrokenPumpAndValveReferences);
    test_harness.runCase(
        "malformed optional epanet-js operational data",
        testSkipsMalformedOptionalOperationalData);
    test_harness.runCase(
        "epanet-js independent SI unit reference values",
        testIndependentUnitReferenceValues);
    return test_harness.finish();
}
