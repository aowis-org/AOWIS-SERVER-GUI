// Included in epanet_js_project_converter_tests.cpp within its anonymous namespace.
// Test cases share the existing fixtures and test harness.

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
