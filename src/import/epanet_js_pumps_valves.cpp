#include "import/epanet_js_pumps_valves.h"
#include <aowis/model/units/conversion.h>
#include "import/epanet_js_patterns_curves.h"
#include "import/epanet_js_conversion_common.h"
#include "import/epanet_js_schema.h"
#include "import/epanet_js_units.h"
#include "import/epanet_js_geometry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <optional>

namespace EpanetJsPumpsValves
{
using namespace EpanetJsConversionCommon;
using namespace EpanetJsSchema;
using namespace EpanetJsUnits;
using namespace EpanetJsGeometry;

std::optional<double> powerToKw(double value, const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("kw") || normalized == QStringLiteral("kilowatt")
        || normalized == QStringLiteral("kilowatts"))
        return value;
    if (normalized == QStringLiteral("hp") || normalized == QStringLiteral("horsepower"))
        return aowis::units::mechanicalHorsepowerToKilowatts(value);
    return std::nullopt;
}

void importLinkVertices(
    const QVariantMap &row,
    qint64 source_id,
    const QString &table_name,
    const QString &entity_name,
    QList<HydraulicLinkVertex> &vertices,
    EpanetJsProjectConversionResult &result)
{
    const std::optional<QList<CoordinateWGS84>> coordinates = linkCoordinates(
        row, source_id, table_name, entity_name, result);
    if (!coordinates.has_value() || coordinates->size() < 2)
        return;

    for (qsizetype index = 1; index + 1 < coordinates->size(); ++index)
    {
        HydraulicLinkVertex vertex;
        vertex.coordinate_wgs84 = coordinates->at(index);
        vertices.append(vertex);
    }
}


bool importPumpStatus(
    const QVariantMap &row,
    qint64 source_id,
    HydraulicLinkPump &pump,
    EpanetJsProjectConversionResult &result)
{
    if (!row.contains(QStringLiteral("initial_status"))
        || row.value(QStringLiteral("initial_status")).isNull())
        return true;

    QString status = row.value(QStringLiteral("initial_status")).toString().trimmed().toLower();
    status.remove(QLatin1Char('_'));
    status.remove(QLatin1Char('-'));
    status.remove(QLatin1Char(' '));

    if (status == QStringLiteral("on") || status == QStringLiteral("open"))
    {
        pump.initial_status = HydraulicLinkPumpInitialStatus::On;
        return true;
    }
    if (status == QStringLiteral("off") || status == QStringLiteral("closed"))
    {
        pump.initial_status = HydraulicLinkPumpInitialStatus::Off;
        return true;
    }

    appendDiagnostic(
        result,
        EpanetJsConversionDiagnosticSeverity::Error,
        QStringLiteral("unknown-pump-status"),
        QStringLiteral("epanet-js pump id %1 has unknown initial_status '%2'.")
            .arg(source_id)
            .arg(row.value(QStringLiteral("initial_status")).toString()),
        QStringLiteral("pumps"),
        source_id);
    return false;
}

QUuid importInlinePumpHeadCurve(
    const QVariantMap &row,
    qint64 source_id,
    const QString &pump_id,
    const QString &flow_unit,
    const QString &head_unit,
    EpanetJsProjectConversionResult &result)
{
    const QString points_text = row.value(QStringLiteral("curve_points")).toString().trimmed();
    if (points_text.isEmpty())
        return {};

    const std::optional<QList<QPair<double, double>>> source_points = EpanetJsPatternsCurves::curvePoints(points_text);
    if (!source_points.has_value() || source_points->isEmpty())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-pump-curve-points"),
            QStringLiteral("epanet-js pump id %1 has invalid or empty curve_points data.")
                .arg(source_id),
            QStringLiteral("pumps"),
            source_id);
        return {};
    }

    HydraulicCurvePumpHead curve;
    curve.id = pump_id;
    curve.uuid = uuidV5(
        result.network.uuid,
        QStringLiteral("epanet-js/pumps/%1/head-curve").arg(source_id).toUtf8());

    for (const QPair<double, double> &point : *source_points)
    {
        const std::optional<double> flow = flowToM3PerH(point.first, flow_unit);
        const std::optional<double> head = lengthToM(point.second, head_unit);
        if (!flow.has_value() || !head.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unsupported-pump-curve-unit"),
                QStringLiteral("AOWIS cannot convert the inline head curve for epanet-js pump id %1.")
                    .arg(source_id),
                QStringLiteral("pumps"),
                source_id);
            return {};
        }
        curve.points.append({*flow, *head});
    }

    result.network.curves_pump_head.append(curve);
    return curve.uuid;
}

