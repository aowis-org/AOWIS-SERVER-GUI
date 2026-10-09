#include "import/epanet_js_pipes.h"
#include "import/epanet_js_geometry.h"
#include "import/epanet_js_conversion_common.h"
#include "import/epanet_js_schema.h"
#include "import/epanet_js_units.h"
#include "import/epanet_js_settings.h"
#include <QDate>
#include <QHash>
#include <algorithm>
#include <cmath>
namespace EpanetJsPipes {
using namespace EpanetJsGeometry;
using namespace EpanetJsConversionCommon;
using namespace EpanetJsSchema;
using namespace EpanetJsUnits;
bool importPipeStatus(
    const QVariantMap &row,
    qint64 source_id,
    HydraulicLinkPipe &pipe,
    EpanetJsProjectConversionResult &result)
{
    if (!row.contains(QStringLiteral("initial_status"))
        || row.value(QStringLiteral("initial_status")).isNull())
        return true;

    QString status = row.value(QStringLiteral("initial_status")).toString().trimmed().toLower();
    status.remove(QLatin1Char('_'));
    status.remove(QLatin1Char('-'));
    status.remove(QLatin1Char(' '));
    if (status.isEmpty() || status == QStringLiteral("open"))
    {
        pipe.initial_status = HydraulicLinkPipeInitialStatus::Open;
        return true;
    }
    if (status == QStringLiteral("closed"))
    {
        pipe.initial_status = HydraulicLinkPipeInitialStatus::Closed;
        return true;
    }
    if (status == QStringLiteral("cv") || status == QStringLiteral("checkvalve"))
    {
        pipe.initial_status = HydraulicLinkPipeInitialStatus::CheckValve;
        return true;
    }

    appendDiagnostic(
        result,
        EpanetJsConversionDiagnosticSeverity::Error,
        QStringLiteral("unknown-pipe-status"),
        QStringLiteral("epanet-js pipe id %1 has unknown initial_status '%2'.")
            .arg(source_id)
            .arg(row.value(QStringLiteral("initial_status")).toString()),
        QStringLiteral("pipes"),
        source_id);
    return false;
}

void importPipes(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("pipes"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString length_unit = firstUnit(units, QStringList{QStringLiteral("length")});
    const QString diameter_unit = firstUnit(units, QStringList{QStringLiteral("diameter")});
    const QString roughness_unit = units.value(QStringLiteral("roughness")).toString().trimmed();
    const QString flow_unit = units.value(QStringLiteral("flow")).toString().trimmed();

    // Preserve the first matching material for case-insensitive labels.
    QHash<QString, qsizetype> material_positions;
    const QList<HydraulicPipeMaterial> &materials = result.network.pipe_materials;
    material_positions.reserve(materials.size());
    for (qsizetype index = 0; index < materials.size(); ++index)
    {
        const QString key = materials.at(index).id.trimmed().toCaseFolded();
        if (!material_positions.contains(key))
            material_positions.insert(key, index);
    }

    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value() || !result.id_map.contains(QStringLiteral("pipes"), *source_id))
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

        HydraulicLinkPipe pipe;
        pipe.id = importedEntityId(row, QStringLiteral("pipes"), *source_id, result);
        pipe.uuid = result.id_map.uuidFor(QStringLiteral("pipes"), *source_id);
        pipe.node_uuid_from = from_uuid;
        pipe.node_uuid_to = to_uuid;
        pipe.metadata.enabled = !row.contains(QStringLiteral("is_active"))
            || row.value(QStringLiteral("is_active")).toInt() != 0;
        pipe.roughness_mode = HydraulicPipeRoughnessMode::Explicit;

        const QString source_material = row.value(QStringLiteral("material")).toString().trimmed();
        const QString material_key = source_material.toCaseFolded();
        const auto material_it = material_positions.constFind(material_key);
        const HydraulicPipeMaterial *material = source_material.isEmpty()
            || material_it == material_positions.cend()
            ? nullptr
            : &materials.at(*material_it);
        if (material != nullptr)
            pipe.material_uuid = material->uuid;

        if (row.contains(QStringLiteral("year")) && !row.value(QStringLiteral("year")).isNull())
        {
            const std::optional<qint64> year = integerValue(row.value(QStringLiteral("year")));
            if (!year.has_value() || *year < 1 || *year > 9999)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Warning,
                    QStringLiteral("invalid-pipe-installation-year"),
                    QStringLiteral("epanet-js pipe id %1 has invalid installation year; AOWIS left date_installed unset.")
                        .arg(*source_id),
                    QStringLiteral("pipes"),
                    *source_id);
            }
            else
            {
                pipe.metadata.date_installed = QDate(static_cast<int>(*year), 1, 1);
            }
        }

        if (row.contains(QStringLiteral("length")) && !row.value(QStringLiteral("length")).isNull())
        {
            const std::optional<double> value = finiteVariantDouble(row.value(QStringLiteral("length")));
            const std::optional<double> converted = value.has_value()
                ? lengthToM(*value, length_unit)
                : std::nullopt;
            if (!converted.has_value() || *converted < 0.0)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("unsupported-pipe-length"),
                    QStringLiteral("AOWIS cannot convert epanet-js pipe id %1 length with unit '%2'.")
                        .arg(*source_id)
                        .arg(length_unit),
                    QStringLiteral("pipes"),
                    *source_id);
            }
            else
            {
                pipe.length_measured_m = *converted;
            }
        }

        if (row.contains(QStringLiteral("diameter")) && !row.value(QStringLiteral("diameter")).isNull())
        {
            const std::optional<double> value = finiteVariantDouble(row.value(QStringLiteral("diameter")));
            const std::optional<double> converted = value.has_value()
                ? diameterToMm(*value, diameter_unit)
                : std::nullopt;
            if (!converted.has_value() || *converted <= 0.0)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("unsupported-pipe-diameter"),
                    QStringLiteral("AOWIS cannot convert epanet-js pipe id %1 diameter with unit '%2'.")
                        .arg(*source_id)
                        .arg(diameter_unit),
                    QStringLiteral("pipes"),
                    *source_id);
            }
            else
            {
                pipe.diameter_mm = *converted;
            }
        }

        const bool has_explicit_roughness = row.contains(QStringLiteral("roughness"))
            && !row.value(QStringLiteral("roughness")).isNull();
        if (has_explicit_roughness)
        {
            const std::optional<double> value = finiteVariantDouble(row.value(QStringLiteral("roughness")));
            if (!value.has_value() || *value <= 0.0)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-pipe-roughness"),
                    QStringLiteral("epanet-js pipe id %1 has invalid explicit roughness.").arg(*source_id),
                    QStringLiteral("pipes"),
                    *source_id);
            }
            else
            {
                switch (result.network.options_hydraulic.headloss_formula)
                {
                case HydraulicHeadlossFormula::HazenWilliams:
                    pipe.roughness_hazen_williams = *value;
                    break;
                case HydraulicHeadlossFormula::DarcyWeisbach:
                {
                    const std::optional<double> converted = darcyRoughnessToMm(
                        *value, roughness_unit, flow_unit);
                    if (!converted.has_value())
                    {
                        appendDiagnostic(
                            result,
                            EpanetJsConversionDiagnosticSeverity::Error,
                            QStringLiteral("unsupported-pipe-roughness-unit"),
                            QStringLiteral("AOWIS cannot convert Darcy-Weisbach roughness for epanet-js pipe id %1.")
                                .arg(*source_id),
                            QStringLiteral("pipes"),
                            *source_id);
                    }
                    else
                    {
                        pipe.roughness_darcy_weisbach_mm = *converted;
                    }
                    break;
                }
                case HydraulicHeadlossFormula::ChezyManning:
                    pipe.roughness_chezy_manning = *value;
                    break;
                }
            }
        }
        else if (material != nullptr)
        {
            pipe.roughness_mode = HydraulicPipeRoughnessMode::MaterialLibrary;
            if (!pipe.metadata.date_installed.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-pipe-material-age"),
                    QStringLiteral("epanet-js pipe id %1 uses material-library roughness but has no valid installation year.")
                        .arg(*source_id),
                    QStringLiteral("pipes"),
                    *source_id);
            }
        }
        else if (!source_material.isEmpty())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("missing-pipe-material-reference"),
                QStringLiteral("epanet-js pipe id %1 uses material '%2' for roughness, but that material is not present in project.pipe_library.")
                    .arg(*source_id)
                    .arg(source_material),
                QStringLiteral("pipes"),
                *source_id);
        }

        if (row.contains(QStringLiteral("minor_loss")) && !row.value(QStringLiteral("minor_loss")).isNull())
        {
            const std::optional<double> value = finiteVariantDouble(row.value(QStringLiteral("minor_loss")));
            if (!value.has_value() || *value < 0.0)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-pipe-minor-loss"),
                    QStringLiteral("epanet-js pipe id %1 has invalid minor_loss.").arg(*source_id),
                    QStringLiteral("pipes"),
                    *source_id);
            }
            else
            {
                pipe.minor_loss_coefficient = *value;
            }
        }

        importPipeStatus(row, *source_id, pipe, result);

        const std::optional<QList<CoordinateWGS84>> coordinates = linkCoordinates(
            row, *source_id, QStringLiteral("pipes"), QStringLiteral("pipe"), result);
        if (coordinates.has_value() && coordinates->size() >= 2)
        {
            double calculated_length_m = 0.0;
            for (qsizetype index = 1; index < coordinates->size(); ++index)
                calculated_length_m += approximateDistanceMeters(
                    coordinates->at(index - 1), coordinates->at(index));
            pipe.length_calculated_m = calculated_length_m;

            for (qsizetype index = 1; index + 1 < coordinates->size(); ++index)
            {
                HydraulicLinkVertex vertex;
                vertex.coordinate_wgs84 = coordinates->at(index);
                pipe.vertices.append(vertex);
            }
        }


        result.network.links_pipes.append(pipe);
    }
}



}
