#include "import/epanet_js_patterns_curves.h"
#include "import/epanet_js_conversion_common.h"
#include "import/epanet_js_schema.h"
#include "import/epanet_js_units.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <optional>

namespace EpanetJsPatternsCurves
{
using namespace EpanetJsConversionCommon;
using namespace EpanetJsSchema;
using namespace EpanetJsUnits;
std::optional<QList<QPair<double, double>>> curvePoints(const QString &text)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError)
        return std::nullopt;

    QList<QPair<double, double>> points;
    if (document.isArray())
    {
        const QJsonArray rows = document.array();
        if (!rows.isEmpty() && rows.first().isDouble())
        {
            if ((rows.size() % 2) != 0)
                return std::nullopt;
            for (qsizetype index = 0; index < rows.size(); index += 2)
            {
                const std::optional<double> x = finiteDouble(rows.at(index));
                const std::optional<double> y = finiteDouble(rows.at(index + 1));
                if (!x.has_value() || !y.has_value())
                    return std::nullopt;
                points.append(qMakePair(*x, *y));
            }
            return points;
        }

        for (const QJsonValue &row_value : rows)
        {
            std::optional<double> x;
            std::optional<double> y;
            if (row_value.isArray())
            {
                const QJsonArray row = row_value.toArray();
                if (row.size() >= 2)
                {
                    x = finiteDouble(row.at(0));
                    y = finiteDouble(row.at(1));
                }
            }
            else if (row_value.isObject())
            {
                const QJsonObject row = row_value.toObject();
                x = finiteDouble(row.value(QStringLiteral("x")));
                y = finiteDouble(row.value(QStringLiteral("y")));
            }
            if (!x.has_value() || !y.has_value())
                return std::nullopt;
            points.append(qMakePair(*x, *y));
        }
        return points;
    }

    if (document.isObject())
    {
        const QJsonObject object = document.object();
        const std::optional<double> scalar_x = finiteDouble(object.value(QStringLiteral("x")));
        const std::optional<double> scalar_y = finiteDouble(object.value(QStringLiteral("y")));
        if (scalar_x.has_value() && scalar_y.has_value())
        {
            points.append(qMakePair(*scalar_x, *scalar_y));
            return points;
        }
        if (!object.value(QStringLiteral("x")).isArray() || !object.value(QStringLiteral("y")).isArray())
            return std::nullopt;
        const QJsonArray xs = object.value(QStringLiteral("x")).toArray();
        const QJsonArray ys = object.value(QStringLiteral("y")).toArray();
        if (xs.size() != ys.size())
            return std::nullopt;
        for (qsizetype index = 0; index < xs.size(); ++index)
        {
            const std::optional<double> x = finiteDouble(xs.at(index));
            const std::optional<double> y = finiteDouble(ys.at(index));
            if (!x.has_value() || !y.has_value())
                return std::nullopt;
            points.append(qMakePair(*x, *y));
        }
        return points;
    }

    return std::nullopt;
}

void importPatterns(
    const EpanetJsProjectSnapshot &project,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("patterns"));
    if (table == nullptr)
        return;

    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value() || !result.id_map.contains(QStringLiteral("patterns"), *source_id))
            continue;

        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(
            row.value(QStringLiteral("multipliers")).toString().toUtf8(), &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isArray())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Warning,
                QStringLiteral("invalid-pattern-multipliers"),
                QStringLiteral("epanet-js pattern id %1 has invalid multipliers JSON and was skipped.").arg(*source_id),
                QStringLiteral("patterns"),
                *source_id);
            continue;
        }

        HydraulicPatternTime pattern;
        pattern.id = row.value(QStringLiteral("label")).toString().trimmed();
        if (pattern.id.isEmpty())
            pattern.id = QString::number(*source_id);
        pattern.uuid = result.id_map.uuidFor(QStringLiteral("patterns"), *source_id);

        bool valid = true;
        for (const QJsonValue &value : document.array())
        {
            const std::optional<double> multiplier = finiteDouble(value);
            if (!multiplier.has_value())
            {
                valid = false;
                break;
            }
            pattern.multipliers.append(*multiplier);
        }
        if (!valid || pattern.multipliers.isEmpty())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Warning,
                QStringLiteral("invalid-pattern-multipliers"),
                QStringLiteral("epanet-js pattern id %1 contains invalid or empty multipliers and was skipped.").arg(*source_id),
                QStringLiteral("patterns"),
                *source_id);
            continue;
        }
        result.network.patterns_time.append(pattern);
    }
}