HydraulicLinkPumpDefinitionType pumpDefinitionForCurve(
    const QString &definition_type,
    int point_count)
{
    QString normalized = definition_type.trimmed().toLower();
    normalized.remove(QLatin1Char('_'));
    normalized.remove(QLatin1Char('-'));
    normalized.remove(QLatin1Char(' '));

    if (normalized.contains(QStringLiteral("library")))
        return HydraulicLinkPumpDefinitionType::Library;
    if (point_count == 1)
        return HydraulicLinkPumpDefinitionType::OnePointCurve;
    if (point_count == 3)
        return HydraulicLinkPumpDefinitionType::ThreePointCurve;
    return HydraulicLinkPumpDefinitionType::MultiPointCurve;
}

void importPumps(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("pumps"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString flow_unit = units.value(QStringLiteral("flow")).toString().trimmed();
    const QString power_unit = firstUnit(units, QStringList{QStringLiteral("power")});
    const QString head_unit = firstUnit(units, QStringList{QStringLiteral("head")});

    QHash<QUuid, int> head_curve_points;
    for (const HydraulicCurvePumpHead &curve : result.network.curves_pump_head)
    {
        if (!head_curve_points.contains(curve.uuid))
            head_curve_points.insert(curve.uuid, curve.points.size());
    }
    const QSet<QUuid> efficiency_curve_uuids = uuidMembershipIndex(result.network.curves_pump_efficiency);
    const QSet<QUuid> pattern_uuids = uuidMembershipIndex(result.network.patterns_time);

    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value() || !result.id_map.contains(QStringLiteral("pumps"), *source_id))
            continue;

        const std::optional<qint64> start_node_id = integerValue(
            row.value(QStringLiteral("start_node_id")));
        const std::optional<qint64> end_node_id = integerValue(
            row.value(QStringLiteral("end_node_id")));
        if (!start_node_id.has_value() || !end_node_id.has_value())
            continue;

        const QUuid from_uuid = result.id_map.nodeUuid(*start_node_id);
        const QUuid to_uuid = result.id_map.nodeUuid(*end_node_id);
        if (from_uuid.isNull() || to_uuid.isNull())
            continue;

        HydraulicLinkPump pump;
        pump.id = importedEntityId(row, QStringLiteral("pumps"), *source_id, result);
        pump.uuid = result.id_map.uuidFor(QStringLiteral("pumps"), *source_id);
        pump.node_uuid_from = from_uuid;
        pump.node_uuid_to = to_uuid;
        pump.metadata.enabled = !row.contains(QStringLiteral("is_active"))
            || row.value(QStringLiteral("is_active")).toInt() != 0;

        importLinkVertices(
            row, *source_id, QStringLiteral("pumps"), QStringLiteral("pump"),
            pump.vertices, result);
        importPumpStatus(row, *source_id, pump, result);

        if (row.contains(QStringLiteral("speed")) && !row.value(QStringLiteral("speed")).isNull())
        {
            const std::optional<double> speed = finiteVariantDouble(row.value(QStringLiteral("speed")));
            if (!speed.has_value() || *speed < 0.0)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-pump-speed"),
                    QStringLiteral("epanet-js pump id %1 has invalid speed.").arg(*source_id),
                    QStringLiteral("pumps"),
                    *source_id);
            }
            else
            {
                pump.initial_speed_ratio = *speed;
            }
        }

        if (row.contains(QStringLiteral("speed_pattern_id"))
            && !row.value(QStringLiteral("speed_pattern_id")).isNull())
        {
            const std::optional<qint64> pattern_id = integerValue(
                row.value(QStringLiteral("speed_pattern_id")));
            if (!pattern_id.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-pump-speed-pattern-reference"),
                    QStringLiteral("epanet-js pump id %1 has an invalid speed pattern reference.")
                        .arg(*source_id),
                    QStringLiteral("pumps"),
                    *source_id);
            }
            else if (*pattern_id > 0)
            {
                const QUuid pattern_uuid = result.id_map.uuidFor(
                    QStringLiteral("patterns"), *pattern_id);
                if (pattern_uuid.isNull() || !pattern_uuids.contains(pattern_uuid))
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("missing-pump-speed-pattern-reference"),
                        QStringLiteral("epanet-js pump id %1 references a missing speed pattern.")
                            .arg(*source_id),
                        QStringLiteral("pumps"),
                        *source_id);
                }
                else
                {
                    pump.speed_pattern_uuid = pattern_uuid;
                }
            }
        }

        QString definition_type = row.value(QStringLiteral("definition_type")).toString();
        QString normalized_definition = definition_type.trimmed().toLower();
        normalized_definition.remove(QLatin1Char('_'));
        normalized_definition.remove(QLatin1Char('-'));
        normalized_definition.remove(QLatin1Char(' '));

        const bool explicitly_constant =
            normalized_definition == QStringLiteral("constantpower")
            || normalized_definition == QStringLiteral("power");
        const std::optional<qint64> curve_reference_id =
            row.contains(QStringLiteral("curve_id")) && !row.value(QStringLiteral("curve_id")).isNull()
            ? integerValue(row.value(QStringLiteral("curve_id")))
            : std::nullopt;
        const bool has_curve_reference =
            curve_reference_id.has_value() && *curve_reference_id > 0;
        const bool has_inline_curve =
            row.contains(QStringLiteral("curve_points"))
            && !row.value(QStringLiteral("curve_points")).toString().trimmed().isEmpty();
        const bool has_power =
            row.contains(QStringLiteral("power")) && !row.value(QStringLiteral("power")).isNull();

        if (explicitly_constant || (has_power && !has_curve_reference && !has_inline_curve))
        {
            const std::optional<double> source_power = finiteVariantDouble(
                row.value(QStringLiteral("power")));
            const std::optional<double> power_kw = source_power.has_value()
                ? powerToKw(*source_power, power_unit)
                : std::nullopt;
            if (!power_kw.has_value() || *power_kw <= 0.0)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-pump-power"),
                    QStringLiteral("AOWIS cannot convert the constant power for epanet-js pump id %1 with unit '%2'.")
                        .arg(*source_id)
                        .arg(power_unit),
                    QStringLiteral("pumps"),
                    *source_id);
            }
            else
            {
                pump.definition_type = HydraulicLinkPumpDefinitionType::ConstantPower;
                pump.constant_power_kw = *power_kw;
            }
        }
        else
        {
            QUuid head_curve_uuid;
            if (has_curve_reference)
            {
                head_curve_uuid = result.id_map.uuidFor(
                    QStringLiteral("curves"), *curve_reference_id);
                if (head_curve_uuid.isNull()
                    || !head_curve_points.contains(head_curve_uuid))
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("missing-pump-head-curve-reference"),
                        QStringLiteral("epanet-js pump id %1 references a missing or non-pump head curve.")
                            .arg(*source_id),
                        QStringLiteral("pumps"),
                        *source_id);
                }
            }
            else if (has_inline_curve)
            {
                head_curve_uuid = importInlinePumpHeadCurve(
                    row, *source_id, pump.id, flow_unit, head_unit, result);
                if (!head_curve_uuid.isNull() && !result.network.curves_pump_head.isEmpty())
                {
                    const HydraulicCurvePumpHead &curve = result.network.curves_pump_head.constLast();
                    if (!head_curve_points.contains(curve.uuid))
                        head_curve_points.insert(curve.uuid, curve.points.size());
                }
            }
            else
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-pump-definition"),
                    QStringLiteral("epanet-js pump id %1 has neither constant power nor a usable head curve.")
                        .arg(*source_id),
                    QStringLiteral("pumps"),
                    *source_id);
            }

            if (!head_curve_uuid.isNull())
            {
                pump.head_curve_uuid = head_curve_uuid;
                const int point_count = head_curve_points.value(head_curve_uuid, 0);
                if (point_count <= 0)
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("empty-pump-head-curve"),
                        QStringLiteral("epanet-js pump id %1 resolves to an empty head curve.")
                            .arg(*source_id),
                        QStringLiteral("pumps"),
                        *source_id);
                }
                else
                {
                    pump.definition_type = pumpDefinitionForCurve(definition_type, point_count);
                }
            }
        }

        if (row.contains(QStringLiteral("efficiency_curve_id"))
            && !row.value(QStringLiteral("efficiency_curve_id")).isNull())
        {
            const std::optional<qint64> curve_id = integerValue(
                row.value(QStringLiteral("efficiency_curve_id")));
            if (!curve_id.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-pump-efficiency-curve-reference"),
                    QStringLiteral("epanet-js pump id %1 has an invalid efficiency curve reference.")
                        .arg(*source_id),
                    QStringLiteral("pumps"),
                    *source_id);
            }
            else if (*curve_id > 0)
            {
                const QUuid curve_uuid = result.id_map.uuidFor(
                    QStringLiteral("curves"), *curve_id);
                if (curve_uuid.isNull()
                    || !efficiency_curve_uuids.contains(curve_uuid))
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("missing-pump-efficiency-curve-reference"),
                        QStringLiteral("epanet-js pump id %1 references a missing or non-efficiency curve.")
                            .arg(*source_id),
                        QStringLiteral("pumps"),
                        *source_id);
                }
                else
                {
                    pump.efficiency_input_type = HydraulicLinkPumpEfficiencyInputType::Curve;
                    pump.efficiency_curve_uuid = curve_uuid;
                }
            }
        }

        std::optional<double> energy_price;
        if (row.contains(QStringLiteral("energy_price"))
            && !row.value(QStringLiteral("energy_price")).isNull())
        {
            energy_price = finiteVariantDouble(row.value(QStringLiteral("energy_price")));
            if (!energy_price.has_value() || *energy_price < 0.0)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-pump-energy-price"),
                    QStringLiteral("epanet-js pump id %1 has an invalid energy price.")
                        .arg(*source_id),
                    QStringLiteral("pumps"),
                    *source_id);
                energy_price = std::nullopt;
            }
        }

        if (row.contains(QStringLiteral("energy_price_pattern_id"))
            && !row.value(QStringLiteral("energy_price_pattern_id")).isNull())
        {
            const std::optional<qint64> pattern_id = integerValue(
                row.value(QStringLiteral("energy_price_pattern_id")));
            if (!pattern_id.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-pump-energy-pattern-reference"),
                    QStringLiteral("epanet-js pump id %1 has an invalid energy-price pattern reference.")
                        .arg(*source_id),
                    QStringLiteral("pumps"),
                    *source_id);
            }
            else if (*pattern_id > 0)
            {
                const QUuid pattern_uuid = result.id_map.uuidFor(
                    QStringLiteral("patterns"), *pattern_id);
                if (pattern_uuid.isNull()
                    || !pattern_uuids.contains(pattern_uuid))
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("missing-pump-energy-pattern-reference"),
                        QStringLiteral("epanet-js pump id %1 references a missing energy-price pattern.")
                            .arg(*source_id),
                        QStringLiteral("pumps"),
                        *source_id);
                }
                else
                {
                    double base_price = energy_price.value_or(
                        result.network.options_energy.global_energy_price_per_kw_h);
                    if (base_price <= 0.0)
                        base_price = result.network.options_energy.global_energy_price_per_kw_h;
                    if (base_price > 0.0)
                    {
                        pump.energy_price_input_type = HydraulicLinkPumpEnergyPriceInputType::Pattern;
                        pump.energy_price_per_kw_h = base_price;
                        pump.price_pattern_uuid = pattern_uuid;
                    }
                    else
                    {
                        appendDiagnostic(
                            result,
                            EpanetJsConversionDiagnosticSeverity::Warning,
                            QStringLiteral("zero-effective-pump-energy-price"),
                            QStringLiteral("epanet-js pump id %1 has an energy-price pattern with zero effective base price; the no-effect pattern was omitted.")
                                .arg(*source_id),
                            QStringLiteral("pumps"),
                            *source_id);
                    }
                }
            }
        }
        else if (energy_price.has_value() && *energy_price > 0.0)
        {
            pump.energy_price_input_type = HydraulicLinkPumpEnergyPriceInputType::Constant;
            pump.energy_price_per_kw_h = *energy_price;
        }
        else if (energy_price.has_value() && *energy_price == 0.0)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Warning,
                QStringLiteral("zero-pump-energy-price-uses-global"),
                QStringLiteral("epanet-js pump id %1 has energy price 0; AOWIS/EPANET treats zero as use-global and imported it that way.")
                    .arg(*source_id),
                QStringLiteral("pumps"),
                *source_id);
        }

        result.network.links_pumps.append(pump);
    }
}

