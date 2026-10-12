// Export filename validation must not confuse EPANET INP text with the
// epanet-js SQLite database format accepted by the import operation.
void testOnlyInpExportPathsAreAccepted()
{
    expectTrue(EpanetNetworkFileFormat::acceptsInpExportPath(
                   QStringLiteral("network.inp")),
               "EPANET text can be exported as an INP file");
    expectTrue(EpanetNetworkFileFormat::acceptsInpExportPath(
                   QStringLiteral("/tmp/model.INP")),
               "INP filename extensions are case insensitive");
    expectTrue(!EpanetNetworkFileFormat::acceptsInpExportPath(
                   QStringLiteral("network.ejsdb")),
               "SQLite epanet-js files are not supported export targets");
    expectTrue(!EpanetNetworkFileFormat::acceptsInpExportPath(
                   QStringLiteral("network.inp.ejsdb")),
               "INP inside the basename does not imply the export format");
    expectTrue(!EpanetNetworkFileFormat::acceptsInpExportPath(
                   QStringLiteral("network")),
               "an export file must have the .inp extension");
    expectTrue(!EpanetNetworkFileFormat::acceptsInpExportPath(QString()),
               "an empty export path is invalid");
}

// epanet-js source unit settings are resolved by the importer. Generating a
// candidate GUI unit profile is only a presentation decision after conversion
// and must not mutate the source settings or imported canonical values.
void testImportedUnitProfileIsOnlyPresentation()
{
    EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
        QStringLiteral("f4c69462-a1fd-4dc6-816f-7a6a1d5c8105"));
    QJsonObject source_units;
    source_units.insert(QStringLiteral("flow"), QStringLiteral("gpm"));
    source_units.insert(QStringLiteral("elevation"), QStringLiteral("ft"));
    source_units.insert(QStringLiteral("head"), QStringLiteral("ft"));
    source_units.insert(QStringLiteral("length"), QStringLiteral("ft"));
    source_units.insert(QStringLiteral("diameter"), QStringLiteral("in"));
    source_units.insert(QStringLiteral("pressure"), QStringLiteral("psi"));

    QJsonObject settings;
    settings.insert(QStringLiteral("name"), QStringLiteral("source-unit-isolation"));
    settings.insert(QStringLiteral("uniqueId"),
                    QStringLiteral("f4c69462-a1fd-4dc6-816f-7a6a1d5c8105"));
    settings.insert(QStringLiteral("units"), source_units);
    setProjectSettings(project, settings);

    EpanetJsTableSnapshot junctions = project.tables.value(QStringLiteral("junctions"));
    junctions.columns.append(QStringLiteral("elevation"));
    junctions.rows[0].insert(QStringLiteral("elevation"), 100.0);
    project.tables.insert(QStringLiteral("junctions"), junctions);

    // Give the same source project three independent measured properties.
    // Unit-profile creation must never change any of these canonical values.
    EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
    pipes.columns.append(QStringLiteral("diameter"));
    pipes.rows[0].insert(QStringLiteral("diameter"), 8.0);
    project.tables.insert(QStringLiteral("pipes"), pipes);
    const QVariantMap demand = {
        {QStringLiteral("junction_id"), 1},
        {QStringLiteral("ordinal"), 0},
        {QStringLiteral("base_demand"), 22.0}
    };
    project.tables.insert(
        QStringLiteral("junction_demands"),
        makeTable(QStringLiteral("junction_demands"),
                  {QStringLiteral("junction_id"), QStringLiteral("ordinal"),
                   QStringLiteral("base_demand")}, {demand}));

    const EpanetJsProjectConversionResult before = EpanetJsProjectConverter::convert(project);
    expectTrue(before.success, "epanet-js source units are independently importable");
    if (!before.success || before.network.nodes_junctions.isEmpty())
        return;
    const double canonical_elevation = before.network.nodes_junctions.first().elevation_m;
    expectNear(canonical_elevation, 30.48, 1e-12,
               "source elevation is converted from feet, regardless of GUI units");
    expectTrue(!before.network.links_pipes.isEmpty(), "source pipe imports");
    expectTrue(!before.network.nodes_junctions.first().demands.isEmpty(),
               "source junction demand imports");
    if (before.network.links_pipes.isEmpty()
        || before.network.nodes_junctions.first().demands.isEmpty())
        return;
    const double canonical_diameter = before.network.links_pipes.first().diameter_mm;
    const double canonical_demand =
        before.network.nodes_junctions.first().demands.first().base_demand_m3_per_h;
    expectNear(canonical_diameter, 203.2, 1e-12,
               "source pipe diameter inches convert to canonical millimetres");
    expectNear(canonical_demand, 22.0 * 0.22712470704, 1e-12,
               "source junction flow GPM converts to canonical m3/h");

    QStringList unmapped;
    const QJsonObject imported_profile = EpanetJsUnitProfile::selections(
        before.source_display_units, &unmapped);
    expectTrue(imported_profile.value(QStringLiteral("elevation")).toString()
                   == QStringLiteral("ft"),
               "importer proposes the actual source elevation unit for its GUI profile");
    expectTrue(imported_profile.value(QStringLiteral("volumetric_flow_rate")).toString()
                   == QStringLiteral("US gal/min"),
               "importer proposes the actual source flow unit for its GUI profile");
    expectTrue(unmapped.isEmpty(), "all supported source units map to AOWIS units");

    UnitPresentationContext presentation;
    presentation.initialize(imported_profile);
    expectTrue(presentation.unit(QStringLiteral("elevation")) == QStringLiteral("ft"),
               "source-derived display preference uses feet");
    expectTrue(presentation.unit(QStringLiteral("link_diameter")) == QStringLiteral("in"),
               "source-derived display preference uses inches");
    const QJsonObject metric_display{
        {QStringLiteral("elevation"), QStringLiteral("m")},
        {QStringLiteral("link_diameter"), QStringLiteral("mm")},
        {QStringLiteral("volumetric_flow_rate"), QStringLiteral("m3/h")}
    };
    expectTrue(presentation.update(metric_display),
               "switching to metric presentation changes presentation state");
    const EpanetJsProjectConversionResult metric = EpanetJsProjectConverter::convert(project);
    expectTrue(metric.success, "source project reimports under metric presentation");
    if (!metric.success || metric.network.nodes_junctions.isEmpty()
        || metric.network.links_pipes.isEmpty()
        || metric.network.nodes_junctions.first().demands.isEmpty())
        return;
    expectNear(metric.network.nodes_junctions.first().elevation_m,
               canonical_elevation, 0.0, "metric GUI profile leaves canonical elevation unchanged");
    expectNear(metric.network.links_pipes.first().diameter_mm,
               canonical_diameter, 0.0, "metric GUI profile leaves canonical diameter unchanged");
    expectNear(metric.network.nodes_junctions.first().demands.first().base_demand_m3_per_h,
               canonical_demand, 0.0, "metric GUI profile leaves canonical demand unchanged");

    expectTrue(presentation.update(imported_profile),
               "switching back to source display units changes presentation state");
    const EpanetJsProjectConversionResult after = EpanetJsProjectConverter::convert(project);
    expectTrue(after.success, "profile extraction does not invalidate source import");
    if (!after.success || after.network.nodes_junctions.isEmpty())
        return;
    expectNear(after.network.nodes_junctions.first().elevation_m,
               canonical_elevation, 0.0,
               "creating a presentation profile cannot alter canonical elevation");
    expectTrue(!after.network.links_pipes.isEmpty()
                   && !after.network.nodes_junctions.first().demands.isEmpty(),
               "source-derived profile retains the complete canonical network");
    if (!after.network.links_pipes.isEmpty()
        && !after.network.nodes_junctions.first().demands.isEmpty())
    {
        expectNear(after.network.links_pipes.first().diameter_mm,
                   canonical_diameter, 0.0,
                   "source-derived profile cannot alter canonical pipe diameter");
        expectNear(after.network.nodes_junctions.first().demands.first().base_demand_m3_per_h,
                   canonical_demand, 0.0,
                   "source-derived profile cannot alter canonical junction demand");
    }
    expectTrue(before.source_display_units == after.source_display_units,
               "presentation mapping does not mutate source unit metadata");
}

