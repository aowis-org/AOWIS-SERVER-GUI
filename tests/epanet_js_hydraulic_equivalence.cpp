#include "epanet_js_hydraulic_equivalence.h"

#include <aowis/epanet/epanet_runner.h>

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFile>
#include <QTemporaryFile>

#include <QMap>
#include <QSet>
#include <QStringList>

namespace
{
const HydraulicSimulationResultNodeJunction *junctionResultById(
    const HydraulicSimulationResult &result,
    const QString &id)
{
    for (const HydraulicSimulationResultNodeJunction &node : result.nodes_junctions)
    {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

const HydraulicSimulationResultNodeReservoir *reservoirResultById(
    const HydraulicSimulationResult &result,
    const QString &id)
{
    for (const HydraulicSimulationResultNodeReservoir &node : result.nodes_reservoirs)
    {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

const HydraulicSimulationResultNodeTank *tankResultById(
    const HydraulicSimulationResult &result,
    const QString &id)
{
    for (const HydraulicSimulationResultNodeTank &node : result.nodes_tanks)
    {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

const HydraulicSimulationResultLinkPipe *pipeResultById(
    const HydraulicSimulationResult &result,
    const QString &id)
{
    for (const HydraulicSimulationResultLinkPipe &link : result.links_pipes)
    {
        if (link.id == id)
            return &link;
    }
    return nullptr;
}

const HydraulicSimulationResultLinkPump *pumpResultById(
    const HydraulicSimulationResult &result,
    const QString &id)
{
    for (const HydraulicSimulationResultLinkPump &link : result.links_pumps)
    {
        if (link.id == id)
            return &link;
    }
    return nullptr;
}

const HydraulicSimulationResultLinkValve *valveResultById(
    const HydraulicSimulationResult &result,
    const QString &id)
{
    for (const HydraulicSimulationResultLinkValve &link : result.links_valves)
    {
        if (link.id == id)
            return &link;
    }
    return nullptr;
}

QStringList hydraulicSectionOrder()
{
    return QStringList{
        QStringLiteral("JUNCTIONS"),
        QStringLiteral("RESERVOIRS"),
        QStringLiteral("TANKS"),
        QStringLiteral("PIPES"),
        QStringLiteral("PUMPS"),
        QStringLiteral("VALVES"),
        QStringLiteral("DEMANDS"),
        QStringLiteral("EMITTERS"),
        QStringLiteral("STATUS"),
        QStringLiteral("PATTERNS"),
        QStringLiteral("CURVES"),
        QStringLiteral("CONTROLS"),
        QStringLiteral("RULES"),
        QStringLiteral("OPTIONS"),
        QStringLiteral("TIMES"),
        QStringLiteral("ENERGY")};
}

QStringList canonicalGroupedSectionLines(const QStringList &lines)
{
    QMap<QString, QStringList> values_by_id;
    for (const QString &line : lines)
    {
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.isEmpty())
            continue;

        const QString id = fields.first();
        values_by_id[id].append(fields.mid(1).join(QLatin1Char(' ')));
    }

    QStringList canonical;
    for (QMap<QString, QStringList>::const_iterator iterator = values_by_id.constBegin();
         iterator != values_by_id.constEnd();
         ++iterator)
    {
        canonical.append(QStringLiteral("%1 %2")
                             .arg(iterator.key(), iterator.value().join(QStringLiteral(" | ")))
                             .trimmed());
    }
    return canonical;
}

QMap<QString, QStringList> canonicalHydraulicSections(const QString &inp_text)
{
    const QStringList included_sections = hydraulicSectionOrder();
    QSet<QString> included_set;
    for (const QString &section_name : included_sections)
        included_set.insert(section_name);
    QMap<QString, QStringList> sections;
    QString current_section;

    const QStringList input_lines = inp_text.split(QLatin1Char('\n'));
    for (QString line : input_lines)
    {
        line = line.trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
        {
            current_section = line.mid(1, line.size() - 2).trimmed().toUpper();
            continue;
        }
        if (!included_set.contains(current_section))
            continue;

        const qsizetype comment_index = line.indexOf(QLatin1Char(';'));
        if (comment_index >= 0)
            line = line.left(comment_index);
        line = line.simplified();
        if (!line.isEmpty())
            sections[current_section].append(line);
    }

    for (const QString &section_name : included_sections)
    {
        QStringList &lines = sections[section_name];
        if (section_name == QStringLiteral("PATTERNS")
            || section_name == QStringLiteral("CURVES"))
        {
            lines = canonicalGroupedSectionLines(lines);
        }
        else
        {
            std::sort(lines.begin(), lines.end());
        }
    }
    return sections;
}

bool near(double left, double right, double tolerance)
{
    return std::abs(left - right) <= tolerance;
}

double flowToleranceM3PerH(double left, double right)
{
    // The paired epanet-js-exported INP is parsed through EPANET's source-unit
    // conversion before both networks are solved in canonical AOWIS units.
    // Keep a tight absolute floor for small flows plus 100 ppm (0.01%) for
    // solver-level roundoff at larger flows. The two equivalence paths deliberately enter
    // EPANET through different source-unit systems before returning to canonical
    // AOWIS units, so bit-identical link flows are not expected.
    constexpr double absolute_tolerance_m3_per_h = 1.0e-4;
    constexpr double relative_tolerance = 1.0e-4;
    const double magnitude = std::max(std::abs(left), std::abs(right));
    return std::max(absolute_tolerance_m3_per_h, magnitude * relative_tolerance);
}

bool nearFlow(double left, double right)
{
    return near(left, right, flowToleranceM3PerH(left, right));
}

double headToleranceM(double left, double right)
{
    // These two equivalence paths deliberately enter EPANET through different
    // source-unit systems. Keep a 10 micrometre absolute floor for small
    // values, plus 0.5 ppm relative tolerance for unit-conversion/solver
    // roundoff at larger heads. This remains far below engineering-scale
    // hydraulic differences while avoiding magnitude-dependent false failures.
    constexpr double absolute_tolerance_m = 1.0e-5;
    constexpr double relative_tolerance = 5.0e-7;
    const double magnitude = std::max(std::abs(left), std::abs(right));
    return std::max(absolute_tolerance_m, magnitude * relative_tolerance);
}

bool nearHead(double left, double right)
{
    return near(left, right, headToleranceM(left, right));
}

bool prepareReferenceInpPath(
    const QString &reference_inp_path,
    QTemporaryFile &temporary_file,
    QString &usable_path,
    QString &error)
{
    if (!reference_inp_path.startsWith(QStringLiteral(":")))
    {
        usable_path = reference_inp_path;
        return true;
    }

    QFile resource_file(reference_inp_path);
    if (!resource_file.open(QIODevice::ReadOnly))
    {
        error = QStringLiteral("Could not open paired epanet-js INP resource '%1'")
            .arg(reference_inp_path);
        return false;
    }

    temporary_file.setFileTemplate(
        QDir::tempPath() + QStringLiteral("/aowis-epanet-js-reference-XXXXXX.inp"));
    temporary_file.setAutoRemove(true);
    if (!temporary_file.open())
    {
        error = QStringLiteral("Could not create temporary file for paired epanet-js INP resource");
        return false;
    }

    const QByteArray contents = resource_file.readAll();
    if (temporary_file.write(contents) != contents.size() || !temporary_file.flush())
    {
        error = QStringLiteral("Could not materialize paired epanet-js INP resource");
        return false;
    }

    usable_path = temporary_file.fileName();
    temporary_file.close();
    return true;
}

bool compareTimeline(
    const EpanetResultRun &reference_run,
    const EpanetResultRun &converted_run,
    EpanetJsHydraulicEquivalenceResult &comparison)
{
    if (!reference_run.status.success)
    {
        comparison.error = QStringLiteral("Paired epanet-js INP hydraulic run failed: %1")
            .arg(reference_run.status.message);
        return false;
    }
    if (!converted_run.status.success)
    {
        comparison.error = QStringLiteral("Converted-network hydraulic run failed: %1")
            .arg(converted_run.status.message);
        return false;
    }
    if (reference_run.result_timeline.results.size()
        != converted_run.result_timeline.results.size())
    {
        comparison.error = QStringLiteral("Hydraulic timestep count differs: reference=%1 converted=%2")
            .arg(reference_run.result_timeline.results.size())
            .arg(converted_run.result_timeline.results.size());
        return false;
    }

    constexpr double speed_tolerance = 1.0e-8;

    for (qsizetype timestep = 0;
         timestep < reference_run.result_timeline.results.size();
         ++timestep)
    {
        const HydraulicSimulationResult &reference =
            reference_run.result_timeline.results.at(timestep);
        const HydraulicSimulationResult &converted =
            converted_run.result_timeline.results.at(timestep);
        if (reference.time_elapsed_s != converted.time_elapsed_s)
        {
            comparison.error = QStringLiteral("Hydraulic timestep time differs at index %1")
                .arg(timestep);
            return false;
        }
        comparison.timesteps_compared++;

        for (const HydraulicSimulationResultNodeJunction &expected : reference.nodes_junctions)
        {
            const HydraulicSimulationResultNodeJunction *actual = junctionResultById(
                converted, expected.id);
            if (actual == nullptr)
            {
                comparison.error = QStringLiteral(
                    "Junction result missing at t=%1 s, id=%2")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id);
                return false;
            }

            if (!nearHead(actual->hydraulic_head_m, expected.hydraulic_head_m))
            {
                comparison.error = QStringLiteral(
                    "Junction hydraulic-head mismatch at t=%1 s, id=%2: reference=%3 m converted=%4 m delta=%5 m tolerance=%6 m")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.hydraulic_head_m, 0, 'g', 17)
                    .arg(actual->hydraulic_head_m, 0, 'g', 17)
                    .arg(actual->hydraulic_head_m - expected.hydraulic_head_m, 0, 'g', 17)
                    .arg(headToleranceM(actual->hydraulic_head_m, expected.hydraulic_head_m), 0, 'g', 17);
                return false;
            }

            if (!nearHead(actual->pressure_head_m, expected.pressure_head_m))
            {
                comparison.error = QStringLiteral(
                    "Junction pressure-head mismatch at t=%1 s, id=%2: reference=%3 m converted=%4 m delta=%5 m tolerance=%6 m")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.pressure_head_m, 0, 'g', 17)
                    .arg(actual->pressure_head_m, 0, 'g', 17)
                    .arg(actual->pressure_head_m - expected.pressure_head_m, 0, 'g', 17)
                    .arg(headToleranceM(actual->pressure_head_m, expected.pressure_head_m), 0, 'g', 17);
                return false;
            }

            if (!nearFlow(actual->demand_delivered_m3_per_h, expected.demand_delivered_m3_per_h))
            {
                comparison.error = QStringLiteral(
                    "Junction delivered-demand mismatch at t=%1 s, id=%2: reference=%3 m3/h converted=%4 m3/h delta=%5 m3/h tolerance=%6 m3/h")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.demand_delivered_m3_per_h, 0, 'g', 17)
                    .arg(actual->demand_delivered_m3_per_h, 0, 'g', 17)
                    .arg(actual->demand_delivered_m3_per_h - expected.demand_delivered_m3_per_h, 0, 'g', 17)
                    .arg(flowToleranceM3PerH(
                             actual->demand_delivered_m3_per_h,
                             expected.demand_delivered_m3_per_h),
                         0, 'g', 17);
                return false;
            }

            comparison.hydraulic_values_compared += 3;
        }

        for (const HydraulicSimulationResultNodeReservoir &expected : reference.nodes_reservoirs)
        {
            const HydraulicSimulationResultNodeReservoir *actual = reservoirResultById(
                converted, expected.id);
            if (actual == nullptr)
            {
                comparison.error = QStringLiteral(
                    "Reservoir result missing at t=%1 s, id=%2")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id);
                return false;
            }

            if (!nearHead(actual->hydraulic_head_m, expected.hydraulic_head_m))
            {
                comparison.error = QStringLiteral(
                    "Reservoir hydraulic-head mismatch at t=%1 s, id=%2: reference=%3 m converted=%4 m delta=%5 m tolerance=%6 m")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.hydraulic_head_m, 0, 'g', 17)
                    .arg(actual->hydraulic_head_m, 0, 'g', 17)
                    .arg(actual->hydraulic_head_m - expected.hydraulic_head_m, 0, 'g', 17)
                    .arg(headToleranceM(actual->hydraulic_head_m, expected.hydraulic_head_m), 0, 'g', 17);
                return false;
            }

            if (!nearFlow(actual->net_demand_m3_per_h, expected.net_demand_m3_per_h))
            {
                comparison.error = QStringLiteral(
                    "Reservoir net-flow mismatch at t=%1 s, id=%2: reference=%3 m3/h converted=%4 m3/h delta=%5 m3/h tolerance=%6 m3/h")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.net_demand_m3_per_h, 0, 'g', 17)
                    .arg(actual->net_demand_m3_per_h, 0, 'g', 17)
                    .arg(actual->net_demand_m3_per_h - expected.net_demand_m3_per_h, 0, 'g', 17)
                    .arg(flowToleranceM3PerH(
                             actual->net_demand_m3_per_h,
                             expected.net_demand_m3_per_h),
                         0, 'g', 17);
                return false;
            }

            comparison.hydraulic_values_compared += 2;
        }

