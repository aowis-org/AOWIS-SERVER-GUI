// Regression tests for the zero-minimum-volume and global reaction import fixes.
void testTankZeroMinimumVolumeIncludesBottomWaterColumn()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("9acffdb2-a624-40e9-9964-f7cdb0dff911"));
    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("elevation"), QStringLiteral("m"));
    units.insert(QStringLiteral("level"), QStringLiteral("m"));
    units.insert(QStringLiteral("tankDiameter"), QStringLiteral("m"));
    units.insert(QStringLiteral("minVolume"), QStringLiteral("m^3"));
    units.insert(QStringLiteral("volume"), QStringLiteral("m^3"));
    QJsonObject settings;
    settings.insert(QStringLiteral("uniqueId"), QStringLiteral("9acffdb2-a624-40e9-9964-f7cdb0dff911"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    const QVariantMap tank = {
        {QStringLiteral("id"), 4},
        {QStringLiteral("label"), QStringLiteral("T-ZERO")},
        {QStringLiteral("coord_x"), 18.4},
        {QStringLiteral("coord_y"), 11.5},
        {QStringLiteral("elevation"), 70.0},
        {QStringLiteral("initial_level"), 3.0},
        {QStringLiteral("min_level"), 1.0},
        {QStringLiteral("max_level"), 5.0},
        {QStringLiteral("min_volume"), 0.0},
        {QStringLiteral("diameter"), 10.0}
    };
    project.tables.insert(QStringLiteral("tanks"), makeTable(
        QStringLiteral("tanks"),
        {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("coord_x"),
         QStringLiteral("coord_y"), QStringLiteral("elevation"),
         QStringLiteral("initial_level"), QStringLiteral("min_level"),
         QStringLiteral("max_level"), QStringLiteral("min_volume"),
         QStringLiteral("diameter")}, {tank}));

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);
    expectTrue(result.success, "zero-minimum-volume tank import succeeds");
    expectTrue(result.network.nodes_tanks.size() == 1, "zero-minimum-volume tank imported");
    if (result.network.nodes_tanks.size() == 1)
    {
        const HydraulicNodeTank &imported = result.network.nodes_tanks.first();
        const double area = 25.0 * std::acos(-1.0);
        expectNear(imported.minimum_volume_m3, 0.0, 1e-12,
                   "explicit zero minimum volume is retained");
        expectNear(imported.volume_at_maximum_level_m3, area * 5.0, 1e-9,
                   "maximum volume includes water below minimum level");
    }
}

void testGlobalFirstOrderReactionsSurviveUnrecognizedChemicalUnit()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("bfaea4fd-cb44-475d-bac7-42c19bdb03af"));
    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("chemicalConcentration"), QStringLiteral("unrecognized-unit"));
    QJsonObject settings;
    settings.insert(QStringLiteral("uniqueId"), QStringLiteral("bfaea4fd-cb44-475d-bac7-42c19bdb03af"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);
    QJsonObject simulation;
    simulation.insert(QStringLiteral("reactionBulkOrder"), 1.0);
    simulation.insert(QStringLiteral("reactionTankOrder"), 1.0);
    simulation.insert(QStringLiteral("reactionWallOrder"), 1.0);
    simulation.insert(QStringLiteral("reactionGlobalBulk"), -0.25);
    simulation.insert(QStringLiteral("reactionGlobalWall"), -0.5);
    simulation.insert(QStringLiteral("reactionRoughnessCorrelation"), 0.75);
    setSimulationSettings(project, simulation);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);
    expectTrue(result.success, "unknown concentration unit does not abort first-order reaction import");
    expectNear(result.network.options_reaction.global_pipe_bulk_reaction.coefficient, -0.25, 1e-12,
               "global first-order pipe bulk coefficient survives unknown concentration unit");
    expectNear(result.network.options_reaction.global_tank_bulk_reaction.coefficient, -0.25, 1e-12,
               "global first-order tank bulk coefficient survives unknown concentration unit");
    expectNear(result.network.options_reaction.global_pipe_wall_reaction.coefficient, -0.5, 1e-12,
               "global nonzero-order wall coefficient survives unknown concentration unit");
    expectNear(result.network.options_reaction.roughness_reaction_factor, 0.75, 1e-12,
               "global roughness correlation survives unknown concentration unit");
}

