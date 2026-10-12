// Independent unit reference checks. Ordinary length/flow conversions use exact
// international definitions; EPANET pressure/head import deliberately uses its
// historical rounded solver constants.
void testIndependentUnitReferenceValues()
{
    const double us_gallon_m3 = 231.0 * 0.0254 * 0.0254 * 0.0254;
    const double imperial_gallon_m3 = 0.00454609;
    const double foot_m = 0.3048;
    const double epanet_psi_per_ft = 0.4333;
    const double epanet_kpa_per_psi = 6.895;
    const double epanet_bar_per_psi = 0.068948;
    const double specific_gravity = 1.2;

    void (*const check)(const std::optional<double> &, double, double, const char *) =
        [](const std::optional<double> &actual, double expected,
           double tolerance, const char *message) {
        expectTrue(actual.has_value(), message);
        if (actual.has_value())
            expectNear(*actual, expected, tolerance, message);
    };

    check(EpanetJsUnits::flowToM3PerH(1.0, QStringLiteral("gpm")),
          us_gallon_m3 * 60.0, 1e-12, "US gallon per minute");
    check(EpanetJsUnits::flowToM3PerH(1.0, QStringLiteral("mgd")),
          us_gallon_m3 * 1000000.0 / 24.0, 1e-10, "million US gallons per day");
    check(EpanetJsUnits::flowToM3PerH(1.0, QStringLiteral("imgd")),
          imperial_gallon_m3 * 1000000.0 / 24.0, 1e-10, "million imperial gallons per day");
    check(EpanetJsUnits::flowToM3PerH(1.0, QStringLiteral("cfs")),
          foot_m * foot_m * foot_m * 3600.0, 1e-10, "cubic feet per second");
    check(EpanetJsUnits::lengthToM(1.0, QStringLiteral("ft")),
          foot_m, 1e-14, "international foot");
    check(EpanetJsUnits::diameterToMm(1.0, QStringLiteral("in")),
          25.4, 1e-12, "inch diameter");
    check(EpanetJsUnits::volumeToM3(1.0, QStringLiteral("ft3")),
          foot_m * foot_m * foot_m, 1e-14, "cubic foot volume");
    check(EpanetJsUnits::darcyRoughnessToMm(1.0, QString(), QStringLiteral("gpm")),
          foot_m, 1e-14, "US Darcy roughness in millifeet");
    check(EpanetJsUnits::darcyRoughnessToMm(1.0, QString(), QStringLiteral("lps")),
          1.0, 1e-14, "metric Darcy roughness in millimetres");

    const double psi_head_m = foot_m / (epanet_psi_per_ft * specific_gravity);
    check(EpanetJsUnits::pressureToHeadM(1.0, QStringLiteral("psi"), 1.2),
          psi_head_m, 1e-12, "EPANET psi to head with specific gravity");
    check(EpanetJsUnits::pressureToHeadM(1.0, QStringLiteral("psi"), 1.0),
          0.7034387260558504, 1e-14, "EPANET 2.3 psi reference at SG=1");
    check(EpanetJsUnits::pressureToHeadM(1.0, QStringLiteral("kPa"), 1.0),
          0.10202157013137787, 1e-14, "EPANET 2.3 kPa reference at SG=1");
    check(EpanetJsUnits::pressureToHeadM(1.0, QStringLiteral("bar"), 1.0),
          10.202452950859351, 1e-12, "EPANET 2.3 bar reference at SG=1");
    check(EpanetJsUnits::pressureToHeadM(100.0, QStringLiteral("kpa"), 1.2),
          100.0 * foot_m / (epanet_psi_per_ft * epanet_kpa_per_psi * specific_gravity),
          1e-12, "EPANET kPa to head");
    check(EpanetJsUnits::pressureToHeadM(1.0, QStringLiteral("bar"), 1.2),
          foot_m / (epanet_psi_per_ft * epanet_bar_per_psi * specific_gravity),
          1e-12, "EPANET bar to head");
    // EPANET 2.3 emitters use psi (US) or metres of head (SI), independent of
    // the PRESSURE option and, for SI, of specific gravity.
    check(EpanetJsUnits::emitterCoefficientToCanonical(
              1.0, 0.5, QStringLiteral("gpm"), 1.2),
          us_gallon_m3 * 60.0 / std::sqrt(psi_head_m), 1e-12,
          "US emitter coefficient to canonical flow/head units");
    check(EpanetJsUnits::emitterCoefficientToCanonical(
              1.0, 0.5, QStringLiteral("l/s"), 1.2),
          3.6, 1e-12,
          "SI emitter coefficient uses metres of head without specific gravity");
    // EPANET 2.3 PRESSURE METERS / FEET are head of the simulated fluid.
    check(EpanetJsUnits::pressureToHeadM(1.0, QStringLiteral("mwc"), 1.2),
          1.0, 1e-12, "mwc is fluid head; specific gravity does not apply");
    check(EpanetJsUnits::pressureToHeadM(1.0, QStringLiteral("fwc"), 1.2),
          foot_m, 1e-12, "fwc is fluid head in feet; specific gravity does not apply");
    check(EpanetJsUnits::pressureToHeadM(1.0, QStringLiteral("ft"), 1.2),
          foot_m, 1e-12, "feet pressure alias is fluid head");
    expectNear(EpanetJsUnits::chemicalConcentrationScaleToMgPerL(QStringLiteral("μg/L")).value_or(-1.0),
               0.001, 1e-15, "Greek mu concentration unit");
    expectTrue(!EpanetJsUnits::pressureToHeadM(1.0, QStringLiteral("psi"), 0.0).has_value(),
               "zero specific gravity is rejected");
}