void importCurves(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("curves"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString flow_unit = units.value(QStringLiteral("flow")).toString();
    QString head_unit = units.value(QStringLiteral("head")).toString();
    if (head_unit.isEmpty())
        head_unit = units.value(QStringLiteral("elevation")).toString();
    QString level_unit = units.value(QStringLiteral("level")).toString();
    if (level_unit.isEmpty())
        level_unit = head_unit;
    const QString volume_unit = units.value(QStringLiteral("volume")).toString();

    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value() || !result.id_map.contains(QStringLiteral("curves"), *source_id))
            continue;

        const std::optional<QList<QPair<double, double>>> source_points = curvePoints(
            row.value(QStringLiteral("points")).toString());
        if (!source_points.has_value() || source_points->isEmpty())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Warning,
                QStringLiteral("invalid-curve-points"),
                QStringLiteral("epanet-js curve id %1 has invalid or empty point data and was skipped.").arg(*source_id),
                QStringLiteral("curves"),
                *source_id);
            continue;
        }

        QString id = row.value(QStringLiteral("label")).toString().trimmed();
        if (id.isEmpty())
            id = QString::number(*source_id);
        const QUuid uuid = result.id_map.uuidFor(QStringLiteral("curves"), *source_id);
        const QString type = row.value(QStringLiteral("type")).toString().trimmed().toLower();

        if (type == QStringLiteral("pump"))
        {
            HydraulicCurvePumpHead curve;
            curve.id = id;
            curve.uuid = uuid;
            bool valid = true;
            for (const QPair<double, double> &point : *source_points)
            {
                const std::optional<double> flow = flowToM3PerH(point.first, flow_unit);
                const std::optional<double> head = lengthToM(point.second, head_unit);
                if (!flow.has_value() || !head.has_value())
                {
                    valid = false;
                    break;
                }
                curve.points.append({*flow, *head});
            }
            if (valid)
                result.network.curves_pump_head.append(curve);
            else
                appendDiagnostic(result, EpanetJsConversionDiagnosticSeverity::Warning,
                    QStringLiteral("unsupported-curve-unit"),
                    QStringLiteral("epanet-js pump curve id %1 uses unsupported project units and was skipped.").arg(*source_id),
                    QStringLiteral("curves"), *source_id);
        }
        else if (type == QStringLiteral("efficiency"))
        {
            HydraulicCurvePumpEfficiency curve;
            curve.id = id;
            curve.uuid = uuid;
            bool valid = true;
            for (const QPair<double, double> &point : *source_points)
            {
                const std::optional<double> flow = flowToM3PerH(point.first, flow_unit);
                if (!flow.has_value())
                {
                    valid = false;
                    break;
                }
                curve.points.append({*flow, point.second});
            }
            if (valid)
                result.network.curves_pump_efficiency.append(curve);
            else
                appendDiagnostic(result, EpanetJsConversionDiagnosticSeverity::Warning,
                    QStringLiteral("unsupported-curve-unit"),
                    QStringLiteral("epanet-js efficiency curve id %1 uses an unsupported flow unit and was skipped.").arg(*source_id),
                    QStringLiteral("curves"), *source_id);
        }
        else if (type == QStringLiteral("volume"))
        {
            HydraulicCurveTankVolume curve;
            curve.id = id;
            curve.uuid = uuid;
            bool valid = true;
            for (const QPair<double, double> &point : *source_points)
            {
                const std::optional<double> level = lengthToM(point.first, level_unit);
                const std::optional<double> volume = volumeToM3(point.second, volume_unit);
                if (!level.has_value() || !volume.has_value())
                {
                    valid = false;
                    break;
                }
                curve.points.append({*level, *volume});
            }
            if (valid)
                result.network.curves_tank_volume.append(curve);
            else
                appendDiagnostic(result, EpanetJsConversionDiagnosticSeverity::Warning,
                    QStringLiteral("unsupported-curve-unit"),
                    QStringLiteral("epanet-js volume curve id %1 uses unsupported project units and was skipped.").arg(*source_id),
                    QStringLiteral("curves"), *source_id);
        }
        else if (type == QStringLiteral("valve"))
        {
            HydraulicCurveValveCharacteristic curve;
            curve.id = id;
            curve.uuid = uuid;
            for (const QPair<double, double> &point : *source_points)
                curve.points.append({point.first, point.second});
            result.network.curves_valve_characteristic.append(curve);
        }
        else if (type == QStringLiteral("headloss"))
        {
            QString headloss_unit = units.value(QStringLiteral("headloss")).toString();
            if (headloss_unit.isEmpty())
                headloss_unit = head_unit;
            HydraulicCurveValveHeadloss curve;
            curve.id = id;
            curve.uuid = uuid;
            bool valid = true;
            for (const QPair<double, double> &point : *source_points)
            {
                const std::optional<double> flow = flowToM3PerH(point.first, flow_unit);
                const std::optional<double> head = lengthToM(point.second, headloss_unit);
                if (!flow.has_value() || !head.has_value())
                {
                    valid = false;
                    break;
                }
                curve.points.append({*flow, *head});
            }
            if (valid)
                result.network.curves_valve_headloss.append(curve);
            else
                appendDiagnostic(result, EpanetJsConversionDiagnosticSeverity::Warning,
                    QStringLiteral("unsupported-curve-unit"),
                    QStringLiteral("epanet-js headloss curve id %1 uses unsupported project units and was skipped.").arg(*source_id),
                    QStringLiteral("curves"), *source_id);
        }
        else
        {
            HydraulicCurveGeneric curve;
            curve.id = id;
            curve.uuid = uuid;
            for (const QPair<double, double> &point : *source_points)
                curve.points.append({point.first, point.second});
            result.network.curves_generic.append(curve);
        }
    }
}


}
