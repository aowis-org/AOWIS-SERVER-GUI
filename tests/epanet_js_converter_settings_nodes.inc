// Included in epanet_js_project_converter_tests.cpp within its anonymous namespace.
// Test cases share the existing fixtures and test harness.

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
    expectNear(result.network.options_hydraulic.minimum_pressure_head_m, 5.858913163660, 1e-9,
               "minimum psi pressure is converted to metres of head using specific gravity");
    expectNear(result.network.options_hydraulic.required_pressure_head_m, 17.576739490979, 1e-9,
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
        expectNear(junction.emitter.coefficient, 0.626042964135, 1e-12,
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