void testGlobalReactionsWithGreekMuConcentrationUnit()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("fd70222c-4e30-46e5-88b8-26bffcc7c5e0"));
    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("chemicalConcentration"), QString::fromUtf8("μg/L"));
    QJsonObject settings;
    settings.insert(QStringLiteral("uniqueId"), QStringLiteral("fd70222c-4e30-46e5-88b8-26bffcc7c5e0"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);
    QJsonObject simulation;
    simulation.insert(QStringLiteral("reactionBulkOrder"), 2.0);
    simulation.insert(QStringLiteral("reactionTankOrder"), 2.0);
    simulation.insert(QStringLiteral("reactionWallOrder"), 0.0);
    simulation.insert(QStringLiteral("reactionGlobalBulk"), -0.2);
    simulation.insert(QStringLiteral("reactionGlobalWall"), -0.1);
    setSimulationSettings(project, simulation);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);
    expectTrue(result.success, "Greek-mu chemical unit project import succeeds");
    expectNear(result.network.options_reaction.global_pipe_bulk_reaction.coefficient, -200.0, 1e-9,
               "second-order global bulk coefficient converts Greek-mu concentration scale");
    expectNear(result.network.options_reaction.global_tank_bulk_reaction.coefficient, -200.0, 1e-9,
               "second-order global tank coefficient converts Greek-mu concentration scale");
    expectNear(result.network.options_reaction.global_pipe_wall_reaction.coefficient, -0.0001, 1e-12,
               "zero-order global wall coefficient converts Greek-mu concentration scale");
}

// Regression tests for unit inference, wall-order validation, solver-tolerance
// severity, and the shared water-age conversion.
void testInfersMissingVolumeUnitForTanks()
{
    // Metric project without any volume unit: the stored min_volume 0.0 must
    // still convert, because epanet-js writes 0.0 rather than NULL.
    {
        EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
            QStringLiteral("0b1d6c1e-5d3c-4d0e-9f53-0f5a8a1e7c21"));
        QJsonObject units;
        units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
        units.insert(QStringLiteral("elevation"), QStringLiteral("m"));
        QJsonObject settings;
        settings.insert(QStringLiteral("uniqueId"), QStringLiteral("0b1d6c1e-5d3c-4d0e-9f53-0f5a8a1e7c21"));
        settings.insert(QStringLiteral("units"), units);
        setProjectSettings(project, settings);

        const QVariantMap tank = {
            {QStringLiteral("id"), 4},
            {QStringLiteral("label"), QStringLiteral("T-METRIC")},
            {QStringLiteral("coord_x"), 18.4},
            {QStringLiteral("coord_y"), 11.5},
            {QStringLiteral("elevation"), 70.0},
            {QStringLiteral("initial_level"), 3.0},
            {QStringLiteral("min_level"), 1.0},
            {QStringLiteral("max_level"), 5.0},
            {QStringLiteral("min_volume"), 0.0},
            {QStringLiteral("diameter"), 10.0}
        };
        project.tables.insert(QStringLiteral("tanks"), makeTable(
            QStringLiteral("tanks"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("coord_x"),
             QStringLiteral("coord_y"), QStringLiteral("elevation"),
             QStringLiteral("initial_level"), QStringLiteral("min_level"),
             QStringLiteral("max_level"), QStringLiteral("min_volume"),
             QStringLiteral("diameter")}, {tank}));

        const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);
        expectTrue(result.success, "metric tank imports without an explicit volume unit");
        expectTrue(hasDiagnosticCode(result, QStringLiteral("inferred-unit")),
                   "missing metric volume unit is reported as inferred");
        expectTrue(result.network.nodes_tanks.size() == 1, "metric tank without volume unit is imported");
        if (result.network.nodes_tanks.size() == 1)
            expectNear(result.network.nodes_tanks.first().minimum_volume_m3, 0.0, 1e-12,
                       "zero minimum volume converts with the inferred metric volume unit");
    }

    // US project without any volume unit: min_volume follows EPANET's ft^3.
    {
        EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
            QStringLiteral("6f2e9c43-8a1b-4c7d-b2e5-3d9f0a6b4e18"));
        QJsonObject units;
        units.insert(QStringLiteral("flow"), QStringLiteral("gpm"));
        units.insert(QStringLiteral("elevation"), QStringLiteral("ft"));
        QJsonObject settings;
        settings.insert(QStringLiteral("uniqueId"), QStringLiteral("6f2e9c43-8a1b-4c7d-b2e5-3d9f0a6b4e18"));
        settings.insert(QStringLiteral("units"), units);
        setProjectSettings(project, settings);

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
        project.tables.insert(QStringLiteral("tanks"), makeTable(
            QStringLiteral("tanks"),
            {QStringLiteral("id"), QStringLiteral("label"), QStringLiteral("coord_x"),
             QStringLiteral("coord_y"), QStringLiteral("elevation"),
             QStringLiteral("initial_level"), QStringLiteral("min_level"),
             QStringLiteral("max_level"), QStringLiteral("min_volume"),
             QStringLiteral("diameter")}, {tank}));

        const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);
        expectTrue(result.success, "US tank imports without an explicit volume unit");
        expectTrue(result.network.nodes_tanks.size() == 1, "US tank without volume unit is imported");
        if (result.network.nodes_tanks.size() == 1)
        {
            const double cubic_foot_m3 = 0.3048 * 0.3048 * 0.3048;
            expectNear(result.network.nodes_tanks.first().minimum_volume_m3,
                       100.0 * cubic_foot_m3, 1e-12,
                       "US minimum volume converts with the inferred ft^3 volume unit");
            expectNear(result.network.nodes_tanks.first().diameter_m, 20.0 * 0.3048, 1e-12,
                       "US tank diameter converts with the inferred ft length unit");
        }
    }
}

