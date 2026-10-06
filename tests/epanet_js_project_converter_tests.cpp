#include "import/epanet_js_project_converter.h"
#include "test_harness.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

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

void testUsesSourceProjectIdentityAndStableEntityUuids()
{
    const QString unique_id = QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded");
    const EpanetJsProjectSnapshot project = makeProjectWithUniqueId(unique_id);

    const EpanetJsProjectConversionResult first = EpanetJsProjectConverter::convert(project);
    const EpanetJsProjectConversionResult second = EpanetJsProjectConverter::convert(project);

    expectTrue(first.success, "converter accepts a structurally valid project contract");
    expectTrue(first.network.uuid == QUuid(unique_id),
               "converter preserves epanet-js settings.uniqueId as the AOWIS project UUID");
    expectTrue(first.network.id == QStringLiteral("converter-test"),
               "converter preserves epanet-js project name as the initial AOWIS network id");
    expectTrue(first.id_map.size() == 3,
               "converter registers deterministic ids for known source entities");

    const QUuid junction_1_uuid = first.id_map.uuidFor(QStringLiteral("junctions"), 1);
    const QUuid junction_2_uuid = first.id_map.uuidFor(QStringLiteral("junctions"), 2);
    const QUuid pipe_uuid = first.id_map.uuidFor(QStringLiteral("pipes"), 10);
    expectTrue(!junction_1_uuid.isNull(), "junction source id resolves to an AOWIS UUID");
    expectTrue(junction_1_uuid != junction_2_uuid,
               "different source ids receive different AOWIS UUIDs");
    expectTrue(junction_1_uuid != pipe_uuid,
               "different source entity domains receive different AOWIS UUIDs");
    expectTrue(junction_1_uuid == second.id_map.uuidFor(QStringLiteral("junctions"), 1),
               "re-importing the same project produces the same entity UUID");
    expectTrue(first.id_map.nodeUuid(1) == junction_1_uuid,
               "generic node references resolve through the registered node domain");
    expectTrue(first.id_map.linkUuid(10) == pipe_uuid,
               "generic link references resolve through the registered link domain");
}

void testAcceptsNamedProjectedProjectWithStoredWgs84Coordinates()
{
    const QString unique_id = QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded");
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(unique_id);

    QJsonObject projection;
    projection.insert(QStringLiteral("type"), QStringLiteral("epsg"));
    projection.insert(QStringLiteral("id"), QStringLiteral("EPSG:3089"));
    projection.insert(
        QStringLiteral("name"),
        QStringLiteral("NAD83 / Kentucky Single Zone (ftUS)"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("projected-project-test"));
    settings.insert(QStringLiteral("uniqueId"), unique_id);
    settings.insert(QStringLiteral("projection"), projection);
    setProjectSettings(project, settings);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success,
               "named projected epanet-js project imports because .ejsdb stores map-based coordinates as longitude/latitude");
    expectTrue(!hasDiagnosticCode(result, QStringLiteral("unsupported-coordinate-projection")),
               "named projected epanet-js project is not rejected merely because its export CRS is not WGS84");
    expectTrue(result.network.nodes_junctions.size() == 2,
               "named projected epanet-js project preserves its junctions");
    if (result.network.nodes_junctions.size() == 2)
    {
        expectNear(result.network.nodes_junctions.at(0).coordinate_wgs84.longitude_deg, 18.0, 1e-12,
                   "named projected project keeps stored longitude without a second projection transform");
        expectNear(result.network.nodes_junctions.at(0).coordinate_wgs84.latitude_deg, 11.0, 1e-12,
                   "named projected project keeps stored latitude without a second projection transform");
        expectNear(result.network.nodes_junctions.at(1).coordinate_wgs84.longitude_deg, 18.1, 1e-12,
                   "named projected project keeps second stored longitude without a second projection transform");
        expectNear(result.network.nodes_junctions.at(1).coordinate_wgs84.latitude_deg, 11.1, 1e-12,
                   "named projected project keeps second stored latitude without a second projection transform");
    }
}

void testRejectsNonGeoreferencedLocalGridProject()
{
    const QString unique_id = QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded");
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(unique_id);

    QJsonObject projection;
    projection.insert(QStringLiteral("type"), QStringLiteral("xy-grid"));
    projection.insert(QStringLiteral("id"), QStringLiteral("local-grid"));
    projection.insert(QStringLiteral("name"), QStringLiteral("Simple X-Y Grid"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("local-grid-test"));
    settings.insert(QStringLiteral("uniqueId"), unique_id);
    settings.insert(QStringLiteral("projection"), projection);
    setProjectSettings(project, settings);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(!result.success,
               "non-georeferenced epanet-js X-Y grid does not silently become geographic WGS84 geometry");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("unsupported-local-grid-projection")),
               "non-georeferenced X-Y grid reports its dedicated unsupported projection diagnostic");
}

void testGeneratesStableFallbackProjectIdentity()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(QString());
    const EpanetJsTableSnapshot project_table = project.tables.take(QStringLiteral("project"));
    Q_UNUSED(project_table);

    const EpanetJsProjectConversionResult first = EpanetJsProjectConverter::convert(project);
    const EpanetJsProjectConversionResult second = EpanetJsProjectConverter::convert(project);

    expectTrue(first.success,
               "converter accepts a recognizable legacy project without settings.uniqueId");
    expectTrue(!first.network.uuid.isNull(),
               "converter generates a project UUID when epanet-js has no uniqueId");
    expectTrue(first.network.uuid == second.network.uuid,
               "fallback project UUID is deterministic for identical project contents");
    expectTrue(hasDiagnosticCode(first, QStringLiteral("generated-project-uuid")),
               "fallback project identity is reported diagnostically");
}

void testRejectsInvalidAndDuplicateSourceIds()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    EpanetJsTableSnapshot junctions = project.tables.value(QStringLiteral("junctions"));
    QVariantMap invalid = junctions.rows.first();
    invalid.insert(QStringLiteral("id"), QStringLiteral("not-an-integer"));
    junctions.rows.append(invalid);

    QVariantMap duplicate = junctions.rows.first();
    duplicate.insert(QStringLiteral("label"), QStringLiteral("J1-duplicate"));
    junctions.rows.append(duplicate);
    project.tables.insert(QStringLiteral("junctions"), junctions);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(!result.success, "converter rejects malformed source entity ids");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("invalid-source-id")),
               "non-integer source ids produce a dedicated diagnostic");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("duplicate-source-id")),
               "duplicate source ids produce a dedicated diagnostic");
}

void testRejectsMissingEndpointReference()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
    pipes.rows[0].insert(QStringLiteral("end_node_id"), 999);
    project.tables.insert(QStringLiteral("pipes"), pipes);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(!result.success, "converter rejects links that reference missing source nodes");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("missing-node-reference")),
               "missing endpoint references produce a dedicated diagnostic");
}

void testRejectsAmbiguousNodeReferenceDomain()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    const QVariantMap reservoir = {
        {QStringLiteral("id"), 1},
        {QStringLiteral("label"), QStringLiteral("R1")},
        {QStringLiteral("coord_x"), 18.0},
        {QStringLiteral("coord_y"), 11.0}
    };
    project.tables.insert(
        QStringLiteral("reservoirs"),
        makeTable(
            QStringLiteral("reservoirs"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y")},
            {reservoir}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(!result.success,
               "converter rejects a numeric node id shared by multiple epanet-js node tables");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("ambiguous-node-id")),
               "ambiguous generic node references are diagnosed before entity conversion");
    expectTrue(result.id_map.nodeUuid(1).isNull(),
               "ambiguous node source ids cannot resolve to an arbitrary AOWIS UUID");
}

void testImportsProjectSettingsAndPatterns()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("gpm"));
    units.insert(QStringLiteral("pressure"), QStringLiteral("psi"));
    units.insert(QStringLiteral("head"), QStringLiteral("ft"));
    units.insert(QStringLiteral("level"), QStringLiteral("ft"));
    units.insert(QStringLiteral("volume"), QStringLiteral("ft^3"));
    units.insert(QStringLiteral("chemicalConcentration"), QStringLiteral("mg/L"));

    QJsonObject project_settings;
    project_settings.insert(QStringLiteral("name"), QStringLiteral("settings-test"));
    project_settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    project_settings.insert(QStringLiteral("headlossFormula"), QStringLiteral("D-W"));
    project_settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, project_settings);

    const QVariantMap pattern_row = {
        {QStringLiteral("id"), 3},
        {QStringLiteral("label"), QStringLiteral("TARIFF")},
        {QStringLiteral("type"), QStringLiteral("energyPrice")},
        {QStringLiteral("multipliers"), QStringLiteral("[1,0.5,2]")}
    };
    project.tables.insert(
        QStringLiteral("patterns"),
        makeTable(
            QStringLiteral("patterns"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("type"),
             QStringLiteral("multipliers")},
            {pattern_row}));

    QJsonObject timing;
    timing.insert(QStringLiteral("duration"), 7200);
    timing.insert(QStringLiteral("hydraulicTimestep"), 900);
    timing.insert(QStringLiteral("qualityTimestep"), 60);
    timing.insert(QStringLiteral("patternTimestep"), 1800);
    timing.insert(QStringLiteral("patternStart"), 300);
    timing.insert(QStringLiteral("reportTimestep"), 1200);
    timing.insert(QStringLiteral("reportStart"), 600);
    timing.insert(QStringLiteral("ruleTimestep"), 120);
    timing.insert(QStringLiteral("startClockTime"), 3600);

    QJsonObject simulation;
    simulation.insert(QStringLiteral("timing"), timing);
    simulation.insert(QStringLiteral("globalDemandMultiplier"), 1.25);
    simulation.insert(QStringLiteral("demandModel"), QStringLiteral("PDA"));
    simulation.insert(QStringLiteral("minimumPressure"), 10.0);
    simulation.insert(QStringLiteral("requiredPressure"), 30.0);
    simulation.insert(QStringLiteral("pressureExponent"), 0.6);
    simulation.insert(QStringLiteral("backflowAllowed"), true);
    simulation.insert(QStringLiteral("accuracy"), 0.0005);
    simulation.insert(QStringLiteral("specificGravity"), 1.2);
    simulation.insert(QStringLiteral("viscosity"), 1.1);
    simulation.insert(QStringLiteral("unbalancedMode"), QStringLiteral("CONTINUE"));
    simulation.insert(QStringLiteral("unbalancedExtraTrials"), 7);
    simulation.insert(QStringLiteral("maximumTrials"), 123);
    simulation.insert(QStringLiteral("checkFrequency"), 3);
    simulation.insert(QStringLiteral("maximumCheck"), 8);
    simulation.insert(QStringLiteral("dampingLimit"), 0.42);
    simulation.insert(QStringLiteral("maximumHeadError"), 2.5);
    simulation.insert(QStringLiteral("maximumFlowChange"), 10.0);
    simulation.insert(QStringLiteral("reactionBulkOrder"), 1.2);
    simulation.insert(QStringLiteral("reactionWallOrder"), 0.8);
    simulation.insert(QStringLiteral("reactionTankOrder"), 1.1);
    simulation.insert(QStringLiteral("reactionGlobalBulk"), -0.2);
    simulation.insert(QStringLiteral("reactionGlobalWall"), -0.1);
    simulation.insert(QStringLiteral("reactionLimitingPotential"), 3.5);
    simulation.insert(QStringLiteral("reactionRoughnessCorrelation"), 0.25);
    simulation.insert(QStringLiteral("reportEnergy"), true);
    simulation.insert(QStringLiteral("energyGlobalEfficiency"), 82.0);
    simulation.insert(QStringLiteral("energyGlobalPrice"), 0.19);
    simulation.insert(QStringLiteral("energyGlobalPatternId"), 3);
    simulation.insert(QStringLiteral("energyDemandCharge"), 12.5);
    simulation.insert(QStringLiteral("statusReport"), QStringLiteral("FULL"));
    simulation.insert(QStringLiteral("reportStatistic"), QStringLiteral("AVERAGE"));
    setSimulationSettings(project, simulation);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter accepts supported epanet-js project and simulation settings");
    expectTrue(result.network.options_hydraulic.headloss_formula == HydraulicHeadlossFormula::DarcyWeisbach,
               "project headloss formula is mapped");
    expectTrue(result.network.duration_s == 7200, "duration is imported in seconds");
    expectTrue(result.network.timestep_hydraulic_s == 900, "hydraulic timestep is imported");
    expectTrue(result.network.timestep_quality_s == 60, "quality timestep is imported when present");
    expectTrue(result.network.timestep_pattern_s == 1800, "pattern timestep is imported");
    expectTrue(result.network.start_pattern_s == 300, "pattern start is imported");
    expectTrue(result.network.timestep_report_s == 1200, "report timestep is imported");
    expectTrue(result.network.start_report_s == 600, "report start is imported");
    expectTrue(result.network.timestep_rule_s == 120, "rule timestep is imported");
    expectTrue(result.network.start_time_of_day_s == 3600, "start clock time is imported");
    expectNear(result.network.options_hydraulic.demand_multiplier, 1.25, 1e-12,
               "global demand multiplier is imported");
    expectTrue(result.network.options_hydraulic.demand_model == HydraulicDemandModel::PressureDriven,
               "PDA demand model is imported");
    expectNear(result.network.options_hydraulic.minimum_pressure_head_m, 5.861989383798753, 1e-9,
               "minimum psi pressure is converted to metres of head using specific gravity");
    expectNear(result.network.options_hydraulic.required_pressure_head_m, 17.585968151396262, 1e-9,
               "required psi pressure is converted to metres of head using specific gravity");
    expectNear(result.network.options_hydraulic.pressure_exponent, 0.6, 1e-12,
               "pressure exponent is imported");
    expectTrue(result.network.options_hydraulic.emitters_can_backflow,
               "backflow setting is imported");
    expectNear(result.network.options_hydraulic.accuracy, 0.0005, 1e-12,
               "hydraulic accuracy is imported");
    expectNear(result.network.options_hydraulic.specific_gravity, 1.2, 1e-12,
               "specific gravity is imported before pressure conversion");
    expectNear(result.network.options_hydraulic.relative_viscosity, 1.1, 1e-12,
               "relative viscosity is imported");
    expectTrue(result.network.options_hydraulic.unbalanced_action == HydraulicUnbalancedAction::Continue,
               "unbalanced continue mode is imported");
    expectTrue(result.network.options_hydraulic.unbalanced_extra_trials == 7,
               "unbalanced extra trials are imported");
    expectTrue(result.network.options_hydraulic.maximum_trials == 123,
               "maximum hydraulic trials are imported");
    expectTrue(result.network.options_hydraulic.check_frequency == 3,
               "hydraulic status check frequency is imported");
    expectTrue(result.network.options_hydraulic.maximum_check == 8,
               "maximum hydraulic status checks are imported");
    expectNear(result.network.options_hydraulic.damping_limit, 0.42, 1e-12,
               "hydraulic damping limit is imported");
    expectNear(result.network.options_hydraulic.maximum_head_error_m, 0.762, 1e-12,
               "maximum head error feet are converted to metres");
    expectNear(result.network.options_hydraulic.maximum_flow_change_m3_per_h, 2.2712470704, 1e-12,
               "maximum flow change gpm is converted to m3/h");
    expectNear(result.network.options_reaction.global_pipe_bulk_reaction.order, 1.2, 1e-12,
               "global pipe bulk order is imported");
    expectNear(result.network.options_reaction.global_pipe_wall_reaction.order, 0.8, 1e-12,
               "global pipe wall order is imported");
    expectNear(result.network.options_reaction.global_tank_bulk_reaction.order, 1.1, 1e-12,
               "global tank reaction order is imported");
    expectNear(result.network.options_reaction.global_pipe_bulk_reaction.coefficient, -0.2, 1e-12,
               "global pipe bulk coefficient is imported");
    expectNear(result.network.options_reaction.global_pipe_wall_reaction.coefficient, -0.03048, 1e-12,
               "US wall reaction coefficient is converted to canonical metres");
    expectNear(result.network.options_reaction.limiting_concentration_mg_per_l, 3.5, 1e-12,
               "reaction limiting potential is imported");
    expectNear(result.network.options_reaction.roughness_reaction_factor, 0.0762, 1e-12,
               "US roughness reaction factor is converted to canonical metres");
    expectTrue(!result.network.options_report.summary,
               "epanet-js report summary tables remain disabled");
    expectTrue(result.network.options_report.energy, "energy report flag is imported");
    expectTrue(result.network.options_report.status == HydraulicSimulationReportStatus::Full,
               "full status report mode is imported");
    expectTrue(result.network.report_statistic == HydraulicSimulationReportStatistic::Average,
               "report statistic is imported");
    expectNear(result.network.options_energy.global_pump_efficiency_percent, 82.0, 1e-12,
               "global pump efficiency is imported");
    expectNear(result.network.options_energy.global_energy_price_per_kw_h, 0.19, 1e-12,
               "global energy price is imported");
    expectNear(result.network.options_energy.demand_charge_per_kw, 12.5, 1e-12,
               "global demand charge is imported");

    expectTrue(result.network.patterns_time.size() == 1,
               "epanet-js pattern rows are imported");
    if (result.network.patterns_time.size() == 1)
    {
        const HydraulicPatternTime &pattern = result.network.patterns_time.first();
        expectTrue(pattern.id == QStringLiteral("TARIFF"), "pattern label becomes AOWIS pattern id");
        expectTrue(pattern.uuid == result.id_map.uuidFor(QStringLiteral("patterns"), 3),
                   "pattern keeps deterministic mapped UUID");
        expectTrue(pattern.multipliers.size() == 3, "all pattern multipliers are imported");
        if (pattern.multipliers.size() == 3)
        {
            expectNear(pattern.multipliers.at(0), 1.0, 1e-12, "first pattern multiplier is retained");
            expectNear(pattern.multipliers.at(1), 0.5, 1e-12, "second pattern multiplier is retained");
            expectNear(pattern.multipliers.at(2), 2.0, 1e-12, "third pattern multiplier is retained");
        }
        expectTrue(result.network.options_energy.global_energy_price_pattern_uuid == pattern.uuid,
                   "global energy pattern reference resolves through the shared id map");
    }
}

