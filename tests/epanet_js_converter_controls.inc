// Included in epanet_js_project_converter_tests.cpp within its anonymous namespace.
// Test cases share the existing fixtures and test harness.

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