void testRejectsUnsupportedWallReactionOrder()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("c3a7d5e1-2f4b-4a6c-8e9d-1b0f2a3c4d5e"));
    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    units.insert(QStringLiteral("chemicalConcentration"), QStringLiteral("mg/L"));
    QJsonObject settings;
    settings.insert(QStringLiteral("uniqueId"), QStringLiteral("c3a7d5e1-2f4b-4a6c-8e9d-1b0f2a3c4d5e"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);
    QJsonObject simulation;
    simulation.insert(QStringLiteral("reactionWallOrder"), 0.5);
    simulation.insert(QStringLiteral("reactionGlobalWall"), -0.1);
    setSimulationSettings(project, simulation);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);
    expectTrue(!result.success, "wall reaction order other than 0 or 1 is rejected");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("invalid-wall-reaction-order")),
               "invalid wall reaction order has a dedicated diagnostic");
    expectNear(result.network.options_reaction.global_pipe_wall_reaction.order, 1.0, 1e-12,
               "invalid wall reaction order is not stored");

    for (const double valid_order : {0.0, 1.0})
    {
        EpanetJsProjectSnapshot valid_project = makeProjectWithUniqueId(
            QStringLiteral("c3a7d5e1-2f4b-4a6c-8e9d-1b0f2a3c4d5e"));
        setProjectSettings(valid_project, settings);
        QJsonObject valid_simulation;
        valid_simulation.insert(QStringLiteral("reactionWallOrder"), valid_order);
        setSimulationSettings(valid_project, valid_simulation);
        const EpanetJsProjectConversionResult valid_result =
            EpanetJsProjectConverter::convert(valid_project);
        expectTrue(valid_result.success, "wall reaction order 0 and 1 are accepted");
        expectNear(valid_result.network.options_reaction.global_pipe_wall_reaction.order,
                   valid_order, 1e-12, "valid wall reaction order is imported");
    }
}