std::optional<HydraulicLinkValveType> valveType(const QString &value)
{
    QString normalized = value.trimmed().toLower();
    normalized.remove(QLatin1Char('_'));
    normalized.remove(QLatin1Char('-'));
    normalized.remove(QLatin1Char(' '));

    if (normalized == QStringLiteral("prv") || normalized == QStringLiteral("pressurereducingvalve"))
        return HydraulicLinkValveType::PRV;
    if (normalized == QStringLiteral("psv") || normalized == QStringLiteral("pressuresustainingvalve"))
        return HydraulicLinkValveType::PSV;
    if (normalized == QStringLiteral("fcv") || normalized == QStringLiteral("flowcontrolvalve"))
        return HydraulicLinkValveType::FCV;
    if (normalized == QStringLiteral("pbv") || normalized == QStringLiteral("pressurebreakervalve"))
        return HydraulicLinkValveType::PBV;
    if (normalized == QStringLiteral("tcv") || normalized == QStringLiteral("throttlecontrolvalve"))
        return HydraulicLinkValveType::TCV;
    if (normalized == QStringLiteral("gpv") || normalized == QStringLiteral("generalpurposevalve"))
        return HydraulicLinkValveType::GPV;
    if (normalized == QStringLiteral("pcv") || normalized == QStringLiteral("positionalcontrolvalve"))
        return HydraulicLinkValveType::PCV;
    return std::nullopt;
}

