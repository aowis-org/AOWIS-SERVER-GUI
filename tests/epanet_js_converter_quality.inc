// Included in epanet_js_project_converter_tests.cpp within its anonymous namespace.
// Test cases share the existing fixtures and test harness.

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