void testUnconvertibleSolverTolerancesKeepDefaults()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("5e8b2a91-7c3d-4f6e-a1b2-9d8c7e6f5a40"));
    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("furlong^3/fortnight"));
    units.insert(QStringLiteral("head"), QStringLiteral("furlong"));
    QJsonObject settings;
    settings.insert(QStringLiteral("uniqueId"), QStringLiteral("5e8b2a91-7c3d-4f6e-a1b2-9d8c7e6f5a40"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);
    QJsonObject simulation;
    simulation.insert(QStringLiteral("maximumHeadError"), 2.5);
    simulation.insert(QStringLiteral("maximumFlowChange"), 10.0);
    setSimulationSettings(project, simulation);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);
    const HydraulicSolverOptions defaults{};
    expectTrue(result.success, "unconvertible solver tolerances do not abort the import");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("unsupported-head-error-unit")),
               "unconvertible maximum head error is reported");
    expectTrue(hasDiagnosticCode(result, QStringLiteral("unsupported-flow-change-unit")),
               "unconvertible maximum flow change is reported");
    expectNear(result.network.options_hydraulic.maximum_head_error_m,
               defaults.maximum_head_error_m, 1e-12,
               "unconvertible maximum head error keeps the AOWIS default");
    expectNear(result.network.options_hydraulic.maximum_flow_change_m3_per_h,
               defaults.maximum_flow_change_m3_per_h, 1e-12,
               "unconvertible maximum flow change keeps the AOWIS default");
}

void testWaterAgeToleranceUsesSharedConversion()
{
    const auto check = [](const std::optional<double> &actual, double expected, const char *message) {
        expectTrue(actual.has_value(), message);
        if (actual.has_value())
            expectNear(*actual, expected, 1e-12, message);
    };
    check(EpanetJsUnits::waterAgeToHours(1.5, QString()), 1.5, "empty water-age unit means hours");
    check(EpanetJsUnits::waterAgeToHours(90.0, QStringLiteral("min")), 1.5, "minutes convert to hours");
    check(EpanetJsUnits::waterAgeToHours(5400.0, QStringLiteral("s")), 1.5, "seconds convert to hours");
    check(EpanetJsUnits::waterAgeToHours(2.0, QStringLiteral("d")), 48.0, "days convert to hours");
    expectTrue(!EpanetJsUnits::waterAgeToHours(1.0, QStringLiteral("fortnight")).has_value(),
               "unknown water-age unit is rejected");

    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("9d4c2b1a-0e8f-4a7b-b6c5-d4e3f2a1b0c9"));
    QJsonObject units;
    units.insert(QStringLiteral("waterAge"), QStringLiteral("min"));
    QJsonObject settings;
    settings.insert(QStringLiteral("uniqueId"), QStringLiteral("9d4c2b1a-0e8f-4a7b-b6c5-d4e3f2a1b0c9"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);
    QJsonObject simulation;
    simulation.insert(QStringLiteral("qualitySimulationType"), QStringLiteral("age"));
    simulation.insert(QStringLiteral("tolerance"), 30.0);
    setSimulationSettings(project, simulation);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);
    expectTrue(result.success, "water-age tolerance in minutes imports");
    expectTrue(result.quality_runs.size() == 1, "water-age run is created");
    if (result.quality_runs.size() == 1)
        expectNear(result.quality_runs.first().water_age_tolerance_h, 0.5, 1e-12,
                   "water-age tolerance minutes convert to canonical hours");

    units.insert(QStringLiteral("waterAge"), QStringLiteral("fortnight"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);
    const EpanetJsProjectConversionResult rejected = EpanetJsProjectConverter::convert(project);
    expectTrue(!rejected.success, "unknown water-age tolerance unit is rejected");
    expectTrue(hasDiagnosticCode(rejected, QStringLiteral("unsupported-water-age-unit")),
               "unknown water-age tolerance unit has a dedicated diagnostic");
    expectTrue(rejected.quality_runs.isEmpty(),
               "unknown water-age tolerance unit does not create a partial quality run");
}

// Regression tests for epanet-js unit semantics verified against the epanet-js
// sources (libs/quantity, libs/project-settings, apps/app/src/simulation/
// build-inp.ts) and EPANET 2.3 (src/input1.c initunits/convertunits).
EpanetJsProjectSnapshot makePressureSemanticsProject(
    const QString &unique_id,
    const QString &flow_unit,
    const QString &pressure_unit,
    double required_pressure)
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(unique_id);
    QJsonObject units;
    units.insert(QStringLiteral("flow"), flow_unit);
    units.insert(QStringLiteral("pressure"), pressure_unit);
    QJsonObject settings;
    settings.insert(QStringLiteral("uniqueId"), unique_id);
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    QJsonObject simulation;
    simulation.insert(QStringLiteral("specificGravity"), 1.2);
    simulation.insert(QStringLiteral("emitterExponent"), 0.5);
    simulation.insert(QStringLiteral("demandModel"), QStringLiteral("PDA"));
    simulation.insert(QStringLiteral("minimumPressure"), 0.0);
    simulation.insert(QStringLiteral("requiredPressure"), required_pressure);
    setSimulationSettings(project, simulation);

    EpanetJsTableSnapshot junctions = project.tables.value(QStringLiteral("junctions"));
    junctions.columns.append(QStringLiteral("emitter_coefficient"));
    junctions.rows[0].insert(QStringLiteral("emitter_coefficient"), 2.0);
    project.tables.insert(QStringLiteral("junctions"), junctions);
    return project;
}

