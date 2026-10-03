#include "import/epanet_js_project_converter.h"
#include "import/epanet_js_project_reader.h"
#include "test_harness.h"

#include <aowis/epanet/epanet_runner.h>

#include <QCoreApplication>
#include <QDir>
#include <QString>

#include <cstdio>
#include <optional>

namespace
{
AowisTestHarness test_harness;

void expectTrue(bool condition, const char *message)
{
    test_harness.expectTrue(condition, message);
}

void expectNear(double actual, double expected, double tolerance, const char *message)
{
    test_harness.expectNear(actual, expected, tolerance, message);
}

QString fixturePath(const QString &file_name)
{
    return QDir(QStringLiteral(AOWIS_EPANET_JS_FIXTURE_DIR)).filePath(file_name);
}

void printDiagnostics(const EpanetJsProjectConversionResult &result)
{
    for (const EpanetJsConversionDiagnostic &diagnostic : result.diagnostics)
    {
        std::fprintf(
            stderr,
            "epanet-js conversion diagnostic [%s] %s: %s\n",
            diagnostic.severity == EpanetJsConversionDiagnosticSeverity::Error
                ? "error"
                : "warning",
            diagnostic.code.toUtf8().constData(),
            diagnostic.message.toUtf8().constData());
    }
}

EpanetJsProjectConversionResult readAndConvertFixture(const QString &file_name)
{
    const EpanetJsProjectReadResult read_result = EpanetJsProjectReader::readFile(
        fixturePath(file_name));
    expectTrue(read_result.success, "synthetic epanet-js fixture can be read as SQLite project");
    expectTrue(read_result.project.user_version == 24,
               "synthetic epanet-js fixture uses the version-24 schema contract");

    if (!read_result.success)
    {
        std::fprintf(stderr, "epanet-js reader error: %s\n", read_result.error.toUtf8().constData());
        return {};
    }

    const EpanetJsProjectConversionResult conversion = EpanetJsProjectConverter::convert(
        read_result.project);
    if (!conversion.success)
        printDiagnostics(conversion);
    expectTrue(conversion.success,
               "synthetic epanet-js fixture converts without blocking diagnostics");
    return conversion;
}

EpanetResultImport importPairedInp(const QString &file_name)
{
    const EpanetResultImport result = EpanetRunner().importInp(fixturePath(file_name));
    expectTrue(result.status.success, "paired synthetic INP imports through the EPANET adapter");
    return result;
}

const HydraulicPipeMaterial *materialById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicPipeMaterial &material : network.pipe_materials)
    {
        if (material.id == id)
            return &material;
    }
    return nullptr;
}

double nativeJunctionDemandM3PerH(const NetworkHydraulic &network)
{
    double total = 0.0;
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        for (const HydraulicDemand &demand : junction.demands)
            total += demand.base_demand_m3_per_h;
    }
    return total;
}

double customerDemandM3PerH(const NetworkHydraulic &network)
{
    double total = 0.0;
    for (const HydraulicDemandPoint &demand_point : network.demand_points)
    {
        for (const HydraulicDemand &demand : demand_point.demands)
            total += demand.base_demand_m3_per_h;
    }
    return total;
}