// Profile creation must accept the same ten EPANET-JS flow presets as the
// numerical importer. Unit-profile mapping is a display-only operation.
void testEpanetJsProfileSupportsSourceUnitPresets()
{
    struct FlowCase {
        const char *source;
        const char *display;
    };
    const FlowCase flow_cases[] = {
        {"l/s", "L/s"},
        {"l/min", "L/min"},
        {"Ml/d", "ML/d"},
        {"m^3/h", "m3/h"},
        {"m^3/d", "m3/d"},
        {"gal/min", "US gal/min"},
        {"ft^3/s", "ft3/s"},
        {"Mgal/d", "MUSgal/d"},
        {"IMgal/d", "MImpgal/d"},
        {"acft/d", "acre.ft/d"},
        {"ML/d", "ML/d"},
        {"MGal/d", "MUSgal/d"},
        {"mgd", "MUSgal/d"},
        {"imgd", "MImpgal/d"},
        {"cfs", "ft3/s"},
        {"gpm", "US gal/min"},
        {"afd", "acre.ft/d"},
        {"m3/s", "m3/s"},
        {"cmh", "m3/h"},
        {"cms", "m3/s"},
    };
    for (const FlowCase &flow_case : flow_cases)
    {
        QJsonObject source;
        source.insert(QStringLiteral("flow"), QString::fromLatin1(flow_case.source));
        QStringList unmapped;
        const QJsonObject profile = EpanetJsUnitProfile::selections(source, &unmapped);
        const QString selection = profile.value(QStringLiteral("volumetric_flow_rate")).toString();
        expectTrue(selection == QString::fromLatin1(flow_case.display), flow_case.source);
        expectTrue(unmapped.isEmpty(), flow_case.source);
        expectTrue(EpanetJsUnits::flowToM3PerH(1.0, QString::fromLatin1(flow_case.source)).has_value(),
                   flow_case.source);
    }

    // A lowercase metric prefix must never silently turn millilitres/day
    // into megalitres/day when generating a custom presentation profile.
    for (const char *unsupported_flow : {"ml/d", "mgal/d"})
    {
        QJsonObject source;
        source.insert(QStringLiteral("flow"), QString::fromLatin1(unsupported_flow));
        QStringList unmapped;
        const QJsonObject profile = EpanetJsUnitProfile::selections(source, &unmapped);
        expectTrue(!profile.contains(QStringLiteral("volumetric_flow_rate")), unsupported_flow);
        expectTrue(!unmapped.isEmpty(), unsupported_flow);
        expectTrue(!EpanetJsUnits::flowToM3PerH(
                       1.0, QString::fromLatin1(unsupported_flow)).has_value(),
                   unsupported_flow);
    }

    struct PressureCase {
        const char *source;
        const char *quantity;
        const char *display;
    };
    const PressureCase pressure_cases[] = {
        {"psi", "pressure_head", "psi"},
        {"kPa", "pressure_head", "kPa"},
        {"bar", "pressure_head", "bar"},
        {"mwc", "pressure_head", "m"},
        {"fwc", "pressure_head", "ft"},
        {"ft", "pressure_head", "ft"},
    };
    for (const PressureCase &pressure_case : pressure_cases)
    {
        QJsonObject source;
        source.insert(QStringLiteral("pressure"), QString::fromLatin1(pressure_case.source));
        QStringList unmapped;
        const QJsonObject profile = EpanetJsUnitProfile::selections(source, &unmapped);
        expectTrue(profile.value(QString::fromLatin1(pressure_case.quantity)).toString()
                       == QString::fromLatin1(pressure_case.display), pressure_case.source);
        expectTrue(unmapped.isEmpty(), pressure_case.source);
        expectTrue(EpanetJsUnits::pressureToHeadM(
                       1.0, QString::fromLatin1(pressure_case.source), 1.0).has_value(),
                   pressure_case.source);
    }

    // Distinct source fields must not overwrite one another in the proposed
    // AOWIS unit profile (nor affect the canonical numerical import).
    QJsonObject distinct_units;
    distinct_units.insert(QStringLiteral("flow"), QStringLiteral("l/s"));
    distinct_units.insert(QStringLiteral("baseDemand"), QStringLiteral("m^3/h"));
    distinct_units.insert(QStringLiteral("customerDemand"), QStringLiteral("gal/min"));
    distinct_units.insert(QStringLiteral("level"), QStringLiteral("m"));
    distinct_units.insert(QStringLiteral("initialLevel"), QStringLiteral("ft"));
    distinct_units.insert(QStringLiteral("minLevel"), QStringLiteral("m"));
    distinct_units.insert(QStringLiteral("maxLevel"), QStringLiteral("ft"));
    distinct_units.insert(QStringLiteral("volume"), QStringLiteral("m^3"));
    distinct_units.insert(QStringLiteral("minVolume"), QStringLiteral("ft^3"));
    QStringList distinct_unmapped;
    const QJsonObject distinct_profile = EpanetJsUnitProfile::selections(
        distinct_units, &distinct_unmapped);
    expectTrue(distinct_unmapped.isEmpty(), "independent EPANET-JS units all recognized");
    struct MappingCase { const char *quantity; const char *unit; };
    const MappingCase cases[] = {
        {"volumetric_flow_rate", "L/s"},
        {"base_demand_flow_rate", "m3/h"},
        {"customer_demand_flow_rate", "US gal/min"},
        {"water_level", "m"},
        {"tank_initial_level", "ft"},
        {"tank_minimum_level", "m"},
        {"tank_maximum_level", "ft"},
        {"volume", "m3"},
        {"tank_minimum_volume", "ft3"},
    };
    for (const MappingCase &mapping : cases) {
        const QString target = QString::fromLatin1(mapping.quantity);
        expectTrue(distinct_profile.value(target).toString()
                       == QString::fromLatin1(mapping.unit), mapping.quantity);
    }
    QJsonObject length_source;
    length_source.insert(QStringLiteral("elevation"), QStringLiteral("metre"));
    length_source.insert(QStringLiteral("length"), QStringLiteral("foot"));
    length_source.insert(QStringLiteral("diameter"), QStringLiteral("inches"));
    QStringList unmapped;
    const QJsonObject lengths = EpanetJsUnitProfile::selections(length_source, &unmapped);
    expectTrue(lengths.value(QStringLiteral("elevation")).toString() == QStringLiteral("m"),
               "source metres spelling maps to elevation display");
    expectTrue(lengths.value(QStringLiteral("length")).toString() == QStringLiteral("ft"),
               "source foot spelling maps to length display");
    expectTrue(lengths.value(QStringLiteral("link_diameter")).toString() == QStringLiteral("in"),
               "source inches spelling maps to diameter display");
    expectTrue(unmapped.isEmpty(), "recognised length spellings map to the Model registry");
    // Numerical diameter import already accepts feet. Profile extraction
    // must preserve that source preference instead of reporting it unmapped.
    for (const char *diameter_source : {"ft", "foot", "feet", "cm", "m", "in"})
    {
        QJsonObject diameter_settings;
        diameter_settings.insert(QStringLiteral("diameter"),
                                 QString::fromLatin1(diameter_source));
        QStringList diameter_unmapped;
        const QJsonObject mapped = EpanetJsUnitProfile::selections(
            diameter_settings, &diameter_unmapped);
        const QString display_unit = mapped.value(QStringLiteral("link_diameter")).toString();
        expectTrue(!display_unit.isEmpty(), diameter_source);
        expectTrue(diameter_unmapped.isEmpty(), diameter_source);
        expectTrue(EpanetJsUnits::diameterToMm(
                       1.0, QString::fromLatin1(diameter_source)).has_value(),
                   diameter_source);
        const std::optional<double> round_trip = aowis::units::convert(
            "link_diameter", 1.0, display_unit.toStdString(), "mm");
        expectTrue(round_trip.has_value(), diameter_source);
        if (round_trip.has_value())
            expectNear(*round_trip,
                       *EpanetJsUnits::diameterToMm(
                           1.0, QString::fromLatin1(diameter_source)),
                       1e-10, diameter_source);
    }
}