void testImportsTypedCurvesInCanonicalUnits()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("gpm"));
    units.insert(QStringLiteral("head"), QStringLiteral("ft"));
    units.insert(QStringLiteral("level"), QStringLiteral("ft"));
    units.insert(QStringLiteral("volume"), QStringLiteral("ft^3"));
    QJsonObject project_settings;
    project_settings.insert(QStringLiteral("name"), QStringLiteral("curve-test"));
    project_settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    project_settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, project_settings);

    QList<QVariantMap> curves;
    curves.append({
        {QStringLiteral("id"), 5},
        {QStringLiteral("label"), QStringLiteral("PUMP-H")},
        {QStringLiteral("type"), QStringLiteral("pump")},
        {QStringLiteral("points"), QStringLiteral("[[100,50],[200,40]]")}
    });
    curves.append({
        {QStringLiteral("id"), 6},
        {QStringLiteral("label"), QStringLiteral("PUMP-E")},
        {QStringLiteral("type"), QStringLiteral("efficiency")},
        {QStringLiteral("points"), QStringLiteral("[{\"x\":100,\"y\":80}]")}
    });
    curves.append({
        {QStringLiteral("id"), 7},
        {QStringLiteral("label"), QStringLiteral("TANK-V")},
        {QStringLiteral("type"), QStringLiteral("volume")},
        {QStringLiteral("points"), QStringLiteral("{\"x\":[0,10],\"y\":[0,1000]}")}
    });
    curves.append({
        {QStringLiteral("id"), 8},
        {QStringLiteral("label"), QStringLiteral("VALVE-C")},
        {QStringLiteral("type"), QStringLiteral("valve")},
        {QStringLiteral("points"), QStringLiteral("[[0,0],[100,100]]")}
    });
    curves.append({
        {QStringLiteral("id"), 9},
        {QStringLiteral("label"), QStringLiteral("GPV-H")},
        {QStringLiteral("type"), QStringLiteral("headloss")},
        {QStringLiteral("points"), QStringLiteral("[[50,4],[100,11]]")}
    });
    curves.append({
        {QStringLiteral("id"), 10},
        {QStringLiteral("label"), QStringLiteral("UNKNOWN")},
        {QStringLiteral("type"), QVariant()},
        {QStringLiteral("points"), QStringLiteral("[[1,2],[3,4]]")}
    });
    project.tables.insert(
        QStringLiteral("curves"),
        makeTable(
            QStringLiteral("curves"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("type"),
             QStringLiteral("points")},
            curves));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter accepts all supported epanet-js curve categories");
    expectTrue(result.network.curves_pump_head.size() == 1, "pump head curve is imported");
    expectTrue(result.network.curves_pump_efficiency.size() == 1, "pump efficiency curve is imported");
    expectTrue(result.network.curves_tank_volume.size() == 1, "tank volume curve is imported");
    expectTrue(result.network.curves_valve_characteristic.size() == 1, "valve characteristic curve is imported");
    expectTrue(result.network.curves_valve_headloss.size() == 1, "valve headloss curve is imported");
    expectTrue(result.network.curves_generic.size() == 1, "uncategorized curve is imported as generic");

    if (result.network.curves_pump_head.size() == 1)
    {
        const HydraulicCurvePumpHead &curve = result.network.curves_pump_head.first();
        expectTrue(curve.uuid == result.id_map.uuidFor(QStringLiteral("curves"), 5),
                   "typed curve keeps deterministic mapped UUID");
        expectNear(curve.points.at(0).flow_m3_per_h, 22.712470704, 1e-9,
                   "pump curve GPM is converted to m3/h");
        expectNear(curve.points.at(0).head_gain_m, 15.24, 1e-12,
                   "pump curve feet of head are converted to metres");
    }
    if (result.network.curves_pump_efficiency.size() == 1)
    {
        const HydraulicCurvePumpEfficiency &curve = result.network.curves_pump_efficiency.first();
        expectNear(curve.points.at(0).flow_m3_per_h, 22.712470704, 1e-9,
                   "efficiency curve flow is converted to m3/h");
        expectNear(curve.points.at(0).efficiency_percent, 80.0, 1e-12,
                   "efficiency remains percent");
    }
    if (result.network.curves_tank_volume.size() == 1)
    {
        const HydraulicCurveTankVolume &curve = result.network.curves_tank_volume.first();
        expectNear(curve.points.at(1).water_level_m, 3.048, 1e-12,
                   "volume curve level feet are converted to metres");
        expectNear(curve.points.at(1).volume_m3, 28.316846592, 1e-12,
                   "volume curve cubic feet are converted to cubic metres");
    }
    if (result.network.curves_valve_characteristic.size() == 1)
    {
        const HydraulicCurveValveCharacteristic &curve = result.network.curves_valve_characteristic.first();
        expectNear(curve.points.at(1).position_percent, 100.0, 1e-12,
                   "valve curve position remains percent");
        expectNear(curve.points.at(1).relative_flow_percent, 100.0, 1e-12,
                   "valve curve relative flow remains percent");
    }
    if (result.network.curves_valve_headloss.size() == 1)
    {
        const HydraulicCurveValveHeadloss &curve = result.network.curves_valve_headloss.first();
        expectNear(curve.points.at(0).flow_m3_per_h, 11.356235352, 1e-9,
                   "headloss curve flow is converted to m3/h");
        expectNear(curve.points.at(0).head_loss_m, 1.2192, 1e-12,
                   "headloss curve feet are converted to metres");
    }
    if (result.network.curves_generic.size() == 1)
    {
        const HydraulicCurveGeneric &curve = result.network.curves_generic.first();
        expectNear(curve.points.at(1).x, 3.0, 1e-12,
                   "generic curve x remains opaque");
        expectNear(curve.points.at(1).y, 4.0, 1e-12,
                   "generic curve y remains opaque");
    }
}