        for (const HydraulicSimulationResultNodeTank &expected : reference.nodes_tanks)
        {
            const HydraulicSimulationResultNodeTank *actual = tankResultById(
                converted, expected.id);
            if (actual == nullptr)
            {
                comparison.error = QStringLiteral(
                    "Tank result missing at t=%1 s, id=%2")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id);
                return false;
            }

            if (!nearHead(actual->water_level_m, expected.water_level_m))
            {
                comparison.error = QStringLiteral(
                    "Tank water-level mismatch at t=%1 s, id=%2: reference=%3 m converted=%4 m delta=%5 m tolerance=%6 m")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.water_level_m, 0, 'g', 17)
                    .arg(actual->water_level_m, 0, 'g', 17)
                    .arg(actual->water_level_m - expected.water_level_m, 0, 'g', 17)
                    .arg(headToleranceM(actual->water_level_m, expected.water_level_m), 0, 'g', 17);
                return false;
            }

            if (!nearHead(actual->hydraulic_head_m, expected.hydraulic_head_m))
            {
                comparison.error = QStringLiteral(
                    "Tank hydraulic-head mismatch at t=%1 s, id=%2: reference=%3 m converted=%4 m delta=%5 m tolerance=%6 m")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.hydraulic_head_m, 0, 'g', 17)
                    .arg(actual->hydraulic_head_m, 0, 'g', 17)
                    .arg(actual->hydraulic_head_m - expected.hydraulic_head_m, 0, 'g', 17)
                    .arg(headToleranceM(actual->hydraulic_head_m, expected.hydraulic_head_m), 0, 'g', 17);
                return false;
            }

            if (!nearFlow(actual->net_demand_m3_per_h, expected.net_demand_m3_per_h))
            {
                comparison.error = QStringLiteral(
                    "Tank net-flow mismatch at t=%1 s, id=%2: reference=%3 m3/h converted=%4 m3/h delta=%5 m3/h tolerance=%6 m3/h")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.net_demand_m3_per_h, 0, 'g', 17)
                    .arg(actual->net_demand_m3_per_h, 0, 'g', 17)
                    .arg(actual->net_demand_m3_per_h - expected.net_demand_m3_per_h, 0, 'g', 17)
                    .arg(flowToleranceM3PerH(
                             actual->net_demand_m3_per_h,
                             expected.net_demand_m3_per_h),
                         0, 'g', 17);
                return false;
            }

            comparison.hydraulic_values_compared += 3;
        }

        for (const HydraulicSimulationResultLinkPipe &expected : reference.links_pipes)
        {
            const HydraulicSimulationResultLinkPipe *actual = pipeResultById(
                converted, expected.id);
            if (actual == nullptr)
            {
                comparison.error = QStringLiteral(
                    "Pipe result missing at t=%1 s, id=%2")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id);
                return false;
            }

            if (!nearFlow(actual->flow_m3_per_h, expected.flow_m3_per_h))
            {
                comparison.error = QStringLiteral(
                    "Pipe flow mismatch at t=%1 s, id=%2: reference=%3 m3/h converted=%4 m3/h delta=%5 m3/h tolerance=%6 m3/h")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.flow_m3_per_h, 0, 'g', 17)
                    .arg(actual->flow_m3_per_h, 0, 'g', 17)
                    .arg(actual->flow_m3_per_h - expected.flow_m3_per_h, 0, 'g', 17)
                    .arg(flowToleranceM3PerH(actual->flow_m3_per_h, expected.flow_m3_per_h),
                         0, 'g', 17);
                return false;
            }

            if (!nearHead(actual->head_loss_m, expected.head_loss_m))
            {
                comparison.error = QStringLiteral(
                    "Pipe head-loss mismatch at t=%1 s, id=%2: reference=%3 m converted=%4 m delta=%5 m tolerance=%6 m")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.head_loss_m, 0, 'g', 17)
                    .arg(actual->head_loss_m, 0, 'g', 17)
                    .arg(actual->head_loss_m - expected.head_loss_m, 0, 'g', 17)
                    .arg(headToleranceM(actual->head_loss_m, expected.head_loss_m), 0, 'g', 17);
                return false;
            }

            if (actual->open != expected.open)
            {
                comparison.error = QStringLiteral(
                    "Pipe status mismatch at t=%1 s, id=%2: reference_open=%3 converted_open=%4")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.open ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(actual->open ? QStringLiteral("true") : QStringLiteral("false"));
                return false;
            }

            comparison.hydraulic_values_compared += 3;
        }

        for (const HydraulicSimulationResultLinkPump &expected : reference.links_pumps)
        {
            const HydraulicSimulationResultLinkPump *actual = pumpResultById(
                converted, expected.id);
            if (actual == nullptr)
            {
                comparison.error = QStringLiteral(
                    "Pump result missing at t=%1 s, id=%2")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id);
                return false;
            }

            if (!nearFlow(actual->flow_m3_per_h, expected.flow_m3_per_h))
            {
                comparison.error = QStringLiteral(
                    "Pump flow mismatch at t=%1 s, id=%2: reference=%3 m3/h converted=%4 m3/h delta=%5 m3/h tolerance=%6 m3/h")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.flow_m3_per_h, 0, 'g', 17)
                    .arg(actual->flow_m3_per_h, 0, 'g', 17)
                    .arg(actual->flow_m3_per_h - expected.flow_m3_per_h, 0, 'g', 17)
                    .arg(flowToleranceM3PerH(
                             actual->flow_m3_per_h,
                             expected.flow_m3_per_h),
                         0, 'g', 17);
                return false;
            }

            // Pump head gain is a difference between two node heads, so allow
            // twice the corresponding combined head tolerance for this derived
            // quantity.
            const double pump_head_gain_tolerance_m = 2.0 * headToleranceM(
                actual->head_gain_m, expected.head_gain_m);
            if (!near(actual->head_gain_m, expected.head_gain_m,
                      pump_head_gain_tolerance_m))
            {
                comparison.error = QStringLiteral(
                    "Pump head-gain mismatch at t=%1 s, id=%2: reference=%3 m converted=%4 m delta=%5 m tolerance=%6 m")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.head_gain_m, 0, 'g', 17)
                    .arg(actual->head_gain_m, 0, 'g', 17)
                    .arg(actual->head_gain_m - expected.head_gain_m, 0, 'g', 17)
                    .arg(pump_head_gain_tolerance_m, 0, 'g', 17);
                return false;
            }

            if (actual->open != expected.open)
            {
                comparison.error = QStringLiteral(
                    "Pump status mismatch at t=%1 s, id=%2: reference_open=%3 converted_open=%4")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.open ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(actual->open ? QStringLiteral("true") : QStringLiteral("false"));
                return false;
            }

            // EPANET's INP/[STATUS] representation can report EN_SETTING=0 for a
            // closed pump, while the direct AOWIS model retains the pump's nominal
            // speed ratio (for example 1.0) together with open=false. Those are the
            // same hydraulic state. Speed is meaningful for equivalence only while
            // the pump is actually operating.
            if (actual->open
                && !near(actual->speed_ratio, expected.speed_ratio, speed_tolerance))
            {
                comparison.error = QStringLiteral(
                    "Pump speed mismatch at t=%1 s, id=%2: reference=%3 converted=%4 delta=%5 tolerance=%6")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.speed_ratio, 0, 'g', 17)
                    .arg(actual->speed_ratio, 0, 'g', 17)
                    .arg(actual->speed_ratio - expected.speed_ratio, 0, 'g', 17)
                    .arg(speed_tolerance, 0, 'g', 17);
                return false;
            }

            comparison.hydraulic_values_compared += 4;
        }

        for (const HydraulicSimulationResultLinkValve &expected : reference.links_valves)
        {
            const HydraulicSimulationResultLinkValve *actual = valveResultById(
                converted, expected.id);
            if (actual == nullptr)
            {
                comparison.error = QStringLiteral(
                    "Valve result missing at t=%1 s, id=%2")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id);
                return false;
            }

            if (!nearFlow(actual->flow_m3_per_h, expected.flow_m3_per_h))
            {
                comparison.error = QStringLiteral(
                    "Valve flow mismatch at t=%1 s, id=%2: reference=%3 m3/h converted=%4 m3/h delta=%5 m3/h tolerance=%6 m3/h")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.flow_m3_per_h, 0, 'g', 17)
                    .arg(actual->flow_m3_per_h, 0, 'g', 17)
                    .arg(actual->flow_m3_per_h - expected.flow_m3_per_h, 0, 'g', 17)
                    .arg(flowToleranceM3PerH(actual->flow_m3_per_h, expected.flow_m3_per_h),
                         0, 'g', 17);
                return false;
            }

            if (!nearHead(actual->head_loss_m, expected.head_loss_m))
            {
                comparison.error = QStringLiteral(
                    "Valve head-loss mismatch at t=%1 s, id=%2: reference=%3 m converted=%4 m delta=%5 m tolerance=%6 m")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.head_loss_m, 0, 'g', 17)
                    .arg(actual->head_loss_m, 0, 'g', 17)
                    .arg(actual->head_loss_m - expected.head_loss_m, 0, 'g', 17)
                    .arg(headToleranceM(actual->head_loss_m, expected.head_loss_m), 0, 'g', 17);
                return false;
            }

            if (actual->open != expected.open)
            {
                comparison.error = QStringLiteral(
                    "Valve open-state mismatch at t=%1 s, id=%2: reference_open=%3 converted_open=%4")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.open ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(actual->open ? QStringLiteral("true") : QStringLiteral("false"));
                return false;
            }

            if (actual->active != expected.active)
            {
                comparison.error = QStringLiteral(
                    "Valve active-state mismatch at t=%1 s, id=%2: reference_active=%3 converted_active=%4")
                    .arg(reference.time_elapsed_s)
                    .arg(expected.id)
                    .arg(expected.active ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(actual->active ? QStringLiteral("true") : QStringLiteral("false"));
                return false;
            }

            comparison.hydraulic_values_compared += 4;
        }
    }

    return true;
}
}