void testEpanetJsPressureAndEmitterSemantics()
{
    const double us_gallon_m3 = 231.0 * 0.0254 * 0.0254 * 0.0254;
    const double fluid_density = 1200.0; // specific gravity 1.2
    const double standard_gravity = 9.80665;
    const double psi_head_m = 6894.757293168361 / (fluid_density * standard_gravity);

    // SI with kPa display pressure: PDA pressure follows kPa and specific
    // gravity, but the emitter coefficient stays per metre of head.
    {
        const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(
            makePressureSemanticsProject(
                QStringLiteral("2f6c1d3e-4a5b-4c7d-8e9f-0a1b2c3d4e51"),
                QStringLiteral("l/s"), QStringLiteral("kPa"), 100.0));
        expectTrue(result.success, "SI project with kPa pressure imports");
        expectNear(result.network.options_hydraulic.required_pressure_head_m,
                   100000.0 / (fluid_density * standard_gravity), 1e-12,
                   "kPa required pressure uses specific gravity");
        expectTrue(!result.network.nodes_junctions.isEmpty(), "SI emitter junction imported");
        if (!result.network.nodes_junctions.isEmpty())
            expectNear(result.network.nodes_junctions.first().emitter.coefficient, 2.0 * 3.6, 1e-12,
                       "SI emitter coefficient ignores the kPa display pressure unit");
    }

    // SI with mwc: EPANET 2.3 PRESSURE METERS is fluid head.
    {
        const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(
            makePressureSemanticsProject(
                QStringLiteral("2f6c1d3e-4a5b-4c7d-8e9f-0a1b2c3d4e52"),
                QStringLiteral("l/s"), QStringLiteral("mwc"), 20.0));
        expectTrue(result.success, "SI project with mwc pressure imports");
        expectNear(result.network.options_hydraulic.required_pressure_head_m, 20.0, 1e-12,
                   "mwc required pressure is fluid head without specific gravity");
    }

    // US with fwc display pressure: PDA pressure is fluid head in feet, while
    // the emitter coefficient stays per psi with specific gravity.
    {
        const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(
            makePressureSemanticsProject(
                QStringLiteral("2f6c1d3e-4a5b-4c7d-8e9f-0a1b2c3d4e53"),
                QStringLiteral("gal/min"), QStringLiteral("fwc"), 10.0));
        expectTrue(result.success, "US project with fwc pressure imports");
        expectNear(result.network.options_hydraulic.required_pressure_head_m, 3.048, 1e-12,
                   "fwc required pressure is fluid head without specific gravity");
        expectTrue(!result.network.nodes_junctions.isEmpty(), "US emitter junction imported");
        if (!result.network.nodes_junctions.isEmpty())
            expectNear(result.network.nodes_junctions.first().emitter.coefficient,
                       2.0 * us_gallon_m3 * 60.0 / std::sqrt(psi_head_m), 1e-12,
                       "US emitter coefficient stays per psi despite fwc display pressure");
    }
}