void expectPhysicalTopologyMatches(
    const NetworkHydraulic &ejsdb_network,
    const NetworkHydraulic &inp_network)
{
    expectTrue(ejsdb_network.nodes_junctions.size() == inp_network.nodes_junctions.size(),
               "paired fixtures contain the same junction count");
    expectTrue(ejsdb_network.nodes_reservoirs.size() == inp_network.nodes_reservoirs.size(),
               "paired fixtures contain the same reservoir count");
    expectTrue(ejsdb_network.nodes_tanks.size() == inp_network.nodes_tanks.size(),
               "paired fixtures contain the same tank count");
    expectTrue(ejsdb_network.links_pipes.size() == inp_network.links_pipes.size(),
               "paired fixtures contain the same pipe count");
    expectTrue(ejsdb_network.links_pumps.size() == inp_network.links_pumps.size(),
               "paired fixtures contain the same pump count");
    expectTrue(ejsdb_network.links_valves.size() == inp_network.links_valves.size(),
               "paired fixtures contain the same valve count");
    expectTrue(ejsdb_network.controls_simple.size() == inp_network.controls_simple.size(),
               "paired fixtures contain the same simple-control count");

    const double ejsdb_total_demand = nativeJunctionDemandM3PerH(ejsdb_network)
        + customerDemandM3PerH(ejsdb_network);
    const double inp_total_demand = nativeJunctionDemandM3PerH(inp_network);
    expectNear(ejsdb_total_demand, inp_total_demand, 1e-9,
               "paired INP aggregates customer demand without changing total base demand");
}

void expectAllCustomerPointsUseAssignedJunctionMode(
    const NetworkHydraulic &network,
    qsizetype expected_count)
{
    qsizetype assigned_count = 0;
    for (const HydraulicDemandPoint &demand_point : network.demand_points)
    {
        if (demand_point.attachment.type == HydraulicDemandPointAttachmentType::Pipe
            && demand_point.attachment.pipe_allocation_mode
                == HydraulicDemandPointPipeAllocationMode::AssignedJunction
            && !demand_point.attachment.pipe_uuid.isNull()
            && !demand_point.attachment.pipe_assigned_junction_uuid.isNull()
            && demand_point.attachment.pipe_position >= 0.0
            && demand_point.attachment.pipe_position <= 1.0)
        {
            ++assigned_count;
        }
    }
    expectTrue(assigned_count == expected_count,
               "all synthetic customer points preserve pipe attachment plus assigned junction");
}

void testSyntheticMetricProject()
{
    const EpanetJsProjectConversionResult result = readAndConvertFixture(
        QStringLiteral("synthetic-metric.ejsdb"));
    if (!result.success)
        return;

    const EpanetResultImport inp_result = importPairedInp(
        QStringLiteral("synthetic-metric.inp"));
    if (!inp_result.status.success)
        return;

    const NetworkHydraulic &network = result.network;
    expectTrue(network.id == QStringLiteral("synthetic-metric"),
               "synthetic metric project name is preserved");
    expectTrue(network.nodes_junctions.size() == 3, "metric fixture imports three junctions");
    expectTrue(network.nodes_reservoirs.size() == 1, "metric fixture imports one reservoir");
    expectTrue(network.nodes_tanks.size() == 1, "metric fixture imports one tank");
    expectTrue(network.links_pipes.size() == 3, "metric fixture imports three pipes");
    expectTrue(network.links_pumps.size() == 1, "metric fixture imports one pump");
    expectTrue(network.links_valves.size() == 1, "metric fixture imports one valve");
    expectTrue(network.patterns_time.size() == 1, "metric fixture imports its demand pattern");
    expectTrue(network.pipe_materials.size() == 2, "metric fixture imports two pipe materials");
    expectTrue(network.demand_points.size() == 3, "metric fixture imports three customer points");
    expectTrue(network.controls_simple.size() == 2,
               "metric structured level-setting control becomes two AOWIS simple controls");

    expectNear(nativeJunctionDemandM3PerH(network), 6.12, 1e-12,
               "metric native junction demands convert from l/s to m3/h");
    expectNear(customerDemandM3PerH(network), 1.26, 1e-12,
               "metric customer demands convert from l/s to m3/h");
    expectAllCustomerPointsUseAssignedJunctionMode(network, 3);

    const HydraulicPipeMaterial *pvc = materialById(
        network, QStringLiteral("AOWIS Synthetic PVC"));
    expectTrue(pvc != nullptr, "metric synthetic PVC material is imported");
    if (pvc != nullptr)
    {
        const std::optional<double> age_zero = resolveHydraulicPipeMaterialRoughness(
            *pvc, HydraulicHeadlossFormula::HazenWilliams, 0);
        expectTrue(age_zero.has_value(), "metric material roughness resolves at age zero");
        if (age_zero.has_value())
            expectNear(age_zero.value(), 150.0, 1e-12,
                       "metric material age-zero Hazen-Williams value is preserved");
    }

    expectNear(network.controls_simple.at(0).trigger_water_level_m, 2.0, 1e-12,
               "metric low-level control retains its two-metre threshold");
    expectNear(network.controls_simple.at(1).trigger_water_level_m, 4.0, 1e-12,
               "metric high-level control retains its four-metre threshold");

    expectPhysicalTopologyMatches(network, inp_result.request.network);
}

