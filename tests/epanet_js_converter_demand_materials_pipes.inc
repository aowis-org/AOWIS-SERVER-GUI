// Included in epanet_js_project_converter_tests.cpp within its anonymous namespace.
// Test cases share the existing fixtures and test harness.

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