bool importValveStatus(
    const QVariantMap &row,
    qint64 source_id,
    HydraulicLinkValve &valve,
    EpanetJsProjectConversionResult &result)
{
    if (!row.contains(QStringLiteral("initial_status"))
        || row.value(QStringLiteral("initial_status")).isNull())
        return true;

    QString status = row.value(QStringLiteral("initial_status")).toString().trimmed().toLower();
    status.remove(QLatin1Char('_'));
    status.remove(QLatin1Char('-'));
    status.remove(QLatin1Char(' '));

    if (status == QStringLiteral("active"))
    {
        valve.initial_status = HydraulicLinkValveInitialStatus::Active;
        return true;
    }
    if (status == QStringLiteral("open"))
    {
        valve.initial_status = HydraulicLinkValveInitialStatus::Open;
        return true;
    }
    if (status == QStringLiteral("closed"))
    {
        valve.initial_status = HydraulicLinkValveInitialStatus::Closed;
        return true;
    }

    appendDiagnostic(
        result,
        EpanetJsConversionDiagnosticSeverity::Error,
        QStringLiteral("unknown-valve-status"),
        QStringLiteral("epanet-js valve id %1 has unknown initial_status '%2'.")
            .arg(source_id)
            .arg(row.value(QStringLiteral("initial_status")).toString()),
        QStringLiteral("valves"),
        source_id);
    return false;
}