void testImportsImperialAndAcreFootFlowPresets()
{
    const double imperial_gallon_m3 = 0.00454609;
    const double cubic_foot_m3 = 0.3048 * 0.3048 * 0.3048;

    struct PresetCase
    {
        const char *unique_id;
        const char *flow_unit;
        double cubic_metres_per_hour;
    };
    const PresetCase cases[] = {
        {"7a1e0c52-3b4d-4e6f-8a9b-1c2d3e4f5a61", "IMgal/d", imperial_gallon_m3 * 1.0e6 / 24.0},
        {"7a1e0c52-3b4d-4e6f-8a9b-1c2d3e4f5a62", "acft/d", 43560.0 * cubic_foot_m3 / 24.0},
    };
    for (const PresetCase &preset : cases)
    {
        const QString unique_id = QString::fromUtf8(preset.unique_id);
        EpanetJsProjectSnapshot project = makeProjectWithUniqueId(unique_id);
        QJsonObject units;
        units.insert(QStringLiteral("flow"), QString::fromUtf8(preset.flow_unit));
        QJsonObject settings;
        settings.insert(QStringLiteral("uniqueId"), unique_id);
        settings.insert(QStringLiteral("units"), units);
        setProjectSettings(project, settings);

        const QVariantMap demand = {
            {QStringLiteral("junction_id"), 1},
            {QStringLiteral("ordinal"), 0},
            {QStringLiteral("base_demand"), 1.0}
        };
        project.tables.insert(
            QStringLiteral("junction_demands"),
            makeTable(
                QStringLiteral("junction_demands"),
                {QStringLiteral("junction_id"), QStringLiteral("ordinal"),
                 QStringLiteral("base_demand")},
                {demand}));

        EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
        pipes.columns.append(QStringLiteral("length"));
        pipes.rows[0].insert(QStringLiteral("length"), 100.0);
        project.tables.insert(QStringLiteral("pipes"), pipes);

        const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);
        expectTrue(result.success, preset.flow_unit);
        expectTrue(!result.network.nodes_junctions.isEmpty()
                       && result.network.nodes_junctions.first().demands.size() == 1,
                   preset.flow_unit);
        if (!result.network.nodes_junctions.isEmpty()
            && result.network.nodes_junctions.first().demands.size() == 1)
            expectNear(result.network.nodes_junctions.first().demands.first().base_demand_m3_per_h,
                       preset.cubic_metres_per_hour, 1e-10 * preset.cubic_metres_per_hour,
                       preset.flow_unit);
        expectTrue(result.network.links_pipes.size() == 1, preset.flow_unit);
        if (result.network.links_pipes.size() == 1)
            expectNear(result.network.links_pipes.first().length_measured_m.value_or(-1.0),
                       30.48, 1e-12, "US flow preset implies feet for pipe length");
    }
}

void testQualityMassUnitTakesPrecedenceOverStaleConcentrationUnit()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("e4d3c2b1-a0f9-4e8d-9c7b-6a5f4e3d2c1b"));
    QJsonObject units;
    units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    // epanet-js copies this only when the project is created.
    units.insert(QStringLiteral("chemicalConcentration"), QStringLiteral("mg/L"));
    QJsonObject settings;
    settings.insert(QStringLiteral("uniqueId"), QStringLiteral("e4d3c2b1-a0f9-4e8d-9c7b-6a5f4e3d2c1b"));
    settings.insert(QStringLiteral("units"), units);
    setProjectSettings(project, settings);

    QJsonObject simulation;
    simulation.insert(QStringLiteral("qualitySimulationType"), QStringLiteral("chemical"));
    simulation.insert(QStringLiteral("qualityChemicalName"), QStringLiteral("CL2"));
    simulation.insert(QStringLiteral("qualityMassUnit"), QStringLiteral("ug/L"));
    simulation.insert(QStringLiteral("tolerance"), 25.0);
    setSimulationSettings(project, simulation);

    EpanetJsTableSnapshot junctions = project.tables.value(QStringLiteral("junctions"));
    junctions.columns.append(QStringLiteral("initial_quality"));
    junctions.rows[0].insert(QStringLiteral("initial_quality"), 25.0);
    project.tables.insert(QStringLiteral("junctions"), junctions);

    const EpanetJsProjectConversionResult result = EpanetJsProjectConverter::convert(project);
    expectTrue(result.success, "chemical project with changed mass unit imports");
    expectTrue(result.quality_runs.size() == 1, "chemical quality run is created");
    if (result.quality_runs.size() == 1)
        expectNear(result.quality_runs.first().chemical_tolerance_mg_per_l, 0.025, 1e-12,
                   "quality tolerance follows qualityMassUnit, not the stale display unit");
    if (!result.network.nodes_junctions.isEmpty())
        expectNear(result.network.nodes_junctions.first().initial_chemical_concentration_mg_per_l,
                   0.025, 1e-12,
                   "initial quality follows qualityMassUnit, not the stale display unit");
}
