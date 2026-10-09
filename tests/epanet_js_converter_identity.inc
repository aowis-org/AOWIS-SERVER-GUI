// Included in epanet_js_project_converter_tests.cpp within its anonymous namespace.
// Test cases share the existing fixtures and test harness.

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
