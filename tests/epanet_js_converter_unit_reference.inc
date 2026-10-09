// Independent dimensional reference checks. The constants below are defined from
// international unit definitions, not from aowis::units or the importer implementation.
// These verify physical conversions, NOT EPANET's historical internal factors.
void testIndependentUnitReferenceValues()
{
    const double us_gallon_m3 = 231.0 * 0.0254 * 0.0254 * 0.0254;
    const double imperial_gallon_m3 = 0.00454609;
    const double foot_m = 0.3048;
    const double psi_pa = 6894.757293168361;
    const double standard_gravity = 9.80665;
    const double density = 1200.0; // specific gravity 1.2

    const auto check = [](const std::optional<double> &actual, double expected,
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

    const double psi_head_m = psi_pa / (density * standard_gravity);
    check(EpanetJsUnits::pressureToHeadM(1.0, QStringLiteral("psi"), 1.2),
          psi_head_m, 1e-12, "psi to head with specific gravity");
    check(EpanetJsUnits::pressureToHeadM(100.0, QStringLiteral("kpa"), 1.2),
          100000.0 / (density * standard_gravity), 1e-12, "kPa to head");
    check(EpanetJsUnits::pressureToHeadM(1.0, QStringLiteral("bar"), 1.2),
          100000.0 / (density * standard_gravity), 1e-12, "bar to head");
    check(EpanetJsUnits::emitterCoefficientToCanonical(
              1.0, 0.5, QStringLiteral("gpm"), QStringLiteral("psi"), 1.2),
          us_gallon_m3 * 60.0 / std::sqrt(psi_head_m), 1e-12,
          "US emitter coefficient to canonical flow/head units");
    expectTrue(!EpanetJsUnits::pressureToHeadM(1.0, QStringLiteral("psi"), 0.0).has_value(),
               "zero specific gravity is rejected");
}