// The ten flow-unit spellings epanet-js writes for the EPANET unit systems
// (libs/quantity FlowUnit, libs/project-settings presets), each checked against
// an independent definition and classified into the right EPANET unit system.
void testEpanetJsFlowUnitSpellings()
{
    const double us_gallon_m3 = 231.0 * 0.0254 * 0.0254 * 0.0254;
    const double imperial_gallon_m3 = 0.00454609;
    const double cubic_foot_m3 = 0.3048 * 0.3048 * 0.3048;

    struct FlowCase
    {
        const char *unit;
        double cubic_metres_per_hour;
        bool us_system;
    };
    const FlowCase cases[] = {
        {"l/s", 3.6, false},
        {"l/min", 0.06, false},
        {"Ml/d", 1000.0 / 24.0, false},
        {"m^3/h", 1.0, false},
        {"m^3/d", 1.0 / 24.0, false},
        {"gal/min", us_gallon_m3 * 60.0, true},
        {"ft^3/s", cubic_foot_m3 * 3600.0, true},
        {"Mgal/d", us_gallon_m3 * 1.0e6 / 24.0, true},
        {"IMgal/d", imperial_gallon_m3 * 1.0e6 / 24.0, true},
        {"acft/d", 43560.0 * cubic_foot_m3 / 24.0, true},
    };
    for (const FlowCase &flow_case : cases)
    {
        const QString unit = QString::fromUtf8(flow_case.unit);
        const std::optional<double> converted = EpanetJsUnits::flowToM3PerH(1.0, unit);
        expectTrue(converted.has_value(), flow_case.unit);
        if (converted.has_value())
            expectNear(*converted, flow_case.cubic_metres_per_hour,
                       1e-10 * flow_case.cubic_metres_per_hour, flow_case.unit);
        expectTrue(EpanetJsUnits::flowUnitUsesFootLength(unit) == flow_case.us_system,
                   flow_case.unit);
        expectTrue(EpanetJsUnits::flowUnitUsesMetricLength(unit) == !flow_case.us_system,
                   flow_case.unit);
    }
    expectTrue(!EpanetJsUnits::flowToM3PerH(1.0, QStringLiteral("ml/d")).has_value(),
               "millilitres per day is not mistaken for megalitres per day");
}
