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