// The source roughness preference is independent of link diameter. Numerical
// Darcy-Weisbach import and the profile must agree on canonical millimetres.
void testEpanetJsProfileRetainsDarcyRoughnessUnits()
{
    struct RoughnessCase { const char *source; const char *display; double millimetres; };
    const RoughnessCase cases[] = {
        {"mm", "mm", 1.0}, {"millimeter", "mm", 1.0},
        {"millimetre", "mm", 1.0},
        {"millift", "millift", 0.3048}, {"millifeet", "millift", 0.3048},
        {"milli-ft", "millift", 0.3048}, {"milli ft", "millift", 0.3048},
        {"0.001ft", "millift", 0.3048}, {"0.001 ft", "millift", 0.3048},
        {"ft", "ft", 304.8}, {"foot", "ft", 304.8}, {"feet", "ft", 304.8}
    };
    for (const RoughnessCase &roughness_case : cases)
    {
        const QString source_unit = QString::fromLatin1(roughness_case.source);
        QJsonObject source;
        source.insert(QStringLiteral("roughness"), source_unit);
        QStringList unmapped;
        const QJsonObject profile = EpanetJsUnitProfile::selections(source, &unmapped);
        expectTrue(unmapped.isEmpty(), roughness_case.source);
        const QString display_unit = profile.value(
            QStringLiteral("darcy_weisbach_roughness_height")).toString();
        expectTrue(display_unit == QString::fromLatin1(roughness_case.display),
                   roughness_case.source);
        const std::optional<double> numerical = EpanetJsUnits::darcyRoughnessToMm(
            1.0, source_unit, QStringLiteral("L/s"));
        const std::optional<double> presentation = aowis::units::convert(
            "darcy_weisbach_roughness_height", 1.0, display_unit.toStdString(), "mm");
        expectTrue(numerical.has_value() && presentation.has_value(), roughness_case.source);
        if (numerical.has_value() && presentation.has_value())
        {
            expectNear(*numerical, roughness_case.millimetres, 1e-12,
                       roughness_case.source);
            expectNear(*presentation, *numerical, 1e-12, roughness_case.source);
        }
    }

    // A dimensionless coefficient must not be mistaken for a length unit.
    QJsonObject dimensionless_source;
    dimensionless_source.insert(QStringLiteral("roughness"), QStringLiteral("1"));
    QStringList unmapped;
    const QJsonObject dimensionless_profile = EpanetJsUnitProfile::selections(
        dimensionless_source, &unmapped);
    expectTrue(!dimensionless_profile.contains(
                   QStringLiteral("darcy_weisbach_roughness_height")),
               "dimensionless roughness does not become a Darcy roughness height");
    expectTrue(unmapped.isEmpty(), "dimensionless roughness does not cause a warning");
}