void testImportsNodesAndNativeDemands()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("elevation"), QStringLiteral("m"));
    units.insert(QStringLiteral("head"), QStringLiteral("m"));
    units.insert(QStringLiteral("level"), QStringLiteral("m"));
    units.insert(QStringLiteral("initialLevel"), QStringLiteral("m"));
    units.insert(QStringLiteral("minLevel"), QStringLiteral("m"));
    units.insert(QStringLiteral("maxLevel"), QStringLiteral("m"));
    units.insert(QStringLiteral("tankDiameter"), QStringLiteral("m"));
    units.insert(QStringLiteral("minVolume"), QStringLiteral("m^3"));
    units.insert(QStringLiteral("volume"), QStringLiteral("m^3"));
    units.insert(QStringLiteral("baseDemand"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));

    QJsonObject projection;
    projection.insert(QStringLiteral("type"), QStringLiteral("wgs84"));
    projection.insert(QStringLiteral("id"), QStringLiteral("wgs84"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("node-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    settings.insert(QStringLiteral("projection"), projection);
    setProjectSettings(project, settings);

    const QVariantMap junction_1 = {
        {QStringLiteral("id"), 1},
        {QStringLiteral("label"), QStringLiteral("J1")},
        {QStringLiteral("is_active"), 0},
        {QStringLiteral("coord_x"), 18.1},
        {QStringLiteral("coord_y"), 11.2},
        {QStringLiteral("elevation"), 40.5}
    };
    const QVariantMap junction_2 = {
        {QStringLiteral("id"), 2},
        {QStringLiteral("label"), QStringLiteral("J2")},
        {QStringLiteral("is_active"), 1},
        {QStringLiteral("coord_x"), 18.2},
        {QStringLiteral("coord_y"), 11.3},
        {QStringLiteral("elevation"), 41.5}
    };
    project.tables.insert(
        QStringLiteral("junctions"),
        makeTable(
            QStringLiteral("junctions"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("is_active"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y"), QStringLiteral("elevation")},
            {junction_1, junction_2}));

    const QVariantMap pattern_row = {
        {QStringLiteral("id"), 5},
        {QStringLiteral("label"), QStringLiteral("RESIDENTIAL")},
        {QStringLiteral("type"), QStringLiteral("demand")},
        {QStringLiteral("multipliers"), QStringLiteral("[1,0.5]")}
    };
    project.tables.insert(
        QStringLiteral("patterns"),
        makeTable(
            QStringLiteral("patterns"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("type"),
             QStringLiteral("multipliers")},
            {pattern_row}));

    const QVariantMap volume_curve = {
        {QStringLiteral("id"), 6},
        {QStringLiteral("label"), QStringLiteral("TANK-VOLUME")},
        {QStringLiteral("type"), QStringLiteral("volume")},
        {QStringLiteral("points"), QStringLiteral("[[0,25],[8,650]]")}
    };
    project.tables.insert(
        QStringLiteral("curves"),
        makeTable(
            QStringLiteral("curves"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("type"),
             QStringLiteral("points")},
            {volume_curve}));

    const QVariantMap reservoir = {
        {QStringLiteral("id"), 3},
        {QStringLiteral("label"), QStringLiteral("R1")},
        {QStringLiteral("is_active"), 1},
        {QStringLiteral("coord_x"), 18.3},
        {QStringLiteral("coord_y"), 11.4},
        {QStringLiteral("head"), 100.0},
        {QStringLiteral("head_pattern_id"), 5}
    };
    project.tables.insert(
        QStringLiteral("reservoirs"),
        makeTable(
            QStringLiteral("reservoirs"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("is_active"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y"), QStringLiteral("head"),
             QStringLiteral("head_pattern_id")},
            {reservoir}));

    const QVariantMap tank = {
        {QStringLiteral("id"), 4},
        {QStringLiteral("label"), QStringLiteral("T1")},
        {QStringLiteral("is_active"), 1},
        {QStringLiteral("coord_x"), 18.4},
        {QStringLiteral("coord_y"), 11.5},
        {QStringLiteral("elevation"), 70.0},
        {QStringLiteral("initial_level"), 5.0},
        {QStringLiteral("min_level"), 1.0},
        {QStringLiteral("max_level"), 8.0},
        {QStringLiteral("min_volume"), 25.0},
        {QStringLiteral("diameter"), 10.0},
        {QStringLiteral("overflow"), 1},
        {QStringLiteral("volume_curve_id"), 6}
    };
    project.tables.insert(
        QStringLiteral("tanks"),
        makeTable(
            QStringLiteral("tanks"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("is_active"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y"), QStringLiteral("elevation"),
             QStringLiteral("initial_level"), QStringLiteral("min_level"),
             QStringLiteral("max_level"), QStringLiteral("min_volume"),
             QStringLiteral("diameter"), QStringLiteral("overflow"),
             QStringLiteral("volume_curve_id")},
            {tank}));

    const QVariantMap demand_ordinal_1 = {
        {QStringLiteral("junction_id"), 1},
        {QStringLiteral("ordinal"), 1},
        {QStringLiteral("base_demand"), 2.0},
        {QStringLiteral("pattern_id"), QVariant()}
    };
    const QVariantMap demand_ordinal_0 = {
        {QStringLiteral("junction_id"), 1},
        {QStringLiteral("ordinal"), 0},
        {QStringLiteral("base_demand"), 1.5},
        {QStringLiteral("pattern_id"), 5}
    };
    project.tables.insert(
        QStringLiteral("junction_demands"),
        makeTable(
            QStringLiteral("junction_demands"),
            {QStringLiteral("junction_id"), QStringLiteral("ordinal"),
             QStringLiteral("base_demand"), QStringLiteral("pattern_id")},
            {demand_ordinal_1, demand_ordinal_0}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter imports epanet-js nodes and native junction demands");
    expectTrue(result.network.nodes_junctions.size() == 2, "both junctions are imported");
    expectTrue(result.network.nodes_reservoirs.size() == 1, "reservoir is imported");
    expectTrue(result.network.nodes_tanks.size() == 1, "tank is imported");

    if (result.network.nodes_junctions.size() == 2)
    {
        const HydraulicNodeJunction &junction = result.network.nodes_junctions.first();
        expectTrue(junction.id == QStringLiteral("J1"), "junction label becomes AOWIS id");
        expectTrue(junction.uuid == result.id_map.uuidFor(QStringLiteral("junctions"), 1),
                   "junction uses deterministic mapped UUID");
        expectTrue(!junction.metadata.enabled, "junction is_active maps to metadata.enabled");
        expectNear(junction.coordinate_wgs84.longitude_deg, 18.1, 1e-12,
                   "junction longitude is imported");
        expectNear(junction.coordinate_wgs84.latitude_deg, 11.2, 1e-12,
                   "junction latitude is imported");
        expectNear(junction.elevation_m, 40.5, 1e-12,
                   "junction elevation is imported in canonical metres");
        expectTrue(junction.demands.size() == 2,
                   "all native junction demand rows are imported");
        if (junction.demands.size() == 2)
        {
            expectNear(junction.demands.at(0).base_demand_m3_per_h, 5.4, 1e-12,
                       "junction demand ordinal zero is converted from l/s to m3/h");
            expectTrue(junction.demands.at(0).pattern_mode == HydraulicTimePatternMode::TimePattern,
                       "junction demand pattern mode is imported");
            expectTrue(junction.demands.at(0).pattern_uuid
                           == result.id_map.uuidFor(QStringLiteral("patterns"), 5),
                       "junction demand pattern reference resolves through the id map");
            expectNear(junction.demands.at(1).base_demand_m3_per_h, 7.2, 1e-12,
                       "junction demand ordering follows epanet-js ordinal");
            expectTrue(junction.demands.at(1).pattern_mode == HydraulicTimePatternMode::Constant,
                       "null junction demand pattern remains constant");
        }
    }

    if (result.network.nodes_reservoirs.size() == 1)
    {
        const HydraulicNodeReservoir &imported = result.network.nodes_reservoirs.first();
        expectNear(imported.hydraulic_head_m, 100.0, 1e-12,
                   "reservoir head is imported");
        expectTrue(imported.head_pattern_mode == HydraulicTimePatternMode::TimePattern,
                   "reservoir head pattern mode is imported");
        expectTrue(imported.head_pattern_uuid
                       == result.id_map.uuidFor(QStringLiteral("patterns"), 5),
                   "reservoir head pattern reference resolves");
    }

    if (result.network.nodes_tanks.size() == 1)
    {
        const HydraulicNodeTank &imported = result.network.nodes_tanks.first();
        expectNear(imported.bottom_elevation_m, 70.0, 1e-12,
                   "tank bottom elevation is imported");
        expectNear(imported.water_level_initial_m, 5.0, 1e-12,
                   "tank initial level is imported");
        expectNear(imported.water_level_minimum_m, 1.0, 1e-12,
                   "tank minimum level is imported");
        expectNear(imported.water_level_maximum_m, 8.0, 1e-12,
                   "tank maximum level is imported");
        expectNear(imported.minimum_volume_m3, 25.0, 1e-12,
                   "tank minimum volume is imported");
        expectNear(imported.diameter_m, 10.0, 1e-12,
                   "tank diameter is imported");
        expectNear(imported.cross_section_area_m2, 78.53981633974483, 1e-10,
                   "cylindrical tank area is derived from diameter");
        expectTrue(imported.can_overflow, "tank overflow flag is imported");
        expectTrue(imported.geometry_input_type == HydraulicNodeTankGeometryInputType::VolumeCurve,
                   "tank volume-curve geometry is selected when a curve is referenced");
        expectTrue(imported.volume_curve_uuid
                       == result.id_map.uuidFor(QStringLiteral("curves"), 6),
                   "tank volume curve reference resolves through the id map");
    }
}

void testConvertsUsNodeAndDemandUnits()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("elevation"), QStringLiteral("ft"));
    units.insert(QStringLiteral("head"), QStringLiteral("ft"));
    units.insert(QStringLiteral("level"), QStringLiteral("ft"));
    units.insert(QStringLiteral("initialLevel"), QStringLiteral("ft"));
    units.insert(QStringLiteral("minLevel"), QStringLiteral("ft"));
    units.insert(QStringLiteral("maxLevel"), QStringLiteral("ft"));
    units.insert(QStringLiteral("tankDiameter"), QStringLiteral("ft"));
    units.insert(QStringLiteral("minVolume"), QStringLiteral("ft^3"));
    units.insert(QStringLiteral("volume"), QStringLiteral("ft^3"));
    units.insert(QStringLiteral("baseDemand"), QStringLiteral("gpm"));
    units.insert(QStringLiteral("flow"), QStringLiteral("gpm"));
    units.insert(QStringLiteral("pressure"), QStringLiteral("psi"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("us-node-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    QJsonObject simulation;
    simulation.insert(QStringLiteral("specificGravity"), 1.2);
    simulation.insert(QStringLiteral("emitterExponent"), 0.6);
    setSimulationSettings(project, simulation);

    EpanetJsTableSnapshot junctions = project.tables.value(QStringLiteral("junctions"));
    junctions.columns.append(QStringLiteral("elevation"));
    junctions.columns.append(QStringLiteral("emitter_coefficient"));
    junctions.rows[0].insert(QStringLiteral("elevation"), 100.0);
    junctions.rows[0].insert(QStringLiteral("emitter_coefficient"), 2.0);
    junctions.rows[1].insert(QStringLiteral("elevation"), 125.0);
    project.tables.insert(QStringLiteral("junctions"), junctions);

    const QVariantMap reservoir = {
        {QStringLiteral("id"), 3},
        {QStringLiteral("label"), QStringLiteral("R-US")},
        {QStringLiteral("coord_x"), -80.0},
        {QStringLiteral("coord_y"), 40.0},
        {QStringLiteral("head"), 200.0}
    };
    project.tables.insert(
        QStringLiteral("reservoirs"),
        makeTable(
            QStringLiteral("reservoirs"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("coord_x"),
             QStringLiteral("coord_y"), QStringLiteral("head")},
            {reservoir}));

    const QVariantMap tank = {
        {QStringLiteral("id"), 4},
        {QStringLiteral("label"), QStringLiteral("T-US")},
        {QStringLiteral("coord_x"), -80.1},
        {QStringLiteral("coord_y"), 40.1},
        {QStringLiteral("elevation"), 150.0},
        {QStringLiteral("initial_level"), 10.0},
        {QStringLiteral("min_level"), 0.0},
        {QStringLiteral("max_level"), 15.0},
        {QStringLiteral("min_volume"), 100.0},
        {QStringLiteral("diameter"), 20.0}
    };
    project.tables.insert(
        QStringLiteral("tanks"),
        makeTable(
            QStringLiteral("tanks"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("coord_x"),
             QStringLiteral("coord_y"), QStringLiteral("elevation"),
             QStringLiteral("initial_level"), QStringLiteral("min_level"),
             QStringLiteral("max_level"), QStringLiteral("min_volume"),
             QStringLiteral("diameter")},
            {tank}));

    const QVariantMap demand = {
        {QStringLiteral("junction_id"), 1},
        {QStringLiteral("ordinal"), 0},
        {QStringLiteral("base_demand"), 10.0},
        {QStringLiteral("pattern_id"), QVariant()}
    };
    project.tables.insert(
        QStringLiteral("junction_demands"),
        makeTable(
            QStringLiteral("junction_demands"),
            {QStringLiteral("junction_id"), QStringLiteral("ordinal"),
             QStringLiteral("base_demand"), QStringLiteral("pattern_id")},
            {demand}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter accepts US customary epanet-js node units");
    if (!result.network.nodes_junctions.isEmpty())
    {
        const HydraulicNodeJunction &junction = result.network.nodes_junctions.first();
        expectNear(junction.elevation_m, 30.48, 1e-12,
                   "junction elevation feet are converted to metres");
        expectNear(junction.emitter.pressure_exponent, 0.6, 1e-12,
                   "epanet-js emitter exponent is imported");
        expectNear(junction.emitter.coefficient, 0.6258458247654275, 1e-12,
                   "US emitter coefficient is converted from gpm/psi^n to canonical m3/h/m^n");
        expectTrue(junction.demands.size() == 1, "US junction demand is imported");
        if (junction.demands.size() == 1)
            expectNear(junction.demands.first().base_demand_m3_per_h, 2.2712470704, 1e-12,
                       "junction demand gpm is converted to m3/h");
    }
    if (result.network.nodes_reservoirs.size() == 1)
        expectNear(result.network.nodes_reservoirs.first().hydraulic_head_m, 60.96, 1e-12,
                   "reservoir head feet are converted to metres");
    if (result.network.nodes_tanks.size() == 1)
    {
        const HydraulicNodeTank &imported = result.network.nodes_tanks.first();
        expectNear(imported.bottom_elevation_m, 45.72, 1e-12,
                   "tank elevation feet are converted to metres");
        expectNear(imported.water_level_initial_m, 3.048, 1e-12,
                   "tank initial level feet are converted to metres");
        expectNear(imported.water_level_maximum_m, 4.572, 1e-12,
                   "tank maximum level feet are converted to metres");
        expectNear(imported.minimum_volume_m3, 2.8316846592, 1e-12,
                   "tank minimum cubic feet are converted to cubic metres");
        expectNear(imported.diameter_m, 6.096, 1e-12,
                   "tank diameter feet are converted to metres");
    }
}

void testImportsWaterQualitySimulationSettings()
{
    EpanetJsProjectSnapshot chemical_project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject chemical_units;
    chemical_units.insert(QStringLiteral("chemicalConcentration"), QStringLiteral("ug/L"));
    QJsonObject chemical_project_settings;
    chemical_project_settings.insert(QStringLiteral("name"), QStringLiteral("quality-chemical-test"));
    chemical_project_settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    chemical_project_settings.insert(QStringLiteral("units"), chemical_units);
    setProjectSettings(chemical_project, chemical_project_settings);

    QJsonObject chemical_settings;
    chemical_settings.insert(QStringLiteral("qualitySimulationType"), QStringLiteral("chemical"));
    chemical_settings.insert(QStringLiteral("qualityChemicalName"), QStringLiteral("Chlorine"));
    chemical_settings.insert(QStringLiteral("tolerance"), 25.0);
    chemical_settings.insert(QStringLiteral("diffusivity"), 1.3);
    setSimulationSettings(chemical_project, chemical_settings);

    const EpanetJsProjectConversionResult chemical_result =
        EpanetJsProjectConverter::convert(chemical_project);
    expectTrue(chemical_result.success,
               "converter accepts epanet-js chemical quality run settings");
    expectTrue(chemical_result.quality_runs.size() == 1,
               "chemical quality configuration creates one AOWIS quality run");
    if (chemical_result.quality_runs.size() == 1)
    {
        const WaterQualitySolverOptions &quality = chemical_result.quality_runs.first();
        expectTrue(quality.analysis == WaterQualityAnalysisType::Chemical,
                   "chemical quality analysis mode is imported");
        expectTrue(quality.chemical_name == QStringLiteral("Chlorine"),
                   "chemical quality name is imported");
        expectNear(quality.chemical_tolerance_mg_per_l, 0.025, 1e-12,
                   "chemical tolerance ug/L is converted to canonical mg/L");
        expectNear(quality.relative_diffusivity, 1.3, 1e-12,
                   "relative diffusivity is imported");
    }

    EpanetJsProjectSnapshot age_project = makeProjectWithUniqueId(
        QStringLiteral("4cd1e05a-cbba-4d2d-919a-a0d287074d1f"));
    QJsonObject age_settings;
    age_settings.insert(QStringLiteral("qualitySimulationType"), QStringLiteral("age"));
    age_settings.insert(QStringLiteral("tolerance"), 0.25);
    setSimulationSettings(age_project, age_settings);

    const EpanetJsProjectConversionResult age_result =
        EpanetJsProjectConverter::convert(age_project);
    expectTrue(age_result.success,
               "converter accepts epanet-js water-age run settings");
    expectTrue(age_result.quality_runs.size() == 1,
               "water-age configuration creates one AOWIS quality run");
    if (age_result.quality_runs.size() == 1)
    {
        const WaterQualitySolverOptions &quality = age_result.quality_runs.first();
        expectTrue(quality.analysis == WaterQualityAnalysisType::WaterAge,
                   "water-age analysis mode is imported");
        expectNear(quality.water_age_tolerance_h, 0.25, 1e-12,
                   "water-age tolerance is imported in hours");
    }

    EpanetJsProjectSnapshot trace_project = makeProjectWithUniqueId(
        QStringLiteral("a6c92c9e-6cfb-424b-9804-664b6c70d6a1"));
    QJsonObject trace_settings;
    trace_settings.insert(QStringLiteral("qualitySimulationType"), QStringLiteral("trace"));
    trace_settings.insert(QStringLiteral("qualityTraceNodeId"), 1);
    trace_settings.insert(QStringLiteral("tolerance"), 0.2);
    setSimulationSettings(trace_project, trace_settings);

    const EpanetJsProjectConversionResult trace_result =
        EpanetJsProjectConverter::convert(trace_project);
    expectTrue(trace_result.success,
               "converter accepts epanet-js source-trace run settings");
    expectTrue(trace_result.quality_runs.size() == 1,
               "source-trace configuration creates one AOWIS quality run");
    if (trace_result.quality_runs.size() == 1)
    {
        const WaterQualitySolverOptions &quality = trace_result.quality_runs.first();
        expectTrue(quality.analysis == WaterQualityAnalysisType::SourceTrace,
                   "source-trace analysis mode is imported");
        expectTrue(
            quality.trace_node_uuid == trace_result.id_map.nodeUuid(1),
            "source-trace node id resolves through the epanet-js id map");
        expectNear(quality.source_trace_tolerance_percent, 0.2, 1e-12,
                   "source-trace tolerance is imported in percent");
    }
}

void testImportsWaterQualityEntityData()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("gpm"));
    units.insert(QStringLiteral("head"), QStringLiteral("ft"));
    units.insert(QStringLiteral("elevation"), QStringLiteral("ft"));
    units.insert(QStringLiteral("level"), QStringLiteral("ft"));
    units.insert(QStringLiteral("chemicalConcentration"), QStringLiteral("ug/L"));
    units.insert(QStringLiteral("waterAge"), QStringLiteral("h"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("quality-entity-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    QJsonObject simulation;
    simulation.insert(QStringLiteral("qualitySimulationType"), QStringLiteral("chemical"));
    simulation.insert(QStringLiteral("qualityChemicalName"), QStringLiteral("CL2"));
    simulation.insert(QStringLiteral("qualityMassUnit"), QStringLiteral("ug/L"));
    simulation.insert(QStringLiteral("reactionBulkOrder"), 2.0);
    simulation.insert(QStringLiteral("reactionWallOrder"), 0.0);
    simulation.insert(QStringLiteral("reactionTankOrder"), 0.5);
    setSimulationSettings(project, simulation);

    const QVariantMap source_pattern = {
        {QStringLiteral("id"), 9},
        {QStringLiteral("label"), QStringLiteral("BOOST")},
        {QStringLiteral("type"), QStringLiteral("demand")},
        {QStringLiteral("multipliers"), QStringLiteral("[1,0.5]")}
    };
    project.tables.insert(
        QStringLiteral("patterns"),
        makeTable(
            QStringLiteral("patterns"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("type"),
             QStringLiteral("multipliers")},
            {source_pattern}));

    EpanetJsTableSnapshot junctions = project.tables.value(QStringLiteral("junctions"));
    junctions.columns.append(QStringLiteral("initial_quality"));
    junctions.columns.append(QStringLiteral("chemical_source_type"));
    junctions.columns.append(QStringLiteral("chemical_source_strength"));
    junctions.columns.append(QStringLiteral("chemical_source_pattern_id"));
    junctions.rows[0].insert(QStringLiteral("initial_quality"), 1500.0);
    junctions.rows[0].insert(QStringLiteral("chemical_source_type"), QStringLiteral("MASS"));
    junctions.rows[0].insert(QStringLiteral("chemical_source_strength"), 2500.0);
    junctions.rows[0].insert(QStringLiteral("chemical_source_pattern_id"), 9);
    junctions.rows[1].insert(QStringLiteral("chemical_source_type"), QStringLiteral("SETPOINT"));
    junctions.rows[1].insert(QStringLiteral("chemical_source_strength"), 600.0);
    junctions.rows[1].insert(QStringLiteral("chemical_source_pattern_id"), QVariant());
    project.tables.insert(QStringLiteral("junctions"), junctions);

    const QVariantMap reservoir = {
        {QStringLiteral("id"), 3},
        {QStringLiteral("label"), QStringLiteral("R1")},
        {QStringLiteral("coord_x"), 18.2},
        {QStringLiteral("coord_y"), 11.2},
        {QStringLiteral("head"), 200.0},
        {QStringLiteral("initial_quality"), 1000.0},
        {QStringLiteral("chemical_source_type"), QStringLiteral("CONCEN")},
        {QStringLiteral("chemical_source_strength"), 1200.0},
        {QStringLiteral("chemical_source_pattern_id"), QVariant()}
    };
    project.tables.insert(
        QStringLiteral("reservoirs"),
        makeTable(
            QStringLiteral("reservoirs"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y"),
             QStringLiteral("head"), QStringLiteral("initial_quality"),
             QStringLiteral("chemical_source_type"),
             QStringLiteral("chemical_source_strength"),
             QStringLiteral("chemical_source_pattern_id")},
            {reservoir}));

    const QVariantMap tank = {
        {QStringLiteral("id"), 4},
        {QStringLiteral("label"), QStringLiteral("T1")},
        {QStringLiteral("coord_x"), 18.3},
        {QStringLiteral("coord_y"), 11.3},
        {QStringLiteral("elevation"), 120.0},
        {QStringLiteral("initial_level"), 10.0},
        {QStringLiteral("min_level"), 0.0},
        {QStringLiteral("max_level"), 20.0},
        {QStringLiteral("initial_quality"), 500.0},
        {QStringLiteral("chemical_source_type"), QStringLiteral("FLOWPACED")},
        {QStringLiteral("chemical_source_strength"), 750.0},
        {QStringLiteral("chemical_source_pattern_id"), 9},
        {QStringLiteral("mixing_model"), QStringLiteral("2COMP")},
        {QStringLiteral("mixing_fraction"), 0.25},
        {QStringLiteral("bulk_reaction_coeff"), -2.0}
    };
    project.tables.insert(
        QStringLiteral("tanks"),
        makeTable(
            QStringLiteral("tanks"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y"),
             QStringLiteral("elevation"), QStringLiteral("initial_level"),
             QStringLiteral("min_level"), QStringLiteral("max_level"),
             QStringLiteral("initial_quality"), QStringLiteral("chemical_source_type"),
             QStringLiteral("chemical_source_strength"),
             QStringLiteral("chemical_source_pattern_id"),
             QStringLiteral("mixing_model"), QStringLiteral("mixing_fraction"),
             QStringLiteral("bulk_reaction_coeff")},
            {tank}));

    EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
    pipes.columns.append(QStringLiteral("bulk_reaction_coeff"));
    pipes.columns.append(QStringLiteral("wall_reaction_coeff"));
    pipes.rows[0].insert(QStringLiteral("bulk_reaction_coeff"), -0.002);
    pipes.rows[0].insert(QStringLiteral("wall_reaction_coeff"), -4.0);
    project.tables.insert(QStringLiteral("pipes"), pipes);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter imports epanet-js water-quality entity data");
    expectTrue(result.network.nodes_junctions.size() == 2,
               "quality entity test keeps both junctions");
    if (!result.network.nodes_junctions.isEmpty())
    {
        const HydraulicNodeJunction &junction = result.network.nodes_junctions.first();
        expectNear(junction.initial_chemical_concentration_mg_per_l, 1.5, 1e-12,
                   "junction initial chemical quality converts ug/L to mg/L");
        expectTrue(junction.quality_source.type == HydraulicNodeQualitySourceType::MassBooster,
                   "MASS source maps to AOWIS mass booster");
        expectNear(junction.quality_source.chemical_mass_flow_mg_per_min, 2.5, 1e-12,
                   "MASS source strength converts ug/min to mg/min");
        expectTrue(junction.quality_source.pattern_uuid
                       == result.id_map.uuidFor(QStringLiteral("patterns"), 9),
                   "quality source pattern resolves through the shared id map");
    }
    if (result.network.nodes_junctions.size() >= 2)
    {
        const HydraulicNodeJunction &junction = result.network.nodes_junctions.at(1);
        expectTrue(junction.quality_source.type == HydraulicNodeQualitySourceType::SetpointBooster,
                   "SETPOINT source maps to AOWIS setpoint booster");
        expectNear(junction.quality_source.chemical_concentration_mg_per_l, 0.6, 1e-12,
                   "SETPOINT source strength converts to mg/L");
    }

    expectTrue(result.network.nodes_reservoirs.size() == 1,
               "quality entity test imports reservoir");
    if (result.network.nodes_reservoirs.size() == 1)
    {
        const HydraulicNodeReservoir &reservoir_imported =
            result.network.nodes_reservoirs.first();
        expectNear(reservoir_imported.initial_chemical_concentration_mg_per_l, 1.0, 1e-12,
                   "reservoir initial chemical quality converts to mg/L");
        expectTrue(
            reservoir_imported.quality_source.type
                == HydraulicNodeQualitySourceType::Concentration,
            "CONCEN source maps to AOWIS concentration source");
        expectNear(
            reservoir_imported.quality_source.chemical_concentration_mg_per_l,
            1.2, 1e-12,
            "concentration source strength converts to mg/L");
    }

    expectTrue(result.network.nodes_tanks.size() == 1,
               "quality entity test imports tank");
    if (result.network.nodes_tanks.size() == 1)
    {
        const HydraulicNodeTank &tank_imported = result.network.nodes_tanks.first();
        expectNear(tank_imported.initial_chemical_concentration_mg_per_l, 0.5, 1e-12,
                   "tank initial chemical quality converts to mg/L");
        expectTrue(
            tank_imported.quality_source.type
                == HydraulicNodeQualitySourceType::FlowPacedBooster,
            "FLOWPACED source maps to AOWIS flow-paced booster");
        expectNear(
            tank_imported.quality_source.chemical_concentration_mg_per_l,
            0.75, 1e-12,
            "flow-paced source strength converts to mg/L");
        expectTrue(
            tank_imported.mixing_model
                == HydraulicNodeTankMixingModel::TwoCompartment,
            "2COMP tank mixing model is imported");
        expectNear(tank_imported.mixing_fraction, 0.25, 1e-12,
                   "two-compartment mixing fraction is imported");
        expectTrue(tank_imported.override_bulk_reaction,
                   "explicit tank bulk reaction becomes an override");
        expectNear(tank_imported.bulk_reaction.order, 0.5, 1e-12,
                   "tank reaction override inherits the configured tank order");
        expectNear(tank_imported.bulk_reaction.coefficient,
                   -0.06324555320336758, 1e-12,
                   "tank bulk reaction coefficient converts concentration dimensions");
    }

    expectTrue(result.network.links_pipes.size() == 1,
               "quality entity test keeps the pipe");
    if (result.network.links_pipes.size() == 1)
    {
        const HydraulicLinkPipe &pipe = result.network.links_pipes.first();
        expectTrue(pipe.override_bulk_reaction,
                   "explicit pipe bulk reaction becomes an override");
        expectTrue(pipe.override_wall_reaction,
                   "explicit pipe wall reaction becomes an override");
        expectNear(pipe.bulk_reaction.order, 2.0, 1e-12,
                   "pipe bulk reaction override inherits the configured bulk order");
        expectNear(pipe.wall_reaction.order, 0.0, 1e-12,
                   "pipe wall reaction override inherits the configured wall order");
        expectNear(pipe.bulk_reaction.coefficient, -2.0, 1e-12,
                   "pipe bulk reaction coefficient converts concentration dimensions");
        expectNear(pipe.wall_reaction.coefficient,
                   -0.04305564166683889, 1e-12,
                   "zero-order US wall reaction coefficient converts concentration and area units");
    }
}

void testImportsAllTankMixingModels()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("elevation"), QStringLiteral("m"));
    units.insert(QStringLiteral("level"), QStringLiteral("m"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("tank-mixing-model-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    const QVariantMap mixed = {
        {QStringLiteral("id"), 10},
        {QStringLiteral("label"), QStringLiteral("TMIX")},
        {QStringLiteral("coord_x"), 18.0},
        {QStringLiteral("coord_y"), 11.0},
        {QStringLiteral("elevation"), 100.0},
        {QStringLiteral("initial_level"), 5.0},
        {QStringLiteral("min_level"), 0.0},
        {QStringLiteral("max_level"), 10.0},
        {QStringLiteral("mixing_model"), QStringLiteral("MIXED")},
        {QStringLiteral("mixing_fraction"), QVariant()}
    };
    const QVariantMap two_compartment = {
        {QStringLiteral("id"), 11},
        {QStringLiteral("label"), QStringLiteral("T2COMP")},
        {QStringLiteral("coord_x"), 18.1},
        {QStringLiteral("coord_y"), 11.0},
        {QStringLiteral("elevation"), 100.0},
        {QStringLiteral("initial_level"), 5.0},
        {QStringLiteral("min_level"), 0.0},
        {QStringLiteral("max_level"), 10.0},
        {QStringLiteral("mixing_model"), QStringLiteral("2COMP")},
        {QStringLiteral("mixing_fraction"), 0.4}
    };
    const QVariantMap fifo = {
        {QStringLiteral("id"), 12},
        {QStringLiteral("label"), QStringLiteral("TFIFO")},
        {QStringLiteral("coord_x"), 18.2},
        {QStringLiteral("coord_y"), 11.0},
        {QStringLiteral("elevation"), 100.0},
        {QStringLiteral("initial_level"), 5.0},
        {QStringLiteral("min_level"), 0.0},
        {QStringLiteral("max_level"), 10.0},
        {QStringLiteral("mixing_model"), QStringLiteral("FIFO")},
        {QStringLiteral("mixing_fraction"), QVariant()}
    };
    const QVariantMap lifo = {
        {QStringLiteral("id"), 13},
        {QStringLiteral("label"), QStringLiteral("TLIFO")},
        {QStringLiteral("coord_x"), 18.3},
        {QStringLiteral("coord_y"), 11.0},
        {QStringLiteral("elevation"), 100.0},
        {QStringLiteral("initial_level"), 5.0},
        {QStringLiteral("min_level"), 0.0},
        {QStringLiteral("max_level"), 10.0},
        {QStringLiteral("mixing_model"), QStringLiteral("LIFO")},
        {QStringLiteral("mixing_fraction"), QVariant()}
    };
    project.tables.insert(
        QStringLiteral("tanks"),
        makeTable(
            QStringLiteral("tanks"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y"),
             QStringLiteral("elevation"), QStringLiteral("initial_level"),
             QStringLiteral("min_level"), QStringLiteral("max_level"),
             QStringLiteral("mixing_model"), QStringLiteral("mixing_fraction")},
            {mixed, two_compartment, fifo, lifo}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter imports all EPANET tank mixing models");
    expectTrue(result.network.nodes_tanks.size() == 4,
               "all four synthetic tank mixing rows are imported");
    if (result.network.nodes_tanks.size() != 4)
        return;

    expectTrue(
        result.network.nodes_tanks.at(0).mixing_model
            == HydraulicNodeTankMixingModel::CompleteMix,
        "MIXED maps to complete mixing");
    expectTrue(
        result.network.nodes_tanks.at(1).mixing_model
            == HydraulicNodeTankMixingModel::TwoCompartment,
        "2COMP maps to two-compartment mixing");
    expectNear(result.network.nodes_tanks.at(1).mixing_fraction, 0.4, 1e-12,
               "2COMP mixing fraction is preserved");
    expectTrue(
        result.network.nodes_tanks.at(2).mixing_model
            == HydraulicNodeTankMixingModel::FirstInFirstOut,
        "FIFO maps to first-in-first-out mixing");
    expectTrue(
        result.network.nodes_tanks.at(3).mixing_model
            == HydraulicNodeTankMixingModel::LastInFirstOut,
        "LIFO maps to last-in-first-out mixing");
}

void testImportsWaterAgeInitialQuality()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("waterAge"), QStringLiteral("min"));
    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("quality-age-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    QJsonObject simulation;
    simulation.insert(QStringLiteral("qualitySimulationType"), QStringLiteral("age"));
    setSimulationSettings(project, simulation);

    EpanetJsTableSnapshot junctions = project.tables.value(QStringLiteral("junctions"));
    junctions.columns.append(QStringLiteral("initial_quality"));
    junctions.rows[0].insert(QStringLiteral("initial_quality"), 90.0);
    project.tables.insert(QStringLiteral("junctions"), junctions);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter imports water-age initial quality");
    if (!result.network.nodes_junctions.isEmpty())
    {
        const HydraulicNodeJunction &junction = result.network.nodes_junctions.first();
        expectNear(junction.initial_water_age_h, 1.5, 1e-12,
                   "water-age initial quality converts minutes to canonical hours");
        expectNear(junction.initial_chemical_concentration_mg_per_l, 0.0, 1e-12,
                   "water-age initial quality does not populate chemical concentration");
    }
}

void testRejectsInvalidWaterQualityTraceNode()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("609d4967-c634-4739-9449-fd13e3a6613a"));

    QJsonObject simulation;
    simulation.insert(QStringLiteral("qualitySimulationType"), QStringLiteral("trace"));
    simulation.insert(QStringLiteral("qualityTraceNodeId"), 9999);
    setSimulationSettings(project, simulation);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);
    expectTrue(!result.success,
               "converter rejects source-trace settings that reference a missing node");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("invalid-quality-trace-node")),
               "missing source-trace node receives a dedicated diagnostic");
    expectTrue(result.quality_runs.isEmpty(),
               "invalid source-trace configuration does not create a partial quality run");
}

void testRejectsBrokenNodeReferences()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("elevation"), QStringLiteral("m"));
    units.insert(QStringLiteral("baseDemand"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("broken-node-reference-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    const QVariantMap demand = {
        {QStringLiteral("junction_id"), 1},
        {QStringLiteral("ordinal"), 0},
        {QStringLiteral("base_demand"), 1.0},
        {QStringLiteral("pattern_id"), 999}
    };
    project.tables.insert(
        QStringLiteral("junction_demands"),
        makeTable(
            QStringLiteral("junction_demands"),
            {QStringLiteral("junction_id"), QStringLiteral("ordinal"),
             QStringLiteral("base_demand"), QStringLiteral("pattern_id")},
            {demand}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(!result.success, "converter rejects a native junction demand with a missing pattern");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("missing-demand-pattern-reference")),
               "missing junction demand pattern produces a dedicated diagnostic");
}


void testImportsCustomerPointsWithAssignedJunctionDemands()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("customerDemand"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("length"), QStringLiteral("m"));
    units.insert(QStringLiteral("diameter"), QStringLiteral("mm"));
    QJsonObject projection;
    projection.insert(QStringLiteral("type"), QStringLiteral("wgs84"));
    projection.insert(QStringLiteral("id"), QStringLiteral("wgs84"));
    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("customer-point-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("headlossFormula"), QStringLiteral("H-W"));
    settings.insert(QStringLiteral("units"), units);
    settings.insert(QStringLiteral("projection"), projection);
    setProjectSettings(project, settings);

    const QVariantMap pattern = {
        {QStringLiteral("id"), 5},
        {QStringLiteral("label"), QStringLiteral("CUSTOMER")},
        {QStringLiteral("multipliers"), QStringLiteral("[1.0,0.5]")}
    };
    project.tables.insert(
        QStringLiteral("patterns"),
        makeTable(
            QStringLiteral("patterns"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("multipliers")},
            {pattern}));

    EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
    QVariantMap pipe = pipes.rows.first();
    pipe.insert(
        QStringLiteral("coords"),
        QStringLiteral("[[18.0,11.0],[18.05,11.05],[18.1,11.1]]"));
    if (!pipes.columns.contains(QStringLiteral("coords")))
        pipes.columns.append(QStringLiteral("coords"));
    pipes.rows = {pipe};
    project.tables.insert(QStringLiteral("pipes"), pipes);

    const QVariantMap customer_point = {
        {QStringLiteral("id"), 20},
        {QStringLiteral("label"), QStringLiteral("C1")},
        {QStringLiteral("coord_x"), 18.04},
        {QStringLiteral("coord_y"), 11.06},
        {QStringLiteral("pipe_id"), 10},
        {QStringLiteral("junction_id"), 1},
        {QStringLiteral("snap_x"), 18.05},
        {QStringLiteral("snap_y"), 11.05}
    };
    project.tables.insert(
        QStringLiteral("customer_points"),
        makeTable(
            QStringLiteral("customer_points"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y"),
             QStringLiteral("pipe_id"), QStringLiteral("junction_id"),
             QStringLiteral("snap_x"), QStringLiteral("snap_y")},
            {customer_point}));

    const QVariantMap demand_ordinal_1 = {
        {QStringLiteral("customer_point_id"), 20},
        {QStringLiteral("ordinal"), 1},
        {QStringLiteral("base_demand"), 2.0},
        {QStringLiteral("pattern_id"), QVariant()}
    };
    const QVariantMap demand_ordinal_0 = {
        {QStringLiteral("customer_point_id"), 20},
        {QStringLiteral("ordinal"), 0},
        {QStringLiteral("base_demand"), 1.5},
        {QStringLiteral("pattern_id"), 5}
    };
    project.tables.insert(
        QStringLiteral("customer_point_demands"),
        makeTable(
            QStringLiteral("customer_point_demands"),
            {QStringLiteral("customer_point_id"), QStringLiteral("ordinal"),
             QStringLiteral("base_demand"), QStringLiteral("pattern_id")},
            {demand_ordinal_1, demand_ordinal_0}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success,
               "converter imports epanet-js customer points and customer-point demands");
    expectTrue(result.network.demand_points.size() == 1,
               "one epanet-js customer point becomes one AOWIS demand point");
    if (result.network.demand_points.size() != 1)
        return;

    const HydraulicDemandPoint &imported = result.network.demand_points.first();
    expectTrue(imported.id == QStringLiteral("C1"),
               "customer-point label becomes the AOWIS demand-point id");
    expectTrue(imported.uuid
                   == result.id_map.uuidFor(QStringLiteral("customer_points"), 20),
               "customer-point UUID uses the stable epanet-js id map");
    expectNear(imported.coordinate_wgs84.longitude_deg, 18.04, 1e-12,
               "customer-point longitude is preserved");
    expectNear(imported.coordinate_wgs84.latitude_deg, 11.06, 1e-12,
               "customer-point latitude is preserved");
    expectTrue(imported.attachment.type == HydraulicDemandPointAttachmentType::Pipe,
               "epanet-js pipe-backed customer point stays visually pipe-attached");
    expectTrue(imported.attachment.pipe_uuid
                   == result.id_map.uuidFor(QStringLiteral("pipes"), 10),
               "customer-point pipe reference resolves through the id map");
    expectTrue(imported.attachment.pipe_allocation_mode
                   == HydraulicDemandPointPipeAllocationMode::AssignedJunction,
               "epanet-js customer point uses assigned-junction allocation mode");
    expectTrue(imported.attachment.pipe_assigned_junction_uuid
                   == result.id_map.uuidFor(QStringLiteral("junctions"), 1),
               "epanet-js assigned junction is preserved separately from visual pipe attachment");
    expectNear(imported.attachment.pipe_position, 0.5, 0.002,
               "epanet-js snap coordinate maps to normalized position along the complete pipe geometry");

    expectTrue(imported.demands.size() == 2,
               "all customer-point demand rows are preserved");
    if (imported.demands.size() == 2)
    {
        expectNear(imported.demands.at(0).base_demand_m3_per_h, 5.4, 1e-12,
                   "customer demand ordinal zero is converted from l/s to m3/h");
        expectTrue(imported.demands.at(0).pattern_mode == HydraulicTimePatternMode::TimePattern,
                   "customer demand pattern mode is imported");
        expectTrue(imported.demands.at(0).pattern_uuid
                       == result.id_map.uuidFor(QStringLiteral("patterns"), 5),
                   "customer demand pattern reference resolves through the id map");
        expectNear(imported.demands.at(1).base_demand_m3_per_h, 7.2, 1e-12,
                   "customer demand ordinal ordering is preserved");
        expectTrue(imported.demands.at(1).pattern_mode == HydraulicTimePatternMode::Constant,
                   "null customer demand pattern remains constant");
    }
}

void testImportsUsCustomerDemandUnits()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("gal/min"));
    units.insert(QStringLiteral("customerDemand"), QStringLiteral("gal/min"));
    units.insert(QStringLiteral("length"), QStringLiteral("ft"));
    units.insert(QStringLiteral("diameter"), QStringLiteral("in"));
    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("customer-point-us-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
    QVariantMap pipe = pipes.rows.first();
    pipe.insert(QStringLiteral("coords"), QStringLiteral("[[18.0,11.0],[18.1,11.1]]"));
    if (!pipes.columns.contains(QStringLiteral("coords")))
        pipes.columns.append(QStringLiteral("coords"));
    pipes.rows = {pipe};
    project.tables.insert(QStringLiteral("pipes"), pipes);

    const QVariantMap customer_point = {
        {QStringLiteral("id"), 20},
        {QStringLiteral("label"), QStringLiteral("C1")},
        {QStringLiteral("coord_x"), 18.05},
        {QStringLiteral("coord_y"), 11.06},
        {QStringLiteral("pipe_id"), 10},
        {QStringLiteral("junction_id"), 2},
        {QStringLiteral("snap_x"), 18.05},
        {QStringLiteral("snap_y"), 11.05}
    };
    project.tables.insert(
        QStringLiteral("customer_points"),
        makeTable(
            QStringLiteral("customer_points"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y"),
             QStringLiteral("pipe_id"), QStringLiteral("junction_id"),
             QStringLiteral("snap_x"), QStringLiteral("snap_y")},
            {customer_point}));
    const QVariantMap demand = {
        {QStringLiteral("customer_point_id"), 20},
        {QStringLiteral("ordinal"), 0},
        {QStringLiteral("base_demand"), 10.0},
        {QStringLiteral("pattern_id"), QVariant()}
    };
    project.tables.insert(
        QStringLiteral("customer_point_demands"),
        makeTable(
            QStringLiteral("customer_point_demands"),
            {QStringLiteral("customer_point_id"), QStringLiteral("ordinal"),
             QStringLiteral("base_demand"), QStringLiteral("pattern_id")},
            {demand}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success,
               "converter accepts epanet-js gal/min customer-demand units");
    expectTrue(result.network.demand_points.size() == 1,
               "US-customary customer point is imported");
    if (result.network.demand_points.size() == 1
        && result.network.demand_points.first().demands.size() == 1)
    {
        expectNear(
            result.network.demand_points.first().demands.first().base_demand_m3_per_h,
            2.2712470704,
            1e-12,
            "gal/min customer demand converts to canonical m3/h");
    }
}

void testRejectsCustomerAssignedJunctionOutsidePipeEndpoints()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    EpanetJsTableSnapshot junctions = project.tables.value(QStringLiteral("junctions"));
    const QVariantMap unrelated_junction = {
        {QStringLiteral("id"), 3},
        {QStringLiteral("label"), QStringLiteral("J3")},
        {QStringLiteral("coord_x"), 18.2},
        {QStringLiteral("coord_y"), 11.2}
    };
    junctions.rows.append(unrelated_junction);
    project.tables.insert(QStringLiteral("junctions"), junctions);

    EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
    QVariantMap pipe = pipes.rows.first();
    pipe.insert(QStringLiteral("coords"), QStringLiteral("[[18.0,11.0],[18.1,11.1]]"));
    if (!pipes.columns.contains(QStringLiteral("coords")))
        pipes.columns.append(QStringLiteral("coords"));
    pipes.rows = {pipe};
    project.tables.insert(QStringLiteral("pipes"), pipes);

    const QVariantMap customer_point = {
        {QStringLiteral("id"), 20},
        {QStringLiteral("label"), QStringLiteral("C1")},
        {QStringLiteral("coord_x"), 18.05},
        {QStringLiteral("coord_y"), 11.06},
        {QStringLiteral("pipe_id"), 10},
        {QStringLiteral("junction_id"), 3},
        {QStringLiteral("snap_x"), 18.05},
        {QStringLiteral("snap_y"), 11.05}
    };
    project.tables.insert(
        QStringLiteral("customer_points"),
        makeTable(
            QStringLiteral("customer_points"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y"),
             QStringLiteral("pipe_id"), QStringLiteral("junction_id"),
             QStringLiteral("snap_x"), QStringLiteral("snap_y")},
            {customer_point}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(!result.success,
               "converter rejects an epanet-js customer assigned to a non-endpoint junction");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("invalid-customer-assigned-junction")),
               "non-endpoint assigned junction produces a dedicated diagnostic");
}

void testImportsPipeMaterialLibraryAndAssignsNullRoughnessPipes()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("length"), QStringLiteral("m"));
    units.insert(QStringLiteral("diameter"), QStringLiteral("mm"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("material-library-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("headlossFormula"), QStringLiteral("H-W"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    QJsonArray library;
    library.append(pipeMaterialDefinition(
        QStringLiteral("AOWIS Synthetic Cement"), {{0, 137.0}, {60, 111.0}}));
    library.append(pipeMaterialDefinition(
        QStringLiteral("AOWIS Synthetic Iron"), {{0, 132.0}, {40, 117.0}, {50, 101.0}}));
    library.append(pipeMaterialDefinition(
        QStringLiteral("AOWIS Synthetic PE-A"), {{0, 151.0}, {20, 143.0}}));
    library.append(pipeMaterialDefinition(
        QStringLiteral("AOWIS Synthetic Polymer-B"), {{0, 149.0}, {40, 142.0}}));
    setPipeLibrary(project, library);

    EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
    QVariantMap pipe = pipes.rows.first();
    pipe.insert(QStringLiteral("roughness"), QVariant());
    pipe.insert(QStringLiteral("material"), QStringLiteral("AOWIS Synthetic Iron"));
    pipe.insert(QStringLiteral("year"), 1984);
    if (!pipes.columns.contains(QStringLiteral("roughness")))
        pipes.columns.append(QStringLiteral("roughness"));
    if (!pipes.columns.contains(QStringLiteral("material")))
        pipes.columns.append(QStringLiteral("material"));
    if (!pipes.columns.contains(QStringLiteral("year")))
        pipes.columns.append(QStringLiteral("year"));
    pipes.rows = {pipe};
    project.tables.insert(QStringLiteral("pipes"), pipes);

    const EpanetJsProjectConversionResult first = EpanetJsProjectConverter::convert(project);
    const EpanetJsProjectConversionResult second = EpanetJsProjectConverter::convert(project);

    expectTrue(first.success,
               "converter accepts an embedded synthetic epanet-js pipe material library");
    expectTrue(first.network.pipe_materials.size() == 4,
               "all embedded epanet-js pipe materials are imported");
    expectTrue(first.network.links_pipes.size() == 1,
               "pipe using material-derived roughness is imported");
    if (first.network.links_pipes.size() != 1)
        return;

    const HydraulicPipeMaterial *ductile_iron = nullptr;
    for (const HydraulicPipeMaterial &material : first.network.pipe_materials)
    {
        if (material.id == QStringLiteral("AOWIS Synthetic Iron"))
        {
            ductile_iron = &material;
            break;
        }
    }
    expectTrue(ductile_iron != nullptr,
               "synthetic iron material is preserved by name");
    if (ductile_iron == nullptr)
        return;

    expectTrue(!ductile_iron->uuid.isNull(),
               "imported pipe material gets a deterministic UUID");
    expectTrue(ductile_iron->roughness_by_age.size() == 3,
               "all synthetic iron age/roughness rows are imported");
    if (ductile_iron->roughness_by_age.size() == 3)
    {
        expectTrue(ductile_iron->roughness_by_age.at(0).age_years == 0
                       && ductile_iron->roughness_by_age.at(1).age_years == 40
                       && ductile_iron->roughness_by_age.at(2).age_years == 50,
                   "material age rows retain epanet-js ordering and ages");
        expectTrue(ductile_iron->roughness_by_age.at(1).roughness_hazen_williams.has_value(),
                   "Hazen-Williams library value is imported into the formula-specific field");
        if (ductile_iron->roughness_by_age.at(1).roughness_hazen_williams.has_value())
        {
            expectNear(
                ductile_iron->roughness_by_age.at(1).roughness_hazen_williams.value(),
                117.0, 1e-12,
                "synthetic iron age-40 roughness is preserved");
        }
    }

    const HydraulicLinkPipe &imported = first.network.links_pipes.first();
    expectTrue(imported.material_uuid == ductile_iron->uuid,
               "pipe material label resolves to the imported material UUID");
    expectTrue(imported.roughness_mode == HydraulicPipeRoughnessMode::MaterialLibrary,
               "NULL epanet-js roughness selects material-library roughness mode");
    expectTrue(imported.metadata.date_installed.has_value()
                   && imported.metadata.date_installed->year() == 1984,
               "material-backed pipe retains its installation year");

    const std::optional<double> age_42_roughness = resolveHydraulicPipeMaterialRoughness(
        *ductile_iron, HydraulicHeadlossFormula::HazenWilliams, 42);
    expectTrue(age_42_roughness.has_value(),
               "imported material library resolves effective roughness by pipe age");
    if (age_42_roughness.has_value())
        expectNear(age_42_roughness.value(), 117.0, 1e-12,
                   "effective roughness uses the last age entry not newer than the pipe");

    const HydraulicPipeMaterial *second_ductile_iron = nullptr;
    for (const HydraulicPipeMaterial &material : second.network.pipe_materials)
    {
        if (material.id == QStringLiteral("AOWIS Synthetic Iron"))
        {
            second_ductile_iron = &material;
            break;
        }
    }
    expectTrue(second_ductile_iron != nullptr
                   && second_ductile_iron->uuid == ductile_iron->uuid,
               "material UUID mapping is stable across repeated imports");
}

void testRejectsMissingMaterialForNullRoughnessPipe()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("length"), QStringLiteral("m"));
    units.insert(QStringLiteral("diameter"), QStringLiteral("mm"));
    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("missing-material-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("headlossFormula"), QStringLiteral("H-W"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    QJsonArray library;
    library.append(pipeMaterialDefinition(QStringLiteral("AOWIS Synthetic Polymer"), {{0, 149.0}}));
    setPipeLibrary(project, library);

    EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
    QVariantMap pipe = pipes.rows.first();
    pipe.insert(QStringLiteral("roughness"), QVariant());
    pipe.insert(QStringLiteral("material"), QStringLiteral("AOWIS Synthetic Iron"));
    pipe.insert(QStringLiteral("year"), 1984);
    pipes.columns.append(QStringLiteral("roughness"));
    pipes.columns.append(QStringLiteral("material"));
    pipes.columns.append(QStringLiteral("year"));
    pipes.rows = {pipe};
    project.tables.insert(QStringLiteral("pipes"), pipes);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);
    expectTrue(!result.success,
               "converter rejects NULL roughness when the referenced material is absent");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("missing-pipe-material-reference")),
               "missing material produces a dedicated conversion diagnostic");
}

void testImportsPipes()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("length"), QStringLiteral("m"));
    units.insert(QStringLiteral("diameter"), QStringLiteral("mm"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("pipe-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("headlossFormula"), QStringLiteral("H-W"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
    QVariantMap pipe = pipes.rows.first();
    pipe.insert(QStringLiteral("is_active"), 0);
    pipe.insert(QStringLiteral("coords"), QStringLiteral("[[18,11],[18.05,11.05],[18.1,11.1]]"));
    pipe.insert(QStringLiteral("length"), 175.5);
    pipe.insert(QStringLiteral("initial_status"), QStringLiteral("check-valve"));
    pipe.insert(QStringLiteral("diameter"), 150.0);
    pipe.insert(QStringLiteral("roughness"), 125.0);
    pipe.insert(QStringLiteral("minor_loss"), 0.35);
    pipe.insert(QStringLiteral("material"), QStringLiteral("AOWIS Synthetic Iron"));
    pipe.insert(QStringLiteral("year"), 1984);
    pipes.columns = {
        QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("is_active"),
        QStringLiteral("start_node_id"), QStringLiteral("end_node_id"),
        QStringLiteral("coords"), QStringLiteral("length"),
        QStringLiteral("initial_status"), QStringLiteral("diameter"),
        QStringLiteral("roughness"), QStringLiteral("minor_loss"),
        QStringLiteral("material"), QStringLiteral("year")
    };
    pipes.rows = {pipe};
    project.tables.insert(QStringLiteral("pipes"), pipes);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter accepts an epanet-js pipe with full hydraulic fields");
    expectTrue(result.network.links_pipes.size() == 1, "one epanet-js pipe is imported");
    if (result.network.links_pipes.size() != 1)
        return;

    const HydraulicLinkPipe &imported = result.network.links_pipes.first();
    expectTrue(imported.id == QStringLiteral("P1"), "pipe label becomes the AOWIS pipe id");
    expectTrue(imported.uuid == result.id_map.uuidFor(QStringLiteral("pipes"), 10),
               "pipe UUID resolves through the deterministic id map");
    expectTrue(imported.node_uuid_from == result.id_map.uuidFor(QStringLiteral("junctions"), 1),
               "pipe start node reference is imported");
    expectTrue(imported.node_uuid_to == result.id_map.uuidFor(QStringLiteral("junctions"), 2),
               "pipe end node reference is imported");
    expectTrue(!imported.metadata.enabled, "pipe enabled state is imported");
    expectTrue(imported.length_measured_m.has_value(), "epanet-js pipe length is retained as measured length");
    if (imported.length_measured_m.has_value())
        expectNear(imported.length_measured_m.value(), 175.5, 1e-12,
                   "metric pipe length is imported in metres");
    expectTrue(imported.length_calculated_m > 0.0,
               "pipe geometric length is calculated from epanet-js coordinates");
    expectNear(imported.diameter_mm, 150.0, 1e-12,
               "metric pipe diameter is imported in millimetres");
    expectTrue(imported.roughness_mode == HydraulicPipeRoughnessMode::Explicit,
               "explicit epanet-js pipe roughness remains explicit before material import");
    expectNear(imported.roughness_hazen_williams, 125.0, 1e-12,
               "Hazen-Williams roughness is imported into the active formula field");
    expectNear(imported.minor_loss_coefficient, 0.35, 1e-12,
               "pipe minor-loss coefficient is imported");
    expectTrue(imported.initial_status == HydraulicLinkPipeInitialStatus::CheckValve,
               "epanet-js check-valve pipe status is imported");
    expectTrue(imported.vertices.size() == 1,
               "only intermediate epanet-js coordinates become AOWIS link vertices");
    if (imported.vertices.size() == 1)
    {
        expectNear(imported.vertices.first().coordinate_wgs84.longitude_deg, 18.05, 1e-12,
                   "pipe intermediate longitude is imported");
        expectNear(imported.vertices.first().coordinate_wgs84.latitude_deg, 11.05, 1e-12,
                   "pipe intermediate latitude is imported");
    }
    expectTrue(imported.metadata.date_installed.has_value(),
               "epanet-js pipe installation year is retained as an installation date");
    if (imported.metadata.date_installed.has_value())
    {
        expectTrue(imported.metadata.date_installed->year() == 1984,
                   "pipe installation year is preserved");
        expectTrue(imported.metadata.date_installed->month() == 1
                       && imported.metadata.date_installed->day() == 1,
                   "year-only installation data uses a deterministic January 1 date");
    }
    expectTrue(imported.material_uuid.isNull(),
               "pipe material remains unassigned when the source project has no embedded material library");
}

void testConvertsUsPipeUnitsAndDarcyRoughness()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("gpm"));
    units.insert(QStringLiteral("length"), QStringLiteral("ft"));
    units.insert(QStringLiteral("diameter"), QStringLiteral("in"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("us-pipe-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("headlossFormula"), QStringLiteral("D-W"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
    QVariantMap pipe = pipes.rows.first();
    pipe.insert(QStringLiteral("length"), 100.0);
    pipe.insert(QStringLiteral("diameter"), 12.0);
    pipe.insert(QStringLiteral("roughness"), 0.5);
    pipe.insert(QStringLiteral("initial_status"), QStringLiteral("closed"));
    pipes.columns.append(QStringLiteral("length"));
    pipes.columns.append(QStringLiteral("diameter"));
    pipes.columns.append(QStringLiteral("roughness"));
    pipes.columns.append(QStringLiteral("initial_status"));
    pipes.rows = {pipe};
    project.tables.insert(QStringLiteral("pipes"), pipes);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter accepts US customary epanet-js pipe units");
    expectTrue(result.network.links_pipes.size() == 1, "US pipe is imported");
    if (result.network.links_pipes.size() != 1)
        return;

    const HydraulicLinkPipe &imported = result.network.links_pipes.first();
    expectTrue(imported.length_measured_m.has_value(), "US pipe length is retained");
    if (imported.length_measured_m.has_value())
        expectNear(imported.length_measured_m.value(), 30.48, 1e-12,
                   "pipe length feet are converted to metres");
    expectNear(imported.diameter_mm, 304.8, 1e-12,
               "pipe diameter inches are converted to millimetres");
    expectNear(imported.roughness_darcy_weisbach_mm, 0.1524, 1e-12,
               "US Darcy-Weisbach millifeet roughness is converted to millimetres");
    expectTrue(imported.initial_status == HydraulicLinkPipeInitialStatus::Closed,
               "closed pipe status is imported");
}

void testRejectsMalformedPipeData()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("length"), QStringLiteral("m"));
    units.insert(QStringLiteral("diameter"), QStringLiteral("mm"));
    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("broken-pipe-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
    QVariantMap pipe = pipes.rows.first();
    pipe.insert(QStringLiteral("coords"), QStringLiteral("[[18,11],[999,11]]"));
    pipe.insert(QStringLiteral("initial_status"), QStringLiteral("mystery"));
    pipes.columns.append(QStringLiteral("coords"));
    pipes.columns.append(QStringLiteral("initial_status"));
    pipes.rows = {pipe};
    project.tables.insert(QStringLiteral("pipes"), pipes);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(!result.success, "converter rejects malformed core pipe data");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("invalid-pipe-coordinates")),
               "invalid pipe geometry produces a dedicated diagnostic");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("unknown-pipe-status")),
               "unknown pipe initial status produces a dedicated diagnostic");
}


void testImportsPumpsAndValves()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("head"), QStringLiteral("m"));
    units.insert(QStringLiteral("pressure"), QStringLiteral("mwc"));
    units.insert(QStringLiteral("diameter"), QStringLiteral("mm"));
    units.insert(QStringLiteral("power"), QStringLiteral("kW"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("pump-valve-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    const QVariantMap speed_pattern = {
        {QStringLiteral("id"), 20},
        {QStringLiteral("label"), QStringLiteral("SPEED")},
        {QStringLiteral("type"), QStringLiteral("pumpSpeed")},
        {QStringLiteral("multipliers"), QStringLiteral("[1,0.8,1.1]")}
    };
    const QVariantMap price_pattern = {
        {QStringLiteral("id"), 21},
        {QStringLiteral("label"), QStringLiteral("PRICE")},
        {QStringLiteral("type"), QStringLiteral("energyPrice")},
        {QStringLiteral("multipliers"), QStringLiteral("[1,2,0.5]")}
    };
    project.tables.insert(
        QStringLiteral("patterns"),
        makeTable(
            QStringLiteral("patterns"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("type"),
             QStringLiteral("multipliers")},
            {speed_pattern, price_pattern}));

    const QVariantMap efficiency_curve = {
        {QStringLiteral("id"), 30},
        {QStringLiteral("label"), QStringLiteral("EFF")},
        {QStringLiteral("type"), QStringLiteral("efficiency")},
        {QStringLiteral("points"), QStringLiteral("[[0,70],[50,82],[100,75]]")}
    };
    project.tables.insert(
        QStringLiteral("curves"),
        makeTable(
            QStringLiteral("curves"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("type"),
             QStringLiteral("points")},
            {efficiency_curve}));

    const QVariantMap pump = {
        {QStringLiteral("id"), 11},
        {QStringLiteral("label"), QStringLiteral("PU1")},
        {QStringLiteral("is_active"), 1},
        {QStringLiteral("start_node_id"), 1},
        {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("coords"), QStringLiteral("[[18,11],[18.05,11.05],[18.1,11.1]]")},
        {QStringLiteral("initial_status"), QStringLiteral("off")},
        {QStringLiteral("definition_type"), QStringLiteral("designPointCurve")},
        {QStringLiteral("power"), QVariant()},
        {QStringLiteral("speed"), 0.85},
        {QStringLiteral("speed_pattern_id"), 20},
        {QStringLiteral("efficiency_curve_id"), 30},
        {QStringLiteral("energy_price"), 0.22},
        {QStringLiteral("energy_price_pattern_id"), 21},
        {QStringLiteral("curve_id"), QVariant()},
        {QStringLiteral("curve_points"), QStringLiteral("[{\"x\":50,\"y\":40}]")}
    };
    project.tables.insert(
        QStringLiteral("pumps"),
        makeTable(
            QStringLiteral("pumps"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("is_active"),
             QStringLiteral("start_node_id"), QStringLiteral("end_node_id"),
             QStringLiteral("coords"), QStringLiteral("initial_status"),
             QStringLiteral("definition_type"), QStringLiteral("power"),
             QStringLiteral("speed"), QStringLiteral("speed_pattern_id"),
             QStringLiteral("efficiency_curve_id"), QStringLiteral("energy_price"),
             QStringLiteral("energy_price_pattern_id"), QStringLiteral("curve_id"),
             QStringLiteral("curve_points")},
            {pump}));

    const QVariantMap valve = {
        {QStringLiteral("id"), 12},
        {QStringLiteral("label"), QStringLiteral("V1")},
        {QStringLiteral("is_active"), 1},
        {QStringLiteral("start_node_id"), 1},
        {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("coords"), QStringLiteral("[[18,11],[18.1,11.1]]")},
        {QStringLiteral("initial_status"), QStringLiteral("active")},
        {QStringLiteral("diameter"), 150.0},
        {QStringLiteral("minor_loss"), 0.1},
        {QStringLiteral("valve_kind"), QStringLiteral("prv")},
        {QStringLiteral("setting"), 26.0},
        {QStringLiteral("curve_id"), QVariant()},
        {QStringLiteral("target_node_id"), QVariant()}
    };
    project.tables.insert(
        QStringLiteral("valves"),
        makeTable(
            QStringLiteral("valves"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("is_active"),
             QStringLiteral("start_node_id"), QStringLiteral("end_node_id"),
             QStringLiteral("coords"), QStringLiteral("initial_status"),
             QStringLiteral("diameter"), QStringLiteral("minor_loss"),
             QStringLiteral("valve_kind"), QStringLiteral("setting"),
             QStringLiteral("curve_id"), QStringLiteral("target_node_id")},
            {valve}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter imports epanet-js pumps and valves");
    expectTrue(result.network.links_pumps.size() == 1, "pump is imported");
    expectTrue(result.network.links_valves.size() == 1, "valve is imported");
    expectTrue(result.network.curves_pump_head.size() == 1,
               "inline epanet-js pump curve becomes an AOWIS pump-head curve");

    if (result.network.links_pumps.size() == 1)
    {
        const HydraulicLinkPump &imported = result.network.links_pumps.first();
        expectTrue(imported.id == QStringLiteral("PU1"), "pump label becomes AOWIS id");
        expectTrue(imported.definition_type == HydraulicLinkPumpDefinitionType::OnePointCurve,
                   "design-point pump becomes an AOWIS one-point curve pump");
        expectTrue(!imported.head_curve_uuid.isNull(), "pump head curve is assigned");
        expectNear(imported.initial_speed_ratio, 0.85, 1e-12, "pump speed is imported");
        expectTrue(imported.initial_status == HydraulicLinkPumpInitialStatus::Off,
                   "pump initial status is imported");
        expectTrue(
            imported.speed_pattern_uuid
                == result.id_map.uuidFor(QStringLiteral("patterns"), 20),
            "pump speed pattern reference is imported");
        expectTrue(
            imported.efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Curve,
            "pump efficiency curve mode is imported");
        expectTrue(
            imported.efficiency_curve_uuid
                == result.id_map.uuidFor(QStringLiteral("curves"), 30),
            "pump efficiency curve reference is imported");
        expectTrue(
            imported.energy_price_input_type == HydraulicLinkPumpEnergyPriceInputType::Pattern,
            "pump energy price pattern mode is imported");
        expectNear(imported.energy_price_per_kw_h, 0.22, 1e-12,
                   "pump-specific energy price is imported");
        expectTrue(
            imported.price_pattern_uuid
                == result.id_map.uuidFor(QStringLiteral("patterns"), 21),
            "pump energy-price pattern reference is imported");
        expectTrue(imported.vertices.size() == 1,
                   "pump intermediate geometry becomes an AOWIS link vertex");
    }

    if (result.network.curves_pump_head.size() == 1)
    {
        const HydraulicCurvePumpHead &curve = result.network.curves_pump_head.first();
        expectTrue(curve.points.size() == 1, "design-point curve keeps one point");
        if (curve.points.size() == 1)
        {
            expectNear(curve.points.first().flow_m3_per_h, 180.0, 1e-12,
                       "pump curve flow is converted from l/s to m3/h");
            expectNear(curve.points.first().head_gain_m, 40.0, 1e-12,
                       "pump curve head is imported in metres");
        }
    }

    if (result.network.links_valves.size() == 1)
    {
        const HydraulicLinkValve &imported = result.network.links_valves.first();
        expectTrue(imported.type == HydraulicLinkValveType::PRV,
                   "epanet-js PRV type is imported");
        expectNear(imported.diameter_mm, 150.0, 1e-12,
                   "valve diameter is imported");
        expectNear(imported.setting_pressure_head_m, 26.0, 1e-12,
                   "metric PRV pressure setting is imported as metres of head");
        expectNear(imported.minor_loss_coefficient, 0.1, 1e-12,
                   "valve minor loss is imported");
        expectTrue(imported.initial_status == HydraulicLinkValveInitialStatus::Active,
                   "valve active status is imported");
    }
}

void testConvertsUsPumpAndValveUnits()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("gpm"));
    units.insert(QStringLiteral("pressure"), QStringLiteral("psi"));
    units.insert(QStringLiteral("diameter"), QStringLiteral("in"));
    units.insert(QStringLiteral("power"), QStringLiteral("hp"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("us-pump-valve-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    const QVariantMap pump = {
        {QStringLiteral("id"), 11},
        {QStringLiteral("label"), QStringLiteral("PU-US")},
        {QStringLiteral("start_node_id"), 1},
        {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("initial_status"), QStringLiteral("on")},
        {QStringLiteral("definition_type"), QStringLiteral("constantPower")},
        {QStringLiteral("power"), 100.0},
        {QStringLiteral("speed"), 1.0}
    };
    project.tables.insert(
        QStringLiteral("pumps"),
        makeTable(
            QStringLiteral("pumps"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("start_node_id"), QStringLiteral("end_node_id"),
             QStringLiteral("initial_status"), QStringLiteral("definition_type"),
             QStringLiteral("power"), QStringLiteral("speed")},
            {pump}));

    const QVariantMap valve = {
        {QStringLiteral("id"), 12},
        {QStringLiteral("label"), QStringLiteral("FCV-US")},
        {QStringLiteral("start_node_id"), 1},
        {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("initial_status"), QStringLiteral("active")},
        {QStringLiteral("diameter"), 12.0},
        {QStringLiteral("minor_loss"), 0.0},
        {QStringLiteral("valve_kind"), QStringLiteral("fcv")},
        {QStringLiteral("setting"), 100.0}
    };
    project.tables.insert(
        QStringLiteral("valves"),
        makeTable(
            QStringLiteral("valves"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("start_node_id"), QStringLiteral("end_node_id"),
             QStringLiteral("initial_status"), QStringLiteral("diameter"),
             QStringLiteral("minor_loss"), QStringLiteral("valve_kind"),
             QStringLiteral("setting")},
            {valve}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter accepts US pump and valve units");
    expectTrue(result.network.links_pumps.size() == 1, "US pump is imported");
    expectTrue(result.network.links_valves.size() == 1, "US valve is imported");

    if (result.network.links_pumps.size() == 1)
    {
        const HydraulicLinkPump &imported = result.network.links_pumps.first();
        expectTrue(imported.definition_type == HydraulicLinkPumpDefinitionType::ConstantPower,
                   "constant-power pump definition is imported");
        expectNear(imported.constant_power_kw, 74.56998715822702, 1e-10,
                   "US pump horsepower is converted to kilowatts");
    }

    if (result.network.links_valves.size() == 1)
    {
        const HydraulicLinkValve &imported = result.network.links_valves.first();
        expectTrue(imported.type == HydraulicLinkValveType::FCV,
                   "US flow-control valve type is imported");
        expectNear(imported.diameter_mm, 304.8, 1e-12,
                   "US valve diameter inches are converted to millimetres");
        expectNear(imported.setting_flow_m3_per_h, 22.712470704, 1e-12,
                   "US FCV setting gpm is converted to m3/h");
    }
}


void testImportsReferencedPumpHeadCurve()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("head"), QStringLiteral("m"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("pump-curve-reference-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    const QVariantMap head_curve = {
        {QStringLiteral("id"), 50},
        {QStringLiteral("label"), QStringLiteral("PUMP-LIB")},
        {QStringLiteral("type"), QStringLiteral("pump")},
        {QStringLiteral("points"), QStringLiteral("[[0,60],[25,50],[50,30]]")}
    };
    project.tables.insert(
        QStringLiteral("curves"),
        makeTable(
            QStringLiteral("curves"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("type"), QStringLiteral("points")},
            {head_curve}));

    const QVariantMap pump = {
        {QStringLiteral("id"), 11},
        {QStringLiteral("label"), QStringLiteral("PU-LIB")},
        {QStringLiteral("start_node_id"), 1},
        {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("definition_type"), QStringLiteral("library")},
        {QStringLiteral("curve_id"), 50},
        {QStringLiteral("speed"), 1.0}
    };
    project.tables.insert(
        QStringLiteral("pumps"),
        makeTable(
            QStringLiteral("pumps"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("start_node_id"), QStringLiteral("end_node_id"),
             QStringLiteral("definition_type"), QStringLiteral("curve_id"),
             QStringLiteral("speed")},
            {pump}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter imports a pump referencing a stored head curve");
    expectTrue(result.network.links_pumps.size() == 1, "referenced-curve pump is imported");
    expectTrue(result.network.curves_pump_head.size() == 1,
               "stored pump head curve remains in the typed AOWIS curve collection");
    if (result.network.links_pumps.size() == 1)
    {
        const HydraulicLinkPump &imported = result.network.links_pumps.first();
        expectTrue(imported.definition_type == HydraulicLinkPumpDefinitionType::Library,
                   "epanet-js library pump retains library definition semantics");
        expectTrue(
            imported.head_curve_uuid
                == result.id_map.uuidFor(QStringLiteral("curves"), 50),
            "pump references the deterministic UUID of its stored head curve");
    }
}

void testImportsAllValveTypesAndCurves()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("pressure"), QStringLiteral("mwc"));
    units.insert(QStringLiteral("diameter"), QStringLiteral("mm"));
    units.insert(QStringLiteral("head"), QStringLiteral("m"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("valve-types-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    const QVariantMap headloss_curve = {
        {QStringLiteral("id"), 40},
        {QStringLiteral("label"), QStringLiteral("GPV-CURVE")},
        {QStringLiteral("type"), QStringLiteral("headloss")},
        {QStringLiteral("points"), QStringLiteral("[[1,2],[2,5]]")}
    };
    const QVariantMap characteristic_curve = {
        {QStringLiteral("id"), 41},
        {QStringLiteral("label"), QStringLiteral("PCV-CURVE")},
        {QStringLiteral("type"), QStringLiteral("valve")},
        {QStringLiteral("points"), QStringLiteral("[[25,20],[100,100]]")}
    };
    project.tables.insert(
        QStringLiteral("curves"),
        makeTable(
            QStringLiteral("curves"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("type"), QStringLiteral("points")},
            {headloss_curve, characteristic_curve}));

    QList<QVariantMap> valves;
    valves.append(QVariantMap{
        {QStringLiteral("id"), 101}, {QStringLiteral("label"), QStringLiteral("PRV")},
        {QStringLiteral("start_node_id"), 1}, {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("diameter"), 100.0}, {QStringLiteral("minor_loss"), 0.0},
        {QStringLiteral("valve_kind"), QStringLiteral("prv")}, {QStringLiteral("setting"), 10.0}
    });
    valves.append(QVariantMap{
        {QStringLiteral("id"), 102}, {QStringLiteral("label"), QStringLiteral("PSV")},
        {QStringLiteral("start_node_id"), 1}, {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("diameter"), 100.0}, {QStringLiteral("minor_loss"), 0.0},
        {QStringLiteral("valve_kind"), QStringLiteral("psv")}, {QStringLiteral("setting"), 11.0}
    });
    valves.append(QVariantMap{
        {QStringLiteral("id"), 103}, {QStringLiteral("label"), QStringLiteral("PBV")},
        {QStringLiteral("start_node_id"), 1}, {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("diameter"), 100.0}, {QStringLiteral("minor_loss"), 0.0},
        {QStringLiteral("valve_kind"), QStringLiteral("pbv")}, {QStringLiteral("setting"), 12.0}
    });
    valves.append(QVariantMap{
        {QStringLiteral("id"), 104}, {QStringLiteral("label"), QStringLiteral("FCV")},
        {QStringLiteral("start_node_id"), 1}, {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("diameter"), 100.0}, {QStringLiteral("minor_loss"), 0.0},
        {QStringLiteral("valve_kind"), QStringLiteral("fcv")}, {QStringLiteral("setting"), 5.0}
    });
    valves.append(QVariantMap{
        {QStringLiteral("id"), 105}, {QStringLiteral("label"), QStringLiteral("TCV")},
        {QStringLiteral("start_node_id"), 1}, {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("diameter"), 100.0}, {QStringLiteral("minor_loss"), 0.0},
        {QStringLiteral("valve_kind"), QStringLiteral("tcv")}, {QStringLiteral("setting"), 2.5}
    });
    valves.append(QVariantMap{
        {QStringLiteral("id"), 106}, {QStringLiteral("label"), QStringLiteral("GPV")},
        {QStringLiteral("start_node_id"), 1}, {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("diameter"), 100.0}, {QStringLiteral("minor_loss"), 0.0},
        {QStringLiteral("valve_kind"), QStringLiteral("gpv")}, {QStringLiteral("curve_id"), 40}
    });
    valves.append(QVariantMap{
        {QStringLiteral("id"), 107}, {QStringLiteral("label"), QStringLiteral("PCV")},
        {QStringLiteral("start_node_id"), 1}, {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("diameter"), 100.0}, {QStringLiteral("minor_loss"), 0.0},
        {QStringLiteral("valve_kind"), QStringLiteral("pcv")}, {QStringLiteral("setting"), 60.0},
        {QStringLiteral("curve_id"), 41}
    });
    project.tables.insert(
        QStringLiteral("valves"),
        makeTable(
            QStringLiteral("valves"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("start_node_id"), QStringLiteral("end_node_id"),
             QStringLiteral("diameter"), QStringLiteral("minor_loss"),
             QStringLiteral("valve_kind"), QStringLiteral("setting"),
             QStringLiteral("curve_id")},
            valves));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success, "converter imports all seven epanet-js valve types");
    expectTrue(result.network.links_valves.size() == 7, "all valve rows are imported");
    if (result.network.links_valves.size() != 7)
        return;

    expectTrue(result.network.links_valves.at(0).type == HydraulicLinkValveType::PRV,
               "PRV type is mapped");
    expectNear(result.network.links_valves.at(0).setting_pressure_head_m, 10.0, 1e-12,
               "PRV pressure setting is mapped");
    expectTrue(result.network.links_valves.at(1).type == HydraulicLinkValveType::PSV,
               "PSV type is mapped");
    expectNear(result.network.links_valves.at(1).setting_pressure_head_m, 11.0, 1e-12,
               "PSV pressure setting is mapped");
    expectTrue(result.network.links_valves.at(2).type == HydraulicLinkValveType::PBV,
               "PBV type is mapped");
    expectNear(result.network.links_valves.at(2).setting_pressure_head_m, 12.0, 1e-12,
               "PBV pressure setting is mapped");
    expectTrue(result.network.links_valves.at(3).type == HydraulicLinkValveType::FCV,
               "FCV type is mapped");
    expectNear(result.network.links_valves.at(3).setting_flow_m3_per_h, 18.0, 1e-12,
               "FCV flow setting is converted to canonical flow");
    expectTrue(result.network.links_valves.at(4).type == HydraulicLinkValveType::TCV,
               "TCV type is mapped");
    expectNear(result.network.links_valves.at(4).setting_loss_coefficient, 2.5, 1e-12,
               "TCV loss-coefficient setting is mapped");
    expectTrue(result.network.links_valves.at(5).type == HydraulicLinkValveType::GPV,
               "GPV type is mapped");
    expectTrue(
        result.network.links_valves.at(5).head_loss_curve_uuid
            == result.id_map.uuidFor(QStringLiteral("curves"), 40),
        "GPV headloss curve reference is mapped");
    expectTrue(result.network.links_valves.at(6).type == HydraulicLinkValveType::PCV,
               "PCV type is mapped");
    expectNear(result.network.links_valves.at(6).setting_position_percent, 60.0, 1e-12,
               "PCV position setting is mapped");
    expectTrue(
        result.network.links_valves.at(6).characteristic_curve_uuid
            == result.id_map.uuidFor(QStringLiteral("curves"), 41),
        "PCV characteristic curve reference is mapped");
}

void testImportsStructuredPumpLevelSettingControls()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("head"), QStringLiteral("m"));
    units.insert(QStringLiteral("elevation"), QStringLiteral("m"));
    units.insert(QStringLiteral("level"), QStringLiteral("m"));
    units.insert(QStringLiteral("tankDiameter"), QStringLiteral("m"));
    units.insert(QStringLiteral("minVolume"), QStringLiteral("m^3"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("structured-control-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    const QVariantMap tank = {
        {QStringLiteral("id"), 3},
        {QStringLiteral("label"), QStringLiteral("T1")},
        {QStringLiteral("is_active"), 1},
        {QStringLiteral("coord_x"), 18.2},
        {QStringLiteral("coord_y"), 11.2},
        {QStringLiteral("elevation"), 79.0},
        {QStringLiteral("initial_level"), 5.0},
        {QStringLiteral("min_level"), 0.0},
        {QStringLiteral("max_level"), 7.5},
        {QStringLiteral("min_volume"), 0.0},
        {QStringLiteral("diameter"), 20.0},
        {QStringLiteral("overflow"), 0},
        {QStringLiteral("volume_curve_id"), QVariant()}
    };
    project.tables.insert(
        QStringLiteral("tanks"),
        makeTable(
            QStringLiteral("tanks"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("is_active"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y"),
             QStringLiteral("elevation"), QStringLiteral("initial_level"),
             QStringLiteral("min_level"), QStringLiteral("max_level"),
             QStringLiteral("min_volume"), QStringLiteral("diameter"),
             QStringLiteral("overflow"), QStringLiteral("volume_curve_id")},
            {tank}));

    const QVariantMap pump = {
        {QStringLiteral("id"), 11},
        {QStringLiteral("label"), QStringLiteral("PU1")},
        {QStringLiteral("is_active"), 1},
        {QStringLiteral("start_node_id"), 1},
        {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("coords"), QStringLiteral("[[18,11],[18.1,11.1]]")},
        {QStringLiteral("initial_status"), QStringLiteral("off")},
        {QStringLiteral("definition_type"), QStringLiteral("designPointCurve")},
        {QStringLiteral("power"), QVariant()},
        {QStringLiteral("speed"), 1.0},
        {QStringLiteral("curve_id"), QVariant()},
        {QStringLiteral("curve_points"), QStringLiteral("[{\"x\":50,\"y\":40}]")}
    };
    project.tables.insert(
        QStringLiteral("pumps"),
        makeTable(
            QStringLiteral("pumps"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("is_active"),
             QStringLiteral("start_node_id"), QStringLiteral("end_node_id"),
             QStringLiteral("coords"), QStringLiteral("initial_status"),
             QStringLiteral("definition_type"), QStringLiteral("power"),
             QStringLiteral("speed"), QStringLiteral("curve_id"),
             QStringLiteral("curve_points")},
            {pump}));

    QJsonObject on;
    on.insert(QStringLiteral("level"), 3.5);
    on.insert(QStringLiteral("setting"), 1.0);
    QJsonObject off;
    off.insert(QStringLiteral("level"), 5.2);
    QJsonObject control;
    control.insert(QStringLiteral("id"), QStringLiteral("FdH39PU4p1TcpjWZ_QifT"));
    control.insert(QStringLiteral("type"), QStringLiteral("level-setting"));
    control.insert(QStringLiteral("linkId"), 11);
    control.insert(QStringLiteral("tankId"), 3);
    control.insert(QStringLiteral("on"), on);
    control.insert(QStringLiteral("off"), off);
    QJsonArray controls;
    controls.append(control);
    const QVariantMap controls_row = {
        {QStringLiteral("id"), 1},
        {QStringLiteral("data"), QString::fromUtf8(
            QJsonDocument(controls).toJson(QJsonDocument::Compact))}
    };
    project.tables.insert(
        QStringLiteral("controls"),
        makeTable(
            QStringLiteral("controls"),
            {QStringLiteral("id"), QStringLiteral("data")},
            {controls_row}));

    const EpanetJsProjectConversionResult first = EpanetJsProjectConverter::convert(project);
    const EpanetJsProjectConversionResult second = EpanetJsProjectConverter::convert(project);

    expectTrue(first.success,
               "converter imports epanet-js structured pump level-setting control");
    expectTrue(first.network.controls_simple.size() == 2,
               "one epanet-js on/off level-setting becomes two AOWIS simple controls");
    if (first.network.controls_simple.size() != 2)
        return;

    const HydraulicControlSimple &low = first.network.controls_simple.at(0);
    const HydraulicControlSimple &high = first.network.controls_simple.at(1);
    const QUuid pump_uuid = first.id_map.uuidFor(QStringLiteral("pumps"), 11);
    const QUuid tank_uuid = first.id_map.uuidFor(QStringLiteral("tanks"), 3);

    expectTrue(low.type == HydraulicControlSimpleType::LowLevel,
               "epanet-js on action becomes an AOWIS low-level control");
    expectTrue(low.link_uuid == pump_uuid,
               "low-level control targets the imported pump");
    expectTrue(low.trigger_node_uuid == tank_uuid,
               "low-level control watches the imported tank");
    expectNear(low.trigger_water_level_m, 3.5, 1e-12,
               "low-level trigger is imported in metres");
    expectTrue(low.action == HydraulicControlActionType::Setting,
               "epanet-js on setting becomes an AOWIS setting action");
    expectTrue(low.setting.pump_speed_ratio.has_value(),
               "pump setting action carries a speed ratio");
    if (low.setting.pump_speed_ratio.has_value())
        expectNear(*low.setting.pump_speed_ratio, 1.0, 1e-12,
                   "pump on setting preserves speed ratio");

    expectTrue(high.type == HydraulicControlSimpleType::HighLevel,
               "epanet-js off action becomes an AOWIS high-level control");
    expectTrue(high.link_uuid == pump_uuid,
               "high-level control targets the imported pump");
    expectTrue(high.trigger_node_uuid == tank_uuid,
               "high-level control watches the imported tank");
    expectNear(high.trigger_water_level_m, 5.2, 1e-12,
               "high-level trigger is imported in metres");
    expectTrue(high.action == HydraulicControlActionType::Close,
               "epanet-js off action without a setting closes the pump");
    expectTrue(!high.setting.pump_speed_ratio.has_value(),
               "pump close action does not invent a speed setting");

    expectTrue(
        first.network.controls_simple.at(0).uuid == second.network.controls_simple.at(0).uuid
            && first.network.controls_simple.at(1).uuid == second.network.controls_simple.at(1).uuid,
        "structured control UUIDs are deterministic across repeated imports");
}

void testRejectsBrokenStructuredLevelSettingReference()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("level"), QStringLiteral("m"));
    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("broken-control-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    QJsonObject on;
    on.insert(QStringLiteral("level"), 3.5);
    QJsonObject control;
    control.insert(QStringLiteral("id"), QStringLiteral("BROKEN"));
    control.insert(QStringLiteral("type"), QStringLiteral("level-setting"));
    control.insert(QStringLiteral("linkId"), 999);
    control.insert(QStringLiteral("tankId"), 998);
    control.insert(QStringLiteral("on"), on);
    QJsonArray controls;
    controls.append(control);
    const QVariantMap controls_row = {
        {QStringLiteral("id"), 1},
        {QStringLiteral("data"), QString::fromUtf8(
            QJsonDocument(controls).toJson(QJsonDocument::Compact))}
    };
    project.tables.insert(
        QStringLiteral("controls"),
        makeTable(
            QStringLiteral("controls"),
            {QStringLiteral("id"), QStringLiteral("data")},
            {controls_row}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(!result.success,
               "converter rejects structured controls with missing pump references");
    expectTrue(hasDiagnosticCode(
                   result, QStringLiteral("missing-level-setting-link-reference")),
               "broken level-setting link produces a dedicated diagnostic");
}

void testImportsRawSimpleLevelControls()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("level"), QStringLiteral("ft"));
    units.insert(QStringLiteral("length"), QStringLiteral("ft"));
    units.insert(QStringLiteral("elevation"), QStringLiteral("ft"));
    units.insert(QStringLiteral("initialLevel"), QStringLiteral("ft"));
    units.insert(QStringLiteral("minLevel"), QStringLiteral("ft"));
    units.insert(QStringLiteral("maxLevel"), QStringLiteral("ft"));
    units.insert(QStringLiteral("tankDiameter"), QStringLiteral("ft"));
    units.insert(QStringLiteral("minVolume"), QStringLiteral("ft^3"));
    units.insert(QStringLiteral("volume"), QStringLiteral("ft^3"));
    units.insert(QStringLiteral("pressure"), QStringLiteral("psi"));
    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("raw-control-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    const QVariantMap tank = {
        {QStringLiteral("id"), 3},
        {QStringLiteral("label"), QStringLiteral("T1")},
        {QStringLiteral("is_active"), 1},
        {QStringLiteral("coord_x"), 18.2},
        {QStringLiteral("coord_y"), 11.2},
        {QStringLiteral("elevation"), 795.0},
        {QStringLiteral("initial_level"), 125.0},
        {QStringLiteral("min_level"), 0.0},
        {QStringLiteral("max_level"), 150.0},
        {QStringLiteral("min_volume"), 0.0},
        {QStringLiteral("diameter"), 90.0},
        {QStringLiteral("overflow"), 0},
        {QStringLiteral("volume_curve_id"), QVariant()}
    };
    project.tables.insert(
        QStringLiteral("tanks"),
        makeTable(
            QStringLiteral("tanks"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("is_active"),
             QStringLiteral("coord_x"), QStringLiteral("coord_y"),
             QStringLiteral("elevation"), QStringLiteral("initial_level"),
             QStringLiteral("min_level"), QStringLiteral("max_level"),
             QStringLiteral("min_volume"), QStringLiteral("diameter"),
             QStringLiteral("overflow"), QStringLiteral("volume_curve_id")},
            {tank}));

    QJsonObject action_reference;
    action_reference.insert(QStringLiteral("assetId"), 10);
    action_reference.insert(QStringLiteral("isActionTarget"), true);
    QJsonObject trigger_reference;
    trigger_reference.insert(QStringLiteral("assetId"), 3);
    trigger_reference.insert(QStringLiteral("isActionTarget"), false);
    QJsonArray references;
    references.append(action_reference);
    references.append(trigger_reference);

    QJsonObject open_control;
    open_control.insert(
        QStringLiteral("template"),
        QStringLiteral("LINK {{0}} OPEN IF NODE {{1}} BELOW 120"));
    open_control.insert(QStringLiteral("assetReferences"), references);
    QJsonObject close_control;
    close_control.insert(
        QStringLiteral("template"),
        QStringLiteral("LINK {{0}} CLOSED IF NODE {{1}} ABOVE 128"));
    close_control.insert(QStringLiteral("assetReferences"), references);

    QJsonArray simple_controls;
    simple_controls.append(open_control);
    simple_controls.append(close_control);
    QJsonObject raw;
    raw.insert(QStringLiteral("simple"), simple_controls);
    raw.insert(QStringLiteral("rules"), QJsonArray());
    const QVariantMap raw_row = {
        {QStringLiteral("id"), 1},
        {QStringLiteral("data"), QString::fromUtf8(
            QJsonDocument(raw).toJson(QJsonDocument::Compact))}
    };
    project.tables.insert(
        QStringLiteral("raw_controls"),
        makeTable(
            QStringLiteral("raw_controls"),
            {QStringLiteral("id"), QStringLiteral("data")},
            {raw_row}));

    const EpanetJsProjectConversionResult first = EpanetJsProjectConverter::convert(project);
    const EpanetJsProjectConversionResult second = EpanetJsProjectConverter::convert(project);

    expectTrue(first.success,
               "converter imports epanet-js raw simple level controls");
    expectTrue(first.network.controls_simple.size() == 2,
               "two epanet-js raw simple controls become two AOWIS simple controls");
    if (first.network.controls_simple.size() != 2)
        return;

    const HydraulicControlSimple &open = first.network.controls_simple.at(0);
    const HydraulicControlSimple &close = first.network.controls_simple.at(1);
    const QUuid pipe_uuid = first.id_map.uuidFor(QStringLiteral("pipes"), 10);
    const QUuid tank_uuid = first.id_map.uuidFor(QStringLiteral("tanks"), 3);

    expectTrue(open.type == HydraulicControlSimpleType::LowLevel,
               "BELOW raw control becomes AOWIS low-level trigger");
    expectTrue(open.link_uuid == pipe_uuid,
               "raw OPEN control preserves target link reference");
    expectTrue(open.trigger_node_uuid == tank_uuid,
               "raw OPEN control preserves trigger node reference");
    expectNear(open.trigger_water_level_m, 36.576, 1e-12,
               "120 ft raw tank level converts to metres");
    expectTrue(open.action == HydraulicControlActionType::Open,
               "raw OPEN action is preserved");

    expectTrue(close.type == HydraulicControlSimpleType::HighLevel,
               "ABOVE raw control becomes AOWIS high-level trigger");
    expectTrue(close.link_uuid == pipe_uuid,
               "raw CLOSED control preserves target link reference");
    expectTrue(close.trigger_node_uuid == tank_uuid,
               "raw CLOSED control preserves trigger node reference");
    expectNear(close.trigger_water_level_m, 39.0144, 1e-12,
               "128 ft raw tank level converts to metres");
    expectTrue(close.action == HydraulicControlActionType::Close,
               "raw CLOSED action is preserved");

    expectTrue(
        first.network.controls_simple.at(0).uuid == second.network.controls_simple.at(0).uuid
            && first.network.controls_simple.at(1).uuid == second.network.controls_simple.at(1).uuid,
        "raw simple-control UUIDs are deterministic across repeated imports");
}

void testImportsRawRules()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("pressure"), QStringLiteral("mwc"));
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("raw-rule-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    QJsonArray references;
    QJsonObject pressure_node;
    pressure_node.insert(QStringLiteral("assetId"), 1);
    pressure_node.insert(QStringLiteral("isActionTarget"), false);
    references.append(pressure_node);
    QJsonObject status_link;
    status_link.insert(QStringLiteral("assetId"), 10);
    status_link.insert(QStringLiteral("isActionTarget"), false);
    references.append(status_link);
    QJsonObject action_link;
    action_link.insert(QStringLiteral("assetId"), 10);
    action_link.insert(QStringLiteral("isActionTarget"), true);
    references.append(action_link);

    QJsonObject raw_rule;
    raw_rule.insert(
        QStringLiteral("template"),
        QStringLiteral(
            "RULE R_RAW\n"
            "IF NODE {{0}} PRESSURE BELOW 20\n"
            "AND SYSTEM TIME >= 1:00\n"
            "OR LINK {{1}} STATUS IS CLOSED\n"
            "THEN LINK {{2}} STATUS = CLOSED\n"
            "ELSE LINK {{2}} STATUS = OPEN\n"
            "PRIORITY 2.5"));
    raw_rule.insert(QStringLiteral("assetReferences"), references);
    QJsonArray raw_rules;
    raw_rules.append(raw_rule);
    QJsonObject raw;
    raw.insert(QStringLiteral("simple"), QJsonArray());
    raw.insert(QStringLiteral("rules"), raw_rules);
    const QVariantMap raw_row = {
        {QStringLiteral("id"), 1},
        {QStringLiteral("data"), QString::fromUtf8(
            QJsonDocument(raw).toJson(QJsonDocument::Compact))}
    };
    project.tables.insert(
        QStringLiteral("raw_controls"),
        makeTable(
            QStringLiteral("raw_controls"),
            {QStringLiteral("id"), QStringLiteral("data")},
            {raw_row}));

    const EpanetJsProjectConversionResult first = EpanetJsProjectConverter::convert(project);
    const EpanetJsProjectConversionResult second = EpanetJsProjectConverter::convert(project);

    expectTrue(first.success,
               "converter imports epanet-js raw EPANET rules");
    expectTrue(first.network.controls_rules.size() == 1,
               "one epanet-js raw rule becomes one AOWIS rule");
    if (first.network.controls_rules.size() != 1)
        return;

    const HydraulicControlRule &rule = first.network.controls_rules.first();
    expectTrue(rule.id == QStringLiteral("R_RAW"),
               "raw rule id is preserved");
    expectNear(rule.priority, 2.5, 1e-12,
               "raw rule priority is preserved");
    expectTrue(rule.premises.size() == 3,
               "raw IF/AND/OR clauses become three structured premises");
    expectTrue(rule.actions_then.size() == 1,
               "raw THEN clause becomes one structured action");
    expectTrue(rule.actions_else.size() == 1,
               "raw ELSE clause becomes one structured action");

    if (rule.premises.size() == 3)
    {
        const HydraulicControlRulePremise &pressure = rule.premises.at(0);
        const HydraulicControlRulePremise &time = rule.premises.at(1);
        const HydraulicControlRulePremise &status = rule.premises.at(2);
        expectTrue(pressure.logical_operator == HydraulicControlRuleLogicalOperator::If,
                   "first raw rule premise retains IF");
        expectTrue(pressure.object == HydraulicControlRuleObject::Node,
                   "raw node-pressure premise targets a node");
        expectTrue(pressure.variable == HydraulicControlRuleVariable::Pressure,
                   "raw node-pressure premise retains PRESSURE variable");
        expectTrue(pressure.comparison == HydraulicControlRuleOperator::Less,
                   "raw BELOW comparison canonicalizes to less-than");
        expectTrue(pressure.pressure_head_m.has_value(),
                   "raw pressure threshold is stored canonically");
        if (pressure.pressure_head_m.has_value())
            expectNear(pressure.pressure_head_m.value(), 20.0, 1e-12,
                       "mwc raw pressure threshold stays in metres of head");

        expectTrue(time.logical_operator == HydraulicControlRuleLogicalOperator::And,
                   "second raw rule premise retains AND");
        expectTrue(time.object == HydraulicControlRuleObject::System,
                   "raw SYSTEM TIME premise targets system");
        expectTrue(time.variable == HydraulicControlRuleVariable::Time,
                   "raw SYSTEM TIME premise retains TIME variable");
        expectTrue(time.elapsed_time_s.has_value(),
                   "raw SYSTEM TIME threshold is stored in seconds");
        if (time.elapsed_time_s.has_value())
            expectTrue(time.elapsed_time_s.value() == 3600,
                       "raw 1:00 system time becomes 3600 seconds");

        expectTrue(status.logical_operator == HydraulicControlRuleLogicalOperator::Or,
                   "third raw rule premise retains OR");
        expectTrue(status.object == HydraulicControlRuleObject::Link,
                   "raw link-status premise targets a link");
        expectTrue(status.variable == HydraulicControlRuleVariable::Status,
                   "raw link-status premise retains STATUS variable");
        expectTrue(status.comparison == HydraulicControlRuleOperator::Equal,
                   "raw IS comparison canonicalizes to equality");
        expectTrue(status.status.has_value()
                       && status.status.value() == HydraulicControlRuleStatus::Closed,
                   "raw CLOSED status premise is preserved");
    }

    if (!rule.actions_then.isEmpty())
    {
        expectTrue(rule.actions_then.first().status.has_value()
                       && rule.actions_then.first().status.value()
                           == HydraulicControlRuleStatus::Closed,
                   "raw THEN status action is preserved");
    }
    if (!rule.actions_else.isEmpty())
    {
        expectTrue(rule.actions_else.first().status.has_value()
                       && rule.actions_else.first().status.value()
                           == HydraulicControlRuleStatus::Open,
                   "raw ELSE status action is preserved");
    }
    expectTrue(
        second.network.controls_rules.size() == 1
            && rule.uuid == second.network.controls_rules.first().uuid,
        "raw rule UUIDs are deterministic across repeated imports");
}

void testRejectsUnsupportedRawRulePowerPremise()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject reference;
    reference.insert(QStringLiteral("assetId"), 10);
    reference.insert(QStringLiteral("isActionTarget"), false);
    QJsonArray references;
    references.append(reference);
    QJsonObject action_reference;
    action_reference.insert(QStringLiteral("assetId"), 10);
    action_reference.insert(QStringLiteral("isActionTarget"), true);
    references.append(action_reference);

    QJsonObject raw_rule;
    raw_rule.insert(
        QStringLiteral("template"),
        QStringLiteral(
            "RULE R_POWER\n"
            "IF LINK {{0}} POWER > 1\n"
            "THEN LINK {{1}} STATUS = CLOSED"));
    raw_rule.insert(QStringLiteral("assetReferences"), references);
    QJsonArray raw_rules;
    raw_rules.append(raw_rule);
    QJsonObject raw;
    raw.insert(QStringLiteral("simple"), QJsonArray());
    raw.insert(QStringLiteral("rules"), raw_rules);
    const QVariantMap raw_row = {
        {QStringLiteral("id"), 1},
        {QStringLiteral("data"), QString::fromUtf8(
            QJsonDocument(raw).toJson(QJsonDocument::Compact))}
    };
    project.tables.insert(
        QStringLiteral("raw_controls"),
        makeTable(
            QStringLiteral("raw_controls"),
            {QStringLiteral("id"), QStringLiteral("data")},
            {raw_row}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(!result.success,
               "converter blocks raw POWER premises that the current EPANET builder cannot execute");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("unsupported-raw-rule-power-premise")),
               "unsupported POWER premise produces a dedicated diagnostic");
}

void testRejectsBrokenPumpAndValveReferences()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("head"), QStringLiteral("m"));
    units.insert(QStringLiteral("pressure"), QStringLiteral("mwc"));
    units.insert(QStringLiteral("diameter"), QStringLiteral("mm"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("broken-links-test"));
    settings.insert(
        QStringLiteral("uniqueId"),
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    const QVariantMap characteristic_curve = {
        {QStringLiteral("id"), 40},
        {QStringLiteral("label"), QStringLiteral("VALVE")},
        {QStringLiteral("type"), QStringLiteral("valve")},
        {QStringLiteral("points"), QStringLiteral("[[50,50],[100,100]]")}
    };
    project.tables.insert(
        QStringLiteral("curves"),
        makeTable(
            QStringLiteral("curves"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("type"), QStringLiteral("points")},
            {characteristic_curve}));

    const QVariantMap pump = {
        {QStringLiteral("id"), 11},
        {QStringLiteral("label"), QStringLiteral("BROKEN-PUMP")},
        {QStringLiteral("start_node_id"), 1},
        {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("definition_type"), QStringLiteral("customCurve")},
        {QStringLiteral("curve_id"), 999}
    };
    project.tables.insert(
        QStringLiteral("pumps"),
        makeTable(
            QStringLiteral("pumps"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("start_node_id"), QStringLiteral("end_node_id"),
             QStringLiteral("definition_type"), QStringLiteral("curve_id")},
            {pump}));

    const QVariantMap gpv = {
        {QStringLiteral("id"), 12},
        {QStringLiteral("label"), QStringLiteral("BROKEN-GPV")},
        {QStringLiteral("start_node_id"), 1},
        {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("initial_status"), QStringLiteral("active")},
        {QStringLiteral("diameter"), 100.0},
        {QStringLiteral("minor_loss"), 0.0},
        {QStringLiteral("valve_kind"), QStringLiteral("gpv")},
        {QStringLiteral("curve_id"), 40}
    };
    const QVariantMap remote_prv = {
        {QStringLiteral("id"), 13},
        {QStringLiteral("label"), QStringLiteral("REMOTE-PRV")},
        {QStringLiteral("start_node_id"), 1},
        {QStringLiteral("end_node_id"), 2},
        {QStringLiteral("initial_status"), QStringLiteral("active")},
        {QStringLiteral("diameter"), 100.0},
        {QStringLiteral("minor_loss"), 0.0},
        {QStringLiteral("valve_kind"), QStringLiteral("prv")},
        {QStringLiteral("setting"), 20.0},
        {QStringLiteral("target_node_id"), 1}
    };
    project.tables.insert(
        QStringLiteral("valves"),
        makeTable(
            QStringLiteral("valves"),
            {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("start_node_id"), QStringLiteral("end_node_id"),
             QStringLiteral("initial_status"), QStringLiteral("diameter"),
             QStringLiteral("minor_loss"), QStringLiteral("valve_kind"),
             QStringLiteral("setting"), QStringLiteral("curve_id"),
             QStringLiteral("target_node_id")},
            {gpv, remote_prv}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(!result.success,
               "converter rejects pump and valve references that would change hydraulic meaning");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("missing-pump-head-curve-reference")),
               "missing pump head curve is diagnosed");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("missing-gpv-curve-reference")),
               "GPV referencing the wrong curve type is diagnosed");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("unsupported-remote-prv-target")),
               "remote PRV target is rejected until AOWIS can represent it faithfully");
}

void testSkipsMalformedOptionalOperationalData()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fc4ca0a0-da46-42eb-bc97-ad1963817ded"));

    const QVariantMap bad_pattern = {
        {QStringLiteral("id"), 4},
        {QStringLiteral("label"), QStringLiteral("BROKEN-PATTERN")},
        {QStringLiteral("type"), QStringLiteral("demand")},
        {QStringLiteral("multipliers"), QStringLiteral("[1,\"bad\"]")}
    };
    project.tables.insert(
        QStringLiteral("patterns"),
        makeTable(
            QStringLiteral("patterns"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("type"),
             QStringLiteral("multipliers")},
            {bad_pattern}));

    const QVariantMap bad_curve = {
        {QStringLiteral("id"), 5},
        {QStringLiteral("label"), QStringLiteral("BROKEN-CURVE")},
        {QStringLiteral("type"), QStringLiteral("pump")},
        {QStringLiteral("points"), QStringLiteral("not-json")}
    };
    project.tables.insert(
        QStringLiteral("curves"),
        makeTable(
            QStringLiteral("curves"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("type"),
             QStringLiteral("points")},
            {bad_curve}));

    const QVariantMap bad_settings = {
        {QStringLiteral("id"), 1},
        {QStringLiteral("data"), QStringLiteral("not-json")}
    };
    project.tables.insert(
        QStringLiteral("simulation_settings"),
        makeTable(
            QStringLiteral("simulation_settings"),
            {QStringLiteral("id"), QStringLiteral("data")},
            {bad_settings}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);

    expectTrue(result.success,
               "malformed optional operational data does not prevent importing the recognizable hydraulic core");
    expectTrue(result.network.patterns_time.isEmpty(), "malformed pattern is skipped");
    expectTrue(result.network.curves_pump_head.isEmpty(), "malformed curve is skipped");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("invalid-simulation-settings-json")),
               "malformed simulation settings are diagnosed");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("invalid-pattern-multipliers")),
               "malformed pattern multipliers are diagnosed");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("invalid-curve-points")),
               "malformed curve point data is diagnosed");
}
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
    return test_harness.finish();
}