void importValves(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("valves"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString diameter_unit = firstUnit(units, QStringList{QStringLiteral("diameter")});
    const QString pressure_unit = units.value(QStringLiteral("pressure")).toString().trimmed();
    const QString flow_unit = units.value(QStringLiteral("flow")).toString().trimmed();

    const QSet<QUuid> headloss_curve_uuids = uuidMembershipIndex(result.network.curves_valve_headloss);
    const QSet<QUuid> characteristic_curve_uuids = uuidMembershipIndex(result.network.curves_valve_characteristic);

    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value() || !result.id_map.contains(QStringLiteral("valves"), *source_id))
            continue;

        const std::optional<qint64> start_node_id = integerValue(
            row.value(QStringLiteral("start_node_id")));
        const std::optional<qint64> end_node_id = integerValue(
            row.value(QStringLiteral("end_node_id")));
        if (!start_node_id.has_value() || !end_node_id.has_value())
            continue;

        const QUuid from_uuid = result.id_map.nodeUuid(*start_node_id);
        const QUuid to_uuid = result.id_map.nodeUuid(*end_node_id);
        if (from_uuid.isNull() || to_uuid.isNull())
            continue;

        const std::optional<HydraulicLinkValveType> type = valveType(
            row.value(QStringLiteral("valve_kind")).toString());
        if (!type.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unknown-valve-type"),
                QStringLiteral("epanet-js valve id %1 has unsupported valve_kind '%2'.")
                    .arg(*source_id)
                    .arg(row.value(QStringLiteral("valve_kind")).toString()),
                QStringLiteral("valves"),
                *source_id);
            continue;
        }

        HydraulicLinkValve valve;
        valve.id = importedEntityId(row, QStringLiteral("valves"), *source_id, result);
        valve.uuid = result.id_map.uuidFor(QStringLiteral("valves"), *source_id);
        valve.node_uuid_from = from_uuid;
        valve.node_uuid_to = to_uuid;
        valve.type = *type;
        valve.metadata.enabled = !row.contains(QStringLiteral("is_active"))
            || row.value(QStringLiteral("is_active")).toInt() != 0;

        importLinkVertices(
            row, *source_id, QStringLiteral("valves"), QStringLiteral("valve"),
            valve.vertices, result);
        importValveStatus(row, *source_id, valve, result);

        const std::optional<double> source_diameter = finiteVariantDouble(
            row.value(QStringLiteral("diameter")));
        const std::optional<double> diameter = source_diameter.has_value()
            ? diameterToMm(*source_diameter, diameter_unit)
            : std::nullopt;
        if (!diameter.has_value() || *diameter <= 0.0)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-valve-diameter"),
                QStringLiteral("AOWIS cannot convert the diameter for epanet-js valve id %1 with unit '%2'.")
                    .arg(*source_id)
                    .arg(diameter_unit),
                QStringLiteral("valves"),
                *source_id);
        }
        else
        {
            valve.diameter_mm = *diameter;
        }

        if (row.contains(QStringLiteral("minor_loss")) && !row.value(QStringLiteral("minor_loss")).isNull())
        {
            const std::optional<double> minor_loss = finiteVariantDouble(
                row.value(QStringLiteral("minor_loss")));
            if (!minor_loss.has_value() || *minor_loss < 0.0)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-valve-minor-loss"),
                    QStringLiteral("epanet-js valve id %1 has invalid minor_loss.")
                        .arg(*source_id),
                    QStringLiteral("valves"),
                    *source_id);
            }
            else
            {
                valve.minor_loss_coefficient = *minor_loss;
            }
        }

        if (valve.type != HydraulicLinkValveType::GPV)
        {
            const std::optional<double> setting = finiteVariantDouble(
                row.value(QStringLiteral("setting")));
            if (!setting.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-valve-setting"),
                    QStringLiteral("epanet-js valve id %1 is missing a valid setting.")
                        .arg(*source_id),
                    QStringLiteral("valves"),
                    *source_id);
            }
            else
            {
                switch (valve.type)
                {
                case HydraulicLinkValveType::PRV:
                case HydraulicLinkValveType::PSV:
                case HydraulicLinkValveType::PBV:
                {
                    const std::optional<double> converted = pressureToHeadM(
                        *setting, pressure_unit, result.network.options_hydraulic.specific_gravity);
                    if (!converted.has_value())
                    {
                        appendDiagnostic(
                            result,
                            EpanetJsConversionDiagnosticSeverity::Error,
                            QStringLiteral("unsupported-valve-pressure-unit"),
                            QStringLiteral("AOWIS cannot convert the pressure setting for epanet-js valve id %1 with unit '%2'.")
                                .arg(*source_id)
                                .arg(pressure_unit),
                            QStringLiteral("valves"),
                            *source_id);
                    }
                    else
                    {
                        valve.setting_pressure_head_m = *converted;
                    }
                    break;
                }
                case HydraulicLinkValveType::FCV:
                {
                    const std::optional<double> converted = flowToM3PerH(*setting, flow_unit);
                    if (!converted.has_value())
                    {
                        appendDiagnostic(
                            result,
                            EpanetJsConversionDiagnosticSeverity::Error,
                            QStringLiteral("unsupported-valve-flow-unit"),
                            QStringLiteral("AOWIS cannot convert the flow setting for epanet-js valve id %1 with unit '%2'.")
                                .arg(*source_id)
                                .arg(flow_unit),
                            QStringLiteral("valves"),
                            *source_id);
                    }
                    else
                    {
                        valve.setting_flow_m3_per_h = *converted;
                    }
                    break;
                }
                case HydraulicLinkValveType::TCV:
                    valve.setting_loss_coefficient = *setting;
                    break;
                case HydraulicLinkValveType::PCV:
                    valve.setting_position_percent = *setting;
                    break;
                case HydraulicLinkValveType::GPV:
                    break;
                }
            }
        }

        if (row.contains(QStringLiteral("curve_id")) && !row.value(QStringLiteral("curve_id")).isNull())
        {
            const std::optional<qint64> curve_id = integerValue(row.value(QStringLiteral("curve_id")));
            const QUuid curve_uuid = curve_id.has_value()
                ? result.id_map.uuidFor(QStringLiteral("curves"), *curve_id)
                : QUuid();

            if (valve.type == HydraulicLinkValveType::GPV)
            {
                if (!curve_id.has_value() || *curve_id <= 0 || curve_uuid.isNull()
                    || !headloss_curve_uuids.contains(curve_uuid))
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("missing-gpv-curve-reference"),
                        QStringLiteral("epanet-js GPV id %1 references a missing or non-headloss curve.")
                            .arg(*source_id),
                        QStringLiteral("valves"),
                        *source_id);
                }
                else
                {
                    valve.head_loss_curve_uuid = curve_uuid;
                }
            }
            else if (valve.type == HydraulicLinkValveType::PCV)
            {
                if (!curve_id.has_value() || *curve_id <= 0 || curve_uuid.isNull()
                    || !characteristic_curve_uuids.contains(curve_uuid))
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("missing-pcv-curve-reference"),
                        QStringLiteral("epanet-js PCV id %1 references a missing or non-valve characteristic curve.")
                            .arg(*source_id),
                        QStringLiteral("valves"),
                        *source_id);
                }
                else
                {
                    valve.characteristic_curve_uuid = curve_uuid;
                }
            }
            else if (curve_id.has_value() && *curve_id > 0)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Warning,
                    QStringLiteral("ignored-valve-curve-reference"),
                    QStringLiteral("epanet-js valve id %1 carries curve_id although its valve type does not use a curve; the reference was ignored.")
                        .arg(*source_id),
                    QStringLiteral("valves"),
                    *source_id);
            }
        }
        else if (valve.type == HydraulicLinkValveType::GPV)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("missing-gpv-curve-reference"),
                QStringLiteral("epanet-js GPV id %1 has no headloss curve.").arg(*source_id),
                QStringLiteral("valves"),
                *source_id);
        }

        if (row.contains(QStringLiteral("target_node_id"))
            && !row.value(QStringLiteral("target_node_id")).isNull())
        {
            const std::optional<qint64> target_node_id = integerValue(
                row.value(QStringLiteral("target_node_id")));
            if (!target_node_id.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-valve-target-node"),
                    QStringLiteral("epanet-js valve id %1 has an invalid target_node_id.")
                        .arg(*source_id),
                    QStringLiteral("valves"),
                    *source_id);
            }
            else if (*target_node_id > 0 && *target_node_id != *end_node_id)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("unsupported-remote-prv-target"),
                    QStringLiteral("epanet-js valve id %1 uses remote PRV target node id %2, which the current AOWIS valve model cannot represent without changing hydraulic behavior.")
                        .arg(*source_id)
                        .arg(*target_node_id),
                    QStringLiteral("valves"),
                    *source_id);
            }
        }

        result.network.links_valves.append(valve);
    }
}

void importPumpsAndValves(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    importPumps(project, project_settings, result);
    importValves(project, project_settings, result);
}

} // namespace EpanetJsPumpsValves