EpanetJsModelInputEquivalenceResult compareEpanetJsModelInputs(
    const QString &reference_inp_path,
    const NetworkHydraulic &converted_network)
{
    EpanetJsModelInputEquivalenceResult comparison;
    if (reference_inp_path.trimmed().isEmpty())
    {
        comparison.error = QStringLiteral("Paired epanet-js INP reference path is empty");
        return comparison;
    }

    QTemporaryFile temporary_reference_file;
    QString usable_reference_path;
    if (!prepareReferenceInpPath(
            reference_inp_path, temporary_reference_file, usable_reference_path, comparison.error))
    {
        return comparison;
    }

    const EpanetResultImport reference_import = EpanetRunner().importInp(usable_reference_path);
    if (!reference_import.complete || !reference_import.status.success)
    {
        comparison.error = QStringLiteral("Paired epanet-js INP reference import failed: %1")
            .arg(reference_import.status.message);
        return comparison;
    }

    EpanetRunRequest converted_request;
    converted_request.network = converted_network;

    const EpanetResultInp reference_inp = EpanetRunner().retrieveInp(reference_import.request);
    if (!reference_inp.status.success)
    {
        comparison.error = QStringLiteral("Could not normalize paired epanet-js INP reference: %1")
            .arg(reference_inp.status.message);
        return comparison;
    }
    const EpanetResultInp converted_inp = EpanetRunner().retrieveInp(converted_request);
    if (!converted_inp.status.success)
    {
        comparison.error = QStringLiteral("Could not normalize converted epanet-js model: %1")
            .arg(converted_inp.status.message);
        return comparison;
    }

    const QMap<QString, QStringList> reference_sections = canonicalHydraulicSections(
        reference_inp.inp_text);
    const QMap<QString, QStringList> converted_sections = canonicalHydraulicSections(
        converted_inp.inp_text);

    const QStringList section_order = hydraulicSectionOrder();
    for (const QString &section_name : section_order)
    {
        const QStringList reference_lines = reference_sections.value(section_name);
        const QStringList converted_lines = converted_sections.value(section_name);
        comparison.sections_compared++;

        if (reference_lines.size() != converted_lines.size())
        {
            comparison.error = QStringLiteral(
                "Normalized solver input differs in [%1]: reference has %2 line(s), converted has %3 line(s)")
                .arg(section_name)
                .arg(reference_lines.size())
                .arg(converted_lines.size());
            return comparison;
        }

        for (qsizetype line_index = 0; line_index < reference_lines.size(); ++line_index)
        {
            comparison.lines_compared++;
            if (reference_lines.at(line_index) != converted_lines.at(line_index))
            {
                comparison.error = QStringLiteral(
                    "Normalized solver input differs in [%1] line %2:\n  reference: %3\n  converted: %4")
                    .arg(section_name)
                    .arg(line_index + 1)
                    .arg(reference_lines.at(line_index), converted_lines.at(line_index));
                return comparison;
            }
        }
    }

    comparison.success = true;
    return comparison;
}