// Missing Darcy roughness metadata must resolve to the same unit as the
// numerical importer: mm for metric EPANET flow families, millift for US.
// Non-Darcy coefficients must never acquire a spurious length preference.
void testInferredDarcyRoughnessMatchesImportedProfile()
{
    struct RoughnessCase {
        const char *flow;
        const char *formula;
        const char *explicit_roughness;
        const char *expected_display;
        double expected_mm;
    };
    const RoughnessCase cases[] = {
        {"l/s", "D-W", "", "mm", 0.5},
        {"gpm", "D-W", "", "millift", 0.1524},
        {"cfs", "D-W", "", "millift", 0.1524},
        {"gpm", "D-W", "mm", "mm", 0.5},
        {"l/s", "D-W", "millift", "millift", 0.1524},
        {"l/s", "H-W", "", "", 0.0},
        {"gpm", "C-M", "", "", 0.0},
    };
    for (const RoughnessCase &roughness_case : cases)
    {
        EpanetJsProjectSnapshot project = makeProjectWithUniqueId(
            QStringLiteral("e1c02bd0-a331-4cd0-90c7-807004f23ea3"));
        QJsonObject units;
        units.insert(QStringLiteral("flow"), QString::fromLatin1(roughness_case.flow));
        if (roughness_case.explicit_roughness[0] != '\0')
            units.insert(QStringLiteral("roughness"),
                         QString::fromLatin1(roughness_case.explicit_roughness));

        QJsonObject settings;
        settings.insert(QStringLiteral("name"), QStringLiteral("roughness-inference"));
        settings.insert(QStringLiteral("uniqueId"),
                        QStringLiteral("e1c02bd0-a331-4cd0-90c7-807004f23ea3"));
        settings.insert(QStringLiteral("headlossFormula"),
                        QString::fromLatin1(roughness_case.formula));
        settings.insert(QStringLiteral("units"), units);
        setProjectSettings(project, settings);

        EpanetJsTableSnapshot pipes = project.tables.value(QStringLiteral("pipes"));
        pipes.columns.append(QStringLiteral("length"));
        pipes.columns.append(QStringLiteral("diameter"));
        pipes.columns.append(QStringLiteral("roughness"));
        pipes.rows[0].insert(QStringLiteral("length"), 100.0);
        pipes.rows[0].insert(QStringLiteral("diameter"), 12.0);
        pipes.rows[0].insert(QStringLiteral("roughness"), 0.5);
        project.tables.insert(QStringLiteral("pipes"), pipes);

        const EpanetJsProjectConversionResult converted =
            EpanetJsProjectConverter::convert(project);
        expectTrue(converted.success, roughness_case.flow);
        expectTrue(converted.network.links_pipes.size() == 1, roughness_case.formula);
        if (!converted.success || converted.network.links_pipes.size() != 1)
            continue;

        QStringList unmapped;
        const QJsonObject profile = EpanetJsUnitProfile::selections(
            converted.source_display_units, &unmapped);
        const QString profile_unit = profile.value(
            QStringLiteral("darcy_weisbach_roughness_height")).toString();
        const QString expected_unit = QString::fromLatin1(roughness_case.expected_display);
        expectTrue(unmapped.isEmpty(), "resolved source units are profile-compatible");
        expectTrue(profile_unit == expected_unit, roughness_case.formula);
        expectTrue(converted.source_display_units.value(QStringLiteral("roughness"))
                       .toString().isEmpty() == expected_unit.isEmpty(),
                   "resolved roughness metadata agrees with the headloss formula");

        if (expected_unit.isEmpty())
            continue;

        const double canonical = converted.network.links_pipes.first()
            .roughness_darcy_weisbach_mm;
        expectNear(canonical, roughness_case.expected_mm, 1e-12,
                   "imported Darcy roughness remains in canonical millimetres");
        const std::optional<double> from_profile = aowis::units::convert(
            "darcy_weisbach_roughness_height", 0.5,
            profile_unit.toStdString(), "mm");
        expectTrue(from_profile.has_value(), "resolved profile unit is convertible");
        if (from_profile.has_value())
            expectNear(*from_profile, canonical, 1e-12,
                       "profile roughness and imported roughness are physically equivalent");
    }
}