void testSyntheticUsCustomaryProject()
{
    const EpanetJsProjectConversionResult result = readAndConvertFixture(
        QStringLiteral("synthetic-us-customary.ejsdb"));
    if (!result.success)
        return;

    const EpanetResultImport inp_result = importPairedInp(
        QStringLiteral("synthetic-us-customary.inp"));
    if (!inp_result.status.success)
        return;

    const NetworkHydraulic &network = result.network;
    expectTrue(network.id == QStringLiteral("synthetic-us-customary"),
               "synthetic US-customary project name is preserved");
    expectTrue(network.nodes_junctions.size() == 3,
               "US-customary fixture imports three junctions");
    expectTrue(network.nodes_reservoirs.size() == 1,
               "US-customary fixture imports one reservoir");
    expectTrue(network.nodes_tanks.size() == 1,
               "US-customary fixture imports one tank");
    expectTrue(network.links_pipes.size() == 3,
               "US-customary fixture imports three pipes");
    expectTrue(network.links_pumps.isEmpty(),
               "US-customary fixture intentionally contains no pump");
    expectTrue(network.links_valves.size() == 1,
               "US-customary fixture imports one valve");
    expectTrue(network.pipe_materials.size() == 2,
               "US-customary fixture imports two pipe materials");
    expectTrue(network.demand_points.size() == 3,
               "US-customary fixture imports three customer points");
    expectTrue(network.controls_simple.size() == 2,
               "US-customary raw controls become two AOWIS simple controls");

    expectNear(nativeJunctionDemandM3PerH(network), 35.0 * 0.22712470704, 1e-12,
               "US-customary native junction demands convert from gal/min to m3/h");
    expectNear(customerDemandM3PerH(network), 6.0 * 0.22712470704, 1e-12,
               "US-customary customer demands convert from gal/min to m3/h");
    expectAllCustomerPointsUseAssignedJunctionMode(network, 3);

    const HydraulicPipeMaterial *steel = materialById(
        network, QStringLiteral("AOWIS Synthetic Steel"));
    expectTrue(steel != nullptr, "US-customary synthetic steel material is imported");
    if (steel != nullptr)
    {
        const std::optional<double> age_zero = resolveHydraulicPipeMaterialRoughness(
            *steel, HydraulicHeadlossFormula::HazenWilliams, 0);
        expectTrue(age_zero.has_value(), "US-customary material roughness resolves at age zero");
        if (age_zero.has_value())
            expectNear(age_zero.value(), 120.0, 1e-12,
                       "US-customary material age-zero Hazen-Williams value is preserved");
    }

    expectNear(network.controls_simple.at(0).trigger_water_level_m, 2.4384, 1e-12,
               "US-customary eight-foot low-level trigger converts to metres");
    expectNear(network.controls_simple.at(1).trigger_water_level_m, 4.8768, 1e-12,
               "US-customary sixteen-foot high-level trigger converts to metres");

    expectPhysicalTopologyMatches(network, inp_result.request.network);
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    test_harness.runCase(
        "synthetic metric epanet-js and INP interoperability",
        testSyntheticMetricProject);
    test_harness.runCase(
        "synthetic US-customary epanet-js and INP interoperability",
        testSyntheticUsCustomaryProject);
    return test_harness.finish();
}