EpanetJsHydraulicEquivalenceResult compareEpanetJsHydraulics(
    const QString &reference_inp_path,
    const NetworkHydraulic &converted_network)
{
    EpanetJsHydraulicEquivalenceResult comparison;

    if (reference_inp_path.trimmed().isEmpty())
    {
        comparison.error = QStringLiteral("Paired epanet-js INP reference path is empty");
        return comparison;
    }

    QTemporaryFile temporary_reference_file;
    QString usable_reference_path;
    if (!prepareReferenceInpPath(
            reference_inp_path, temporary_reference_file, usable_reference_path, comparison.error))
    {
        return comparison;
    }

    const EpanetResultImport reference_import = EpanetRunner().importInp(usable_reference_path);
    if (!reference_import.complete || !reference_import.status.success)
    {
        comparison.error = QStringLiteral("Paired epanet-js INP reference import failed: %1")
            .arg(reference_import.status.message);
        return comparison;
    }

    const EpanetResultRun reference_run = EpanetRunner().run(reference_import.request);

    EpanetRunRequest converted_request;
    converted_request.network = converted_network;
    const EpanetResultRun converted_run = EpanetRunner().run(converted_request);

    if (!compareTimeline(reference_run, converted_run, comparison))
        return comparison;

    comparison.success = true;
    return comparison;
}