// Every water-age unit the numerical importer accepts must also be representable
// by the optional custom presentation profile. Canonical hours are not changed.
void testEpanetJsProfileRetainsWaterAgeSecondsAndPressureHeadAliases()
{
    struct AgeCase { const char *source; const char *display; };
    const AgeCase age_cases[] = {
        {"s", "s"}, {"sec", "s"}, {"second", "s"}, {"seconds", "s"},
        {"min", "min"}, {"minute", "min"}, {"minutes", "min"},
        {"h", "h"}, {"hr", "h"}, {"hrs", "h"}, {"hour", "h"},
        {"hours", "h"}, {"d", "d"}, {"day", "d"}, {"days", "d"},
    };
    for (const AgeCase &age_case : age_cases)
    {
        const QString source = QString::fromLatin1(age_case.source);
        const QString unit = QString::fromLatin1(age_case.display);
        QJsonObject settings;
        settings.insert(QStringLiteral("waterAge"), source);
        QStringList unmapped;
        const QJsonObject selections = EpanetJsUnitProfile::selections(settings, &unmapped);
        expectTrue(unmapped.isEmpty(), age_case.source);
        expectTrue(selections.value(QStringLiteral("water_age")).toString() == unit,
                   age_case.source);
        const std::optional<double> imported = EpanetJsUnits::waterAgeToHours(3600.0, source);
        const std::optional<double> converted = aowis::units::convert(
            "water_age", 3600.0, unit.toStdString(), "h");
        expectTrue(imported.has_value() && converted.has_value(), age_case.source);
        if (imported.has_value() && converted.has_value())
            expectNear(*converted, *imported, 1e-10, age_case.source);
    }

    struct PressureCase { const char *source; const char *display; };
    const PressureCase pressure_cases[] = {
        {"mwc", "m"}, {"mH2O", "m"}, {"fwc", "ft"}, {"ftwc", "ft"},
    };
    for (const PressureCase &pressure_case : pressure_cases)
    {
        const QString source = QString::fromLatin1(pressure_case.source);
        QJsonObject settings;
        settings.insert(QStringLiteral("pressure"), source);
        QStringList unmapped;
        const QJsonObject selections = EpanetJsUnitProfile::selections(settings, &unmapped);
        const QString unit = QString::fromLatin1(pressure_case.display);
        expectTrue(unmapped.isEmpty(), pressure_case.source);
        expectTrue(selections.value(QStringLiteral("pressure_head")).toString() == unit,
                   pressure_case.source);
        const std::optional<double> imported = EpanetJsUnits::pressureToHeadM(1.0, source, 1.0);
        const std::optional<double> converted = aowis::units::convert(
            "pressure_head", 1.0, unit.toStdString(), "m");
        expectTrue(imported.has_value() && converted.has_value(), pressure_case.source);
        if (imported.has_value() && converted.has_value())
            expectNear(*converted, *imported, 1e-12, pressure_case.source);
    }
}
