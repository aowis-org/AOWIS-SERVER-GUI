#include "import/epanet_js_project_converter.h"

#include <QByteArray>
#include <QDate>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const QStringList entityTables()
{
    return {
        QStringLiteral("junctions"),
        QStringLiteral("reservoirs"),
        QStringLiteral("tanks"),
        QStringLiteral("pipes"),
        QStringLiteral("pumps"),
        QStringLiteral("valves"),
        QStringLiteral("patterns"),
        QStringLiteral("curves"),
        QStringLiteral("customer_points"),
        QStringLiteral("zones")
    };
}

const QStringList nodeTables()
{
    return {
        QStringLiteral("junctions"),
        QStringLiteral("reservoirs"),
        QStringLiteral("tanks")
    };
}

const QStringList linkTables()
{
    return {
        QStringLiteral("pipes"),
        QStringLiteral("pumps"),
        QStringLiteral("valves")
    };
}

const EpanetJsTableSnapshot *tableByName(
    const EpanetJsProjectSnapshot &project,
    const QString &table_name)
{
    const QMap<QString, EpanetJsTableSnapshot>::const_iterator iterator =
        project.tables.constFind(table_name);
    if (iterator == project.tables.cend())
        return nullptr;
    return &iterator.value();
}

bool tableHasColumn(const EpanetJsTableSnapshot *table, const QString &column_name)
{
    return table != nullptr && table->columns.contains(column_name);
}

QUuid uuidV5(const QUuid &namespace_uuid, const QByteArray &name)
{
    QByteArray input = namespace_uuid.toRfc4122();
    input.append(name);
    QByteArray bytes = QCryptographicHash::hash(input, QCryptographicHash::Sha1).left(16);
    if (bytes.size() != 16)
        return {};

    bytes[6] = static_cast<char>((static_cast<unsigned char>(bytes.at(6)) & 0x0fU) | 0x50U);
    bytes[8] = static_cast<char>((static_cast<unsigned char>(bytes.at(8)) & 0x3fU) | 0x80U);
    return QUuid::fromRfc4122(bytes);
}

QUuid aowisEpanetJsNamespace()
{
    return QUuid(QStringLiteral("{d150f58c-c6c4-5d39-94d5-24f709935868}"));
}

QString projectSettingsText(const EpanetJsProjectSnapshot &project)
{
    const EpanetJsTableSnapshot *project_table = tableByName(project, QStringLiteral("project"));
    if (project_table == nullptr || project_table->rows.isEmpty())
        return {};
    return project_table->rows.first().value(QStringLiteral("settings")).toString();
}

QJsonObject projectSettingsObject(const EpanetJsProjectSnapshot &project)
{
    const QByteArray settings = projectSettingsText(project).toUtf8();
    if (settings.isEmpty())
        return {};

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(settings, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    return document.object();
}

QString projectPipeLibraryText(const EpanetJsProjectSnapshot &project)
{
    const EpanetJsTableSnapshot *project_table = tableByName(project, QStringLiteral("project"));
    if (project_table == nullptr || project_table->rows.isEmpty()
        || !project_table->columns.contains(QStringLiteral("pipe_library")))
    {
        return {};
    }
    return project_table->rows.first().value(QStringLiteral("pipe_library")).toString();
}

QString pipeMaterialLookupKey(const QString &label)
{
    return label.trimmed().toCaseFolded();
}

QByteArray fallbackProjectIdentitySeed(const EpanetJsProjectSnapshot &project)
{
    QByteArray seed("aowis/epanet-js/project\n");
    const QString settings = projectSettingsText(project);
    if (!settings.isEmpty())
    {
        seed.append("settings=");
        seed.append(settings.toUtf8());
        seed.append('\n');
    }

    for (const QString &table_name : entityTables())
    {
        const EpanetJsTableSnapshot *table = tableByName(project, table_name);
        if (table == nullptr)
            continue;

        QList<QPair<QString, QString>> identities;
        for (const QVariantMap &row : table->rows)
        {
            identities.append(qMakePair(
                row.value(QStringLiteral("id")).toString(),
                row.value(QStringLiteral("label")).toString()));
        }
        std::sort(identities.begin(), identities.end(),
                  [](const QPair<QString, QString> &left,
                     const QPair<QString, QString> &right)
                  {
                      if (left.first != right.first)
                          return left.first < right.first;
                      return left.second < right.second;
                  });

        seed.append(table_name.toUtf8());
        seed.append('=');
        for (const QPair<QString, QString> &identity : identities)
        {
            seed.append(identity.first.toUtf8());
            seed.append(':');
            seed.append(identity.second.toUtf8());
            seed.append(';');
        }
        seed.append('\n');
    }

    return seed;
}

void appendDiagnostic(
    EpanetJsProjectConversionResult &result,
    EpanetJsConversionDiagnosticSeverity severity,
    const QString &code,
    const QString &message,
    const QString &table_name = {},
    std::optional<qint64> source_id = std::nullopt)
{
    EpanetJsConversionDiagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = code;
    diagnostic.message = message;
    diagnostic.table_name = table_name;
    diagnostic.source_id = source_id;
    result.diagnostics.append(diagnostic);
}

std::optional<qint64> integerValue(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return std::nullopt;

    bool ok = false;
    const qint64 integer = value.toLongLong(&ok);
    if (!ok)
        return std::nullopt;
    return integer;
}


std::optional<double> finiteDouble(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number))
        return std::nullopt;
    return number;
}

QString normalizedUnit(QString unit)
{
    unit = unit.trimmed().toLower();
    unit.remove(QLatin1Char(' '));
    unit.replace(QStringLiteral("³"), QStringLiteral("^3"));
    return unit;
}

std::optional<double> flowToM3PerH(double value, const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("m^3/h") || normalized == QStringLiteral("m3/h") ||
        normalized == QStringLiteral("cmh"))
        return value;
    if (normalized == QStringLiteral("m^3/s") || normalized == QStringLiteral("m3/s") ||
        normalized == QStringLiteral("cms"))
        return value * 3600.0;
    if (normalized == QStringLiteral("l/s") || normalized == QStringLiteral("lps"))
        return value * 3.6;
    if (normalized == QStringLiteral("l/min") || normalized == QStringLiteral("lpm"))
        return value * 0.06;
    if (normalized == QStringLiteral("m^3/d") || normalized == QStringLiteral("m3/d") ||
        normalized == QStringLiteral("cmd"))
        return value / 24.0;
    if (normalized == QStringLiteral("mld"))
        return value * (1000.0 / 24.0);
    if (normalized == QStringLiteral("ft^3/s") || normalized == QStringLiteral("ft3/s") ||
        normalized == QStringLiteral("cfs"))
        return value * 101.9406477312;
    if (normalized == QStringLiteral("gpm") || normalized == QStringLiteral("gal/min")
        || normalized == QStringLiteral("usgal/min"))
        return value * 0.22712470704;
    if (normalized == QStringLiteral("mgd"))
        return value * 157.725491;
    if (normalized == QStringLiteral("imgd"))
        return value * 189.42041666666667;
    if (normalized == QStringLiteral("acre-ft/d") || normalized == QStringLiteral("afd"))
        return value * 51.39507656448;
    return std::nullopt;
}

std::optional<double> lengthToM(double value, const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("m") || normalized == QStringLiteral("meter") ||
        normalized == QStringLiteral("metre"))
        return value;
    if (normalized == QStringLiteral("ft") || normalized == QStringLiteral("foot") ||
        normalized == QStringLiteral("feet"))
        return value * 0.3048;
    return std::nullopt;
}

std::optional<double> volumeToM3(double value, const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("m^3") || normalized == QStringLiteral("m3"))
        return value;
    if (normalized == QStringLiteral("ft^3") || normalized == QStringLiteral("ft3"))
        return value * 0.028316846592;
    if (normalized == QStringLiteral("gal") || normalized == QStringLiteral("usgal"))
        return value * 0.003785411784;
    return std::nullopt;
}


std::optional<double> diameterToMm(double value, const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("mm") || normalized == QStringLiteral("millimeter")
        || normalized == QStringLiteral("millimetre"))
        return value;
    if (normalized == QStringLiteral("cm"))
        return value * 10.0;
    if (normalized == QStringLiteral("m") || normalized == QStringLiteral("meter")
        || normalized == QStringLiteral("metre"))
        return value * 1000.0;
    if (normalized == QStringLiteral("in") || normalized == QStringLiteral("inch")
        || normalized == QStringLiteral("inches"))
        return value * 25.4;
    if (normalized == QStringLiteral("ft") || normalized == QStringLiteral("foot")
        || normalized == QStringLiteral("feet"))
        return value * 304.8;
    return std::nullopt;
}


bool flowUnitUsesMetricLength(const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    return normalized == QStringLiteral("l/s") || normalized == QStringLiteral("lps") ||
        normalized == QStringLiteral("l/min") || normalized == QStringLiteral("lpm") ||
        normalized == QStringLiteral("mld") || normalized == QStringLiteral("m^3/h") ||
        normalized == QStringLiteral("m3/h") || normalized == QStringLiteral("cmh") ||
        normalized == QStringLiteral("m^3/d") || normalized == QStringLiteral("m3/d") ||
        normalized == QStringLiteral("cmd") || normalized == QStringLiteral("m^3/s") ||
        normalized == QStringLiteral("m3/s") || normalized == QStringLiteral("cms");
}

bool flowUnitUsesFootLength(const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    return normalized == QStringLiteral("ft^3/s") || normalized == QStringLiteral("ft3/s") ||
        normalized == QStringLiteral("cfs") || normalized == QStringLiteral("gpm") ||
        normalized == QStringLiteral("gal/min") || normalized == QStringLiteral("usgal/min") ||
        normalized == QStringLiteral("mgd") || normalized == QStringLiteral("imgd") ||
        normalized == QStringLiteral("acre-ft/d") || normalized == QStringLiteral("afd");
}

std::optional<double> darcyRoughnessToMm(
    double value,
    const QString &roughness_unit,
    const QString &flow_unit)
{
    const QString normalized = normalizedUnit(roughness_unit);
    if (normalized == QStringLiteral("mm") || normalized == QStringLiteral("millimeter")
        || normalized == QStringLiteral("millimetre"))
        return value;
    if (normalized == QStringLiteral("millift") || normalized == QStringLiteral("millifeet")
        || normalized == QStringLiteral("milli-ft") || normalized == QStringLiteral("0.001ft"))
        return value * 0.3048;
    if (normalized == QStringLiteral("ft") || normalized == QStringLiteral("foot")
        || normalized == QStringLiteral("feet"))
        return value * 304.8;
    if (normalized.isEmpty())
    {
        if (flowUnitUsesMetricLength(flow_unit))
            return value;
        if (flowUnitUsesFootLength(flow_unit))
            return value * 0.3048;
    }
    return std::nullopt;
}

std::optional<double> chemicalConcentrationScaleToMgPerL(const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("mg/l"))
        return 1.0;
    if (normalized == QStringLiteral("ug/l") || normalized == QStringLiteral("µg/l"))
        return 0.001;
    return std::nullopt;
}

std::optional<double> pressureToHeadM(
    double value,
    const QString &unit,
    double specific_gravity)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("m") || normalized == QStringLiteral("mwc") ||
        normalized == QStringLiteral("mh2o"))
        return value;
    if (normalized == QStringLiteral("ft") || normalized == QStringLiteral("feet"))
        return value * 0.3048;
    if (!std::isfinite(specific_gravity) || specific_gravity <= 0.0)
        return std::nullopt;
    if (normalized == QStringLiteral("psi"))
        return value / (0.4333 * specific_gravity) * 0.3048;
    if (normalized == QStringLiteral("kpa"))
        return value / (6.895 * 0.4333 * specific_gravity) * 0.3048;
    if (normalized == QStringLiteral("bar"))
        return value / (0.068948 * 0.4333 * specific_gravity) * 0.3048;
    return std::nullopt;
}

QJsonObject simulationSettingsObject(
    const EpanetJsProjectSnapshot &project,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("simulation_settings"));
    if (table == nullptr || table->rows.isEmpty())
        return {};

    const QString data = table->rows.first().value(QStringLiteral("data")).toString();
    if (data.trimmed().isEmpty())
        return {};

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("invalid-simulation-settings-json"),
            QStringLiteral("epanet-js simulation_settings.data is not a valid JSON object; AOWIS kept its simulation defaults."),
            QStringLiteral("simulation_settings"));
        return {};
    }
    return document.object();
}

QJsonObject projectUnitsObject(const QJsonObject &project_settings)
{
    const QJsonValue units = project_settings.value(QStringLiteral("units"));
    return units.isObject() ? units.toObject() : QJsonObject();
}

void mapHeadlossFormula(
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const QString value = project_settings.value(QStringLiteral("headlossFormula"))
        .toString().trimmed().toUpper();
    if (value.isEmpty())
        return;

    if (value == QStringLiteral("H-W") || value == QStringLiteral("HW") ||
        value == QStringLiteral("HAZEN-WILLIAMS"))
    {
        result.network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::HazenWilliams;
        return;
    }
    if (value == QStringLiteral("D-W") || value == QStringLiteral("DW") ||
        value == QStringLiteral("DARCY-WEISBACH"))
    {
        result.network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::DarcyWeisbach;
        return;
    }
    if (value == QStringLiteral("C-M") || value == QStringLiteral("CM") ||
        value == QStringLiteral("CHEZY-MANNING"))
    {
        result.network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::ChezyManning;
        return;
    }

    appendDiagnostic(
        result,
        EpanetJsConversionDiagnosticSeverity::Warning,
        QStringLiteral("unknown-headloss-formula"),
        QStringLiteral("epanet-js headloss formula '%1' is unknown; AOWIS kept its default formula.").arg(value),
        QStringLiteral("project"));
}

std::optional<double> pipeLibraryRoughnessToCanonical(
    double value,
    HydraulicHeadlossFormula formula,
    const QString &roughness_unit,
    const QString &flow_unit)
{
    if (!std::isfinite(value) || value <= 0.0)
        return std::nullopt;

    switch (formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
    case HydraulicHeadlossFormula::ChezyManning:
        return value;
    case HydraulicHeadlossFormula::DarcyWeisbach:
        return darcyRoughnessToMm(value, roughness_unit, flow_unit);
    }
    return std::nullopt;
}

void importPipeMaterials(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const QString library_text = projectPipeLibraryText(project).trimmed();
    if (library_text.isEmpty())
        return;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(library_text.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isArray())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-pipe-library-json"),
            QStringLiteral("epanet-js project.pipe_library is not a valid JSON array."),
            QStringLiteral("project"));
        return;
    }

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString roughness_unit = units.value(QStringLiteral("roughness")).toString().trimmed();
    const QString flow_unit = units.value(QStringLiteral("flow")).toString().trimmed();
    const HydraulicHeadlossFormula formula = result.network.options_hydraulic.headloss_formula;
    QSet<QString> material_keys;

    const QJsonArray materials = document.array();
    for (qsizetype material_index = 0; material_index < materials.size(); ++material_index)
    {
        const QJsonValue material_value = materials.at(material_index);
        if (!material_value.isObject())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-pipe-material"),
                QStringLiteral("epanet-js pipe library entry %1 is not an object.")
                    .arg(material_index),
                QStringLiteral("project"));
            continue;
        }

        const QJsonObject material_object = material_value.toObject();
        const QString label = material_object.value(QStringLiteral("label")).toString().trimmed();
        const QString material_key = pipeMaterialLookupKey(label);
        if (material_key.isEmpty())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-pipe-material-label"),
                QStringLiteral("epanet-js pipe library entry %1 has no material label.")
                    .arg(material_index),
                QStringLiteral("project"));
            continue;
        }
        if (material_keys.contains(material_key))
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("duplicate-pipe-material-label"),
                QStringLiteral("epanet-js pipe library contains duplicate material label '%1'.")
                    .arg(label),
                QStringLiteral("project"));
            continue;
        }
        material_keys.insert(material_key);

        const QJsonValue entries_value = material_object.value(QStringLiteral("entries"));
        if (!entries_value.isArray() || entries_value.toArray().isEmpty())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-pipe-material-entries"),
                QStringLiteral("epanet-js pipe material '%1' has no roughness entries.")
                    .arg(label),
                QStringLiteral("project"));
            continue;
        }

        HydraulicPipeMaterial material;
        material.id = label;
        material.uuid = uuidV5(
            result.network.uuid,
            QStringLiteral("epanet-js/pipe-materials/%1").arg(material_key).toUtf8());
        material.description = QStringLiteral("Imported from epanet-js pipe library");

        QSet<int> ages;
        const QJsonArray entries = entries_value.toArray();
        for (qsizetype entry_index = 0; entry_index < entries.size(); ++entry_index)
        {
            const QJsonValue entry_value = entries.at(entry_index);
            if (!entry_value.isObject())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-pipe-material-entry"),
                    QStringLiteral("epanet-js pipe material '%1' entry %2 is not an object.")
                        .arg(label)
                        .arg(entry_index),
                    QStringLiteral("project"));
                continue;
            }

            const QJsonObject entry_object = entry_value.toObject();
            const std::optional<double> source_age = finiteDouble(entry_object.value(QStringLiteral("age")));
            const std::optional<double> source_roughness = finiteDouble(
                entry_object.value(QStringLiteral("roughness")));
            if (!source_age.has_value() || *source_age < 0.0
                || std::floor(*source_age) != *source_age
                || *source_age > static_cast<double>(std::numeric_limits<int>::max()))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-pipe-material-age"),
                    QStringLiteral("epanet-js pipe material '%1' entry %2 has an invalid age.")
                        .arg(label)
                        .arg(entry_index),
                    QStringLiteral("project"));
                continue;
            }

            const int age_years = static_cast<int>(*source_age);
            if (ages.contains(age_years))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("duplicate-pipe-material-age"),
                    QStringLiteral("epanet-js pipe material '%1' contains duplicate age %2.")
                        .arg(label)
                        .arg(age_years),
                    QStringLiteral("project"));
                continue;
            }
            ages.insert(age_years);

            const std::optional<double> roughness = source_roughness.has_value()
                ? pipeLibraryRoughnessToCanonical(
                    *source_roughness, formula, roughness_unit, flow_unit)
                : std::nullopt;
            if (!roughness.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("unsupported-pipe-material-roughness"),
                    QStringLiteral("AOWIS cannot convert roughness for epanet-js pipe material '%1' at age %2.")
                        .arg(label)
                        .arg(age_years),
                    QStringLiteral("project"));
                continue;
            }

            HydraulicPipeMaterialRoughnessAtAge entry;
            entry.age_years = age_years;
            switch (formula)
            {
            case HydraulicHeadlossFormula::HazenWilliams:
                entry.roughness_hazen_williams = *roughness;
                break;
            case HydraulicHeadlossFormula::DarcyWeisbach:
                entry.roughness_darcy_weisbach_mm = *roughness;
                break;
            case HydraulicHeadlossFormula::ChezyManning:
                entry.roughness_chezy_manning = *roughness;
                break;
            }
            material.roughness_by_age.append(entry);
        }

        std::sort(
            material.roughness_by_age.begin(),
            material.roughness_by_age.end(),
            [](const HydraulicPipeMaterialRoughnessAtAge &left,
               const HydraulicPipeMaterialRoughnessAtAge &right)
            {
                return left.age_years < right.age_years;
            });

        if (!material.roughness_by_age.isEmpty())
            result.network.pipe_materials.append(material);
    }
}

const HydraulicPipeMaterial *pipeMaterialByLabel(
    const NetworkHydraulic &network,
    const QString &label)
{
    const QString key = pipeMaterialLookupKey(label);
    if (key.isEmpty())
        return nullptr;
    for (const HydraulicPipeMaterial &material : network.pipe_materials)
    {
        if (pipeMaterialLookupKey(material.id) == key)
            return &material;
    }
    return nullptr;
}

void setUnsignedSecondsIfPresent(
    const QJsonObject &object,
    const QString &key,
    quint64 &target,
    EpanetJsProjectConversionResult &result)
{
    if (!object.contains(key))
        return;
    const std::optional<double> value = finiteDouble(object.value(key));
    if (!value.has_value() || *value < 0.0 || std::floor(*value) != *value)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("invalid-time-setting"),
            QStringLiteral("epanet-js timing.%1 is not a non-negative whole number of seconds; AOWIS kept its default.").arg(key),
            QStringLiteral("simulation_settings"));
        return;
    }
    target = static_cast<quint64>(*value);
}

void mapSimulationSettings(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const QJsonObject settings = simulationSettingsObject(project, result);
    if (settings.isEmpty())
        return;

    const QJsonObject timing = settings.value(QStringLiteral("timing")).toObject();
    setUnsignedSecondsIfPresent(timing, QStringLiteral("duration"), result.network.duration_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("hydraulicTimestep"), result.network.timestep_hydraulic_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("qualityTimestep"), result.network.timestep_quality_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("patternTimestep"), result.network.timestep_pattern_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("patternStart"), result.network.start_pattern_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("reportTimestep"), result.network.timestep_report_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("reportStart"), result.network.start_report_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("ruleTimestep"), result.network.timestep_rule_s, result);
    setUnsignedSecondsIfPresent(timing, QStringLiteral("startClockTime"), result.network.start_time_of_day_s, result);

    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("globalDemandMultiplier"))); value.has_value())
        result.network.options_hydraulic.demand_multiplier = *value;

    const QString demand_model = settings.value(QStringLiteral("demandModel")).toString().trimmed().toUpper();
    if (demand_model == QStringLiteral("DDA"))
        result.network.options_hydraulic.demand_model = HydraulicDemandModel::DemandDriven;
    else if (demand_model == QStringLiteral("PDA"))
        result.network.options_hydraulic.demand_model = HydraulicDemandModel::PressureDriven;
    else if (!demand_model.isEmpty())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("unknown-demand-model"),
            QStringLiteral("epanet-js demand model '%1' is unknown; AOWIS kept its default demand model.").arg(demand_model),
            QStringLiteral("simulation_settings"));
    }

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString pressure_unit = units.value(QStringLiteral("pressure")).toString();
    const QString flow_unit = units.value(QStringLiteral("flow")).toString();
    QString head_unit = units.value(QStringLiteral("head")).toString();
    if (head_unit.isEmpty())
        head_unit = units.value(QStringLiteral("elevation")).toString();

    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("specificGravity"))); value.has_value())
        result.network.options_hydraulic.specific_gravity = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("viscosity"))); value.has_value())
        result.network.options_hydraulic.relative_viscosity = *value;

    const QList<QPair<QString, double *>> pressure_fields = {
        {QStringLiteral("minimumPressure"), &result.network.options_hydraulic.minimum_pressure_head_m},
        {QStringLiteral("requiredPressure"), &result.network.options_hydraulic.required_pressure_head_m}
    };
    for (const QPair<QString, double *> &field : pressure_fields)
    {
        if (!settings.contains(field.first))
            continue;
        const std::optional<double> value = finiteDouble(settings.value(field.first));
        if (!value.has_value())
            continue;
        const std::optional<double> converted = pressureToHeadM(
            *value, pressure_unit, result.network.options_hydraulic.specific_gravity);
        if (converted.has_value())
            *field.second = *converted;
        else
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Warning,
                QStringLiteral("unsupported-pressure-unit"),
                QStringLiteral("AOWIS cannot convert epanet-js pressure unit '%1' for %2; the default was kept.")
                    .arg(pressure_unit, field.first),
                QStringLiteral("simulation_settings"));
        }
    }

    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("pressureExponent"))); value.has_value())
        result.network.options_hydraulic.pressure_exponent = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("accuracy"))); value.has_value())
        result.network.options_hydraulic.accuracy = *value;
    if (settings.contains(QStringLiteral("backflowAllowed")) && settings.value(QStringLiteral("backflowAllowed")).isBool())
        result.network.options_hydraulic.emitters_can_backflow = settings.value(QStringLiteral("backflowAllowed")).toBool();

    const QString unbalanced = settings.value(QStringLiteral("unbalancedMode")).toString().trimmed().toUpper();
    if (unbalanced == QStringLiteral("CONTINUE"))
        result.network.options_hydraulic.unbalanced_action = HydraulicUnbalancedAction::Continue;
    else if (unbalanced == QStringLiteral("STOP"))
        result.network.options_hydraulic.unbalanced_action = HydraulicUnbalancedAction::Stop;
    if (settings.contains(QStringLiteral("unbalancedExtraTrials")))
        result.network.options_hydraulic.unbalanced_extra_trials = settings.value(QStringLiteral("unbalancedExtraTrials")).toInt(result.network.options_hydraulic.unbalanced_extra_trials);
    if (settings.contains(QStringLiteral("maximumTrials")))
        result.network.options_hydraulic.maximum_trials = settings.value(QStringLiteral("maximumTrials")).toInt(result.network.options_hydraulic.maximum_trials);
    if (settings.contains(QStringLiteral("checkFrequency")))
        result.network.options_hydraulic.check_frequency = settings.value(QStringLiteral("checkFrequency")).toInt(result.network.options_hydraulic.check_frequency);
    if (settings.contains(QStringLiteral("maximumCheck")))
        result.network.options_hydraulic.maximum_check = settings.value(QStringLiteral("maximumCheck")).toInt(result.network.options_hydraulic.maximum_check);
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("dampingLimit"))); value.has_value())
        result.network.options_hydraulic.damping_limit = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("maximumHeadError"))); value.has_value())
    {
        const std::optional<double> converted = lengthToM(*value, head_unit);
        if (converted.has_value())
            result.network.options_hydraulic.maximum_head_error_m = *converted;
    }
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("maximumFlowChange"))); value.has_value())
    {
        const std::optional<double> converted = flowToM3PerH(*value, flow_unit);
        if (converted.has_value())
            result.network.options_hydraulic.maximum_flow_change_m3_per_h = *converted;
    }

    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionBulkOrder"))); value.has_value())
        result.network.options_reaction.global_pipe_bulk_reaction.order = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionWallOrder"))); value.has_value())
        result.network.options_reaction.global_pipe_wall_reaction.order = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionTankOrder"))); value.has_value())
        result.network.options_reaction.global_tank_bulk_reaction.order = *value;

    QString chemical_unit = units.value(QStringLiteral("chemicalConcentration")).toString();
    if (chemical_unit.isEmpty())
        chemical_unit = settings.value(QStringLiteral("qualityMassUnit")).toString();
    const std::optional<double> chemical_scale = chemicalConcentrationScaleToMgPerL(chemical_unit);
    double source_length_to_m = 1.0;
    bool source_length_known = true;
    if (flowUnitUsesFootLength(flow_unit))
        source_length_to_m = 0.3048;
    else if (!flowUnitUsesMetricLength(flow_unit))
        source_length_known = false;

    if (chemical_scale.has_value())
    {
        const double pipe_bulk_order = result.network.options_reaction.global_pipe_bulk_reaction.order;
        const double tank_bulk_order = result.network.options_reaction.global_tank_bulk_reaction.order;
        const double pipe_bulk_scale = std::pow(*chemical_scale, 1.0 - std::max(pipe_bulk_order, 0.0));
        const double tank_bulk_scale = std::pow(*chemical_scale, 1.0 - std::max(tank_bulk_order, 0.0));

        if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionGlobalBulk"))); value.has_value())
        {
            result.network.options_reaction.global_pipe_bulk_reaction.coefficient = *value * pipe_bulk_scale;
            result.network.options_reaction.global_tank_bulk_reaction.coefficient = *value * tank_bulk_scale;
        }
        if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionLimitingPotential"))); value.has_value())
            result.network.options_reaction.limiting_concentration_mg_per_l = *value * *chemical_scale;
    }
    else if (settings.contains(QStringLiteral("reactionGlobalBulk")) ||
             settings.contains(QStringLiteral("reactionLimitingPotential")))
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("unsupported-chemical-unit"),
            QStringLiteral("AOWIS cannot convert epanet-js chemical concentration unit '%1'; concentration-dependent reaction settings kept their defaults.").arg(chemical_unit),
            QStringLiteral("simulation_settings"));
    }

    if (chemical_scale.has_value() && source_length_known)
    {
        const double wall_order = result.network.options_reaction.global_pipe_wall_reaction.order;
        const double wall_scale = wall_order == 0.0
            ? *chemical_scale / (source_length_to_m * source_length_to_m)
            : source_length_to_m;
        if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionGlobalWall"))); value.has_value())
            result.network.options_reaction.global_pipe_wall_reaction.coefficient = *value * wall_scale;
        if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("reactionRoughnessCorrelation"))); value.has_value())
            result.network.options_reaction.roughness_reaction_factor = *value * wall_scale;
    }
    else if (settings.contains(QStringLiteral("reactionGlobalWall")) ||
             settings.contains(QStringLiteral("reactionRoughnessCorrelation")))
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("unsupported-reaction-unit-system"),
            QStringLiteral("AOWIS cannot determine the epanet-js reaction unit system from flow unit '%1'; wall reaction settings kept their defaults.").arg(flow_unit),
            QStringLiteral("simulation_settings"));
    }

    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("energyGlobalEfficiency"))); value.has_value())
        result.network.options_energy.global_pump_efficiency_percent = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("energyGlobalPrice"))); value.has_value())
        result.network.options_energy.global_energy_price_per_kw_h = *value;
    if (const std::optional<double> value = finiteDouble(settings.value(QStringLiteral("energyDemandCharge"))); value.has_value())
        result.network.options_energy.demand_charge_per_kw = *value;

    if (settings.contains(QStringLiteral("energyGlobalPatternId")) && !settings.value(QStringLiteral("energyGlobalPatternId")).isNull())
    {
        const std::optional<qint64> source_id = integerValue(settings.value(QStringLiteral("energyGlobalPatternId")).toVariant());
        bool imported = false;
        QUuid pattern_uuid;
        if (source_id.has_value() && result.id_map.contains(QStringLiteral("patterns"), *source_id))
        {
            pattern_uuid = result.id_map.uuidFor(QStringLiteral("patterns"), *source_id);
            for (const HydraulicPatternTime &pattern : result.network.patterns_time)
            {
                if (pattern.uuid == pattern_uuid)
                {
                    imported = true;
                    break;
                }
            }
        }
        if (imported)
            result.network.options_energy.global_energy_price_pattern_uuid = pattern_uuid;
        else
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Warning,
                QStringLiteral("missing-energy-pattern-reference"),
                QStringLiteral("epanet-js global energy price pattern does not reference an existing pattern; AOWIS left it unset."),
                QStringLiteral("simulation_settings"));
        }
    }

    // epanet-js intentionally disables EPANET's node/link summary tables and uses
    // its own result views instead. Preserve that report behavior on import.
    result.network.options_report.summary = false;
    if (settings.contains(QStringLiteral("reportEnergy")) && settings.value(QStringLiteral("reportEnergy")).isBool())
        result.network.options_report.energy = settings.value(QStringLiteral("reportEnergy")).toBool();

    const QString status = settings.value(QStringLiteral("statusReport")).toString().trimmed().toUpper();
    if (status == QStringLiteral("NO") || status == QStringLiteral("NONE"))
        result.network.options_report.status = HydraulicSimulationReportStatus::None;
    else if (status == QStringLiteral("YES") || status == QStringLiteral("NORMAL"))
        result.network.options_report.status = HydraulicSimulationReportStatus::Normal;
    else if (status == QStringLiteral("FULL"))
        result.network.options_report.status = HydraulicSimulationReportStatus::Full;

    const QString statistic = settings.value(QStringLiteral("reportStatistic")).toString().trimmed().toUpper();
    if (statistic == QStringLiteral("AVERAGE"))
        result.network.report_statistic = HydraulicSimulationReportStatistic::Average;
    else if (statistic == QStringLiteral("MINIMUM"))
        result.network.report_statistic = HydraulicSimulationReportStatistic::Minimum;
    else if (statistic == QStringLiteral("MAXIMUM"))
        result.network.report_statistic = HydraulicSimulationReportStatistic::Maximum;
    else if (statistic == QStringLiteral("RANGE"))
        result.network.report_statistic = HydraulicSimulationReportStatistic::Range;
    else if (statistic == QStringLiteral("SERIES") || statistic == QStringLiteral("NONE"))
        result.network.report_statistic = HydraulicSimulationReportStatistic::Series;
}

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
            HydraulicCurveValveHeadloss curve;
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


std::optional<double> finiteVariantDouble(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return std::nullopt;

    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number))
        return std::nullopt;
    return number;
}

QString firstUnit(const QJsonObject &units, const QStringList &keys)
{
    for (const QString &key : keys)
    {
        const QString unit = units.value(key).toString().trimmed();
        if (!unit.isEmpty())
            return unit;
    }
    return {};
}

QString importedEntityId(
    const QVariantMap &row,
    const QString &table_name,
    qint64 source_id,
    EpanetJsProjectConversionResult &result)
{
    const QString label = row.value(QStringLiteral("label")).toString().trimmed();
    if (!label.isEmpty())
        return label;

    const QString generated = QStringLiteral("%1-%2").arg(table_name).arg(source_id);
    appendDiagnostic(
        result,
        EpanetJsConversionDiagnosticSeverity::Warning,
        QStringLiteral("generated-entity-id"),
        QStringLiteral("epanet-js %1 id %2 has no label; AOWIS generated id '%3'.")
            .arg(table_name)
            .arg(source_id)
            .arg(generated),
        table_name,
        source_id);
    return generated;
}

bool importWgs84Coordinate(
    const QVariantMap &row,
    const QJsonObject &project_settings,
    const QString &table_name,
    qint64 source_id,
    CoordinateWGS84 &coordinate,
    EpanetJsProjectConversionResult &result)
{
    const QJsonObject projection = project_settings.value(QStringLiteral("projection")).toObject();
    const QString projection_type = projection.value(QStringLiteral("type")).toString().trimmed().toLower();
    const QString projection_id = projection.value(QStringLiteral("id")).toString().trimmed().toLower();
    const bool projection_unspecified = projection_type.isEmpty() && projection_id.isEmpty();
    const bool wgs84 = projection_unspecified
        || projection_type == QStringLiteral("wgs84")
        || projection_type == QStringLiteral("epsg:4326")
        || projection_type == QStringLiteral("4326")
        || projection_id == QStringLiteral("wgs84")
        || projection_id == QStringLiteral("epsg:4326")
        || projection_id == QStringLiteral("4326");
    if (!wgs84)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("unsupported-coordinate-projection"),
            QStringLiteral("epanet-js %1 id %2 uses projection '%3'; AOWIS currently only converts WGS84 coordinates from epanet-js projects safely.")
                .arg(table_name)
                .arg(source_id)
                .arg(projection_id.isEmpty() ? projection_type : projection_id),
            table_name,
            source_id);
        return false;
    }

    const std::optional<double> longitude = finiteVariantDouble(row.value(QStringLiteral("coord_x")));
    const std::optional<double> latitude = finiteVariantDouble(row.value(QStringLiteral("coord_y")));
    if (!longitude.has_value() || !latitude.has_value()
        || *longitude < -180.0 || *longitude > 180.0
        || *latitude < -90.0 || *latitude > 90.0)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-node-coordinate"),
            QStringLiteral("epanet-js %1 id %2 has an invalid WGS84 coordinate.")
                .arg(table_name)
                .arg(source_id),
            table_name,
            source_id);
        return false;
    }

    coordinate.longitude_deg = *longitude;
    coordinate.latitude_deg = *latitude;
    return true;
}

bool importLengthValue(
    const QVariantMap &row,
    const QString &column,
    const QString &unit,
    double &target,
    const QString &table_name,
    qint64 source_id,
    EpanetJsProjectConversionResult &result,
    bool required = false)
{
    if (!row.contains(column) || row.value(column).isNull())
    {
        if (required)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("missing-node-value"),
                QStringLiteral("epanet-js %1 id %2 is missing required field '%3'.")
                    .arg(table_name)
                    .arg(source_id)
                    .arg(column),
                table_name,
                source_id);
            return false;
        }
        return true;
    }

    const std::optional<double> value = finiteVariantDouble(row.value(column));
    const std::optional<double> converted = value.has_value() ? lengthToM(*value, unit) : std::nullopt;
    if (!converted.has_value())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("unsupported-node-length"),
            QStringLiteral("AOWIS cannot convert epanet-js %1 id %2 field '%3' with unit '%4'.")
                .arg(table_name)
                .arg(source_id)
                .arg(column)
                .arg(unit),
            table_name,
            source_id);
        return false;
    }
    target = *converted;
    return true;
}

bool importVolumeValue(
    const QVariantMap &row,
    const QString &column,
    const QString &unit,
    double &target,
    const QString &table_name,
    qint64 source_id,
    EpanetJsProjectConversionResult &result)
{
    if (!row.contains(column) || row.value(column).isNull())
        return true;

    const std::optional<double> value = finiteVariantDouble(row.value(column));
    const std::optional<double> converted = value.has_value() ? volumeToM3(*value, unit) : std::nullopt;
    if (!converted.has_value())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("unsupported-node-volume"),
            QStringLiteral("AOWIS cannot convert epanet-js %1 id %2 field '%3' with unit '%4'.")
                .arg(table_name)
                .arg(source_id)
                .arg(column)
                .arg(unit),
            table_name,
            source_id);
        return false;
    }
    target = *converted;
    return true;
}

bool networkHasPattern(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicPatternTime &pattern : network.patterns_time)
    {
        if (pattern.uuid == uuid)
            return true;
    }
    return false;
}

bool networkHasTankVolumeCurve(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicCurveTankVolume &curve : network.curves_tank_volume)
    {
        if (curve.uuid == uuid)
            return true;
    }
    return false;
}

HydraulicNodeJunction *junctionByUuid(NetworkHydraulic &network, const QUuid &uuid)
{
    for (HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (junction.uuid == uuid)
            return &junction;
    }
    return nullptr;
}

double approximateDistanceMeters(
    const CoordinateWGS84 &from,
    const CoordinateWGS84 &to);

const HydraulicLinkPipe *pipeByUuid(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (pipe.uuid == uuid)
            return &pipe;
    }
    return nullptr;
}

std::optional<CoordinateWGS84> nodeCoordinateByUuid(
    const NetworkHydraulic &network,
    const QUuid &uuid)
{
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (junction.uuid == uuid)
            return junction.coordinate_wgs84;
    }
    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
    {
        if (reservoir.uuid == uuid)
            return reservoir.coordinate_wgs84;
    }
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (tank.uuid == uuid)
            return tank.coordinate_wgs84;
    }
    return std::nullopt;
}

std::optional<double> normalizedPipePosition(
    const NetworkHydraulic &network,
    const HydraulicLinkPipe &pipe,
    const CoordinateWGS84 &snap_coordinate)
{
    const std::optional<CoordinateWGS84> from_coordinate =
        nodeCoordinateByUuid(network, pipe.node_uuid_from);
    const std::optional<CoordinateWGS84> to_coordinate =
        nodeCoordinateByUuid(network, pipe.node_uuid_to);
    if (!from_coordinate.has_value() || !to_coordinate.has_value())
        return std::nullopt;

    QList<CoordinateWGS84> coordinates;
    coordinates.reserve(pipe.vertices.size() + 2);
    coordinates.append(from_coordinate.value());
    for (const HydraulicLinkVertex &vertex : pipe.vertices)
        coordinates.append(vertex.coordinate_wgs84);
    coordinates.append(to_coordinate.value());
    if (coordinates.size() < 2)
        return std::nullopt;

    double total_length_m = 0.0;
    QList<double> segment_lengths_m;
    segment_lengths_m.reserve(coordinates.size() - 1);
    for (qsizetype index = 1; index < coordinates.size(); ++index)
    {
        const double segment_length_m = approximateDistanceMeters(
            coordinates.at(index - 1), coordinates.at(index));
        segment_lengths_m.append(segment_length_m);
        total_length_m += segment_length_m;
    }
    if (!std::isfinite(total_length_m) || total_length_m <= 0.0)
        return std::nullopt;

    constexpr double degrees_to_radians = 0.017453292519943295769236907684886;
    const double longitude_scale = std::max(
        1.0e-12,
        std::abs(std::cos(snap_coordinate.latitude_deg * degrees_to_radians)));

    double best_distance_squared = std::numeric_limits<double>::infinity();
    double best_distance_along_m = 0.0;
    double cumulative_length_m = 0.0;
    for (qsizetype index = 1; index < coordinates.size(); ++index)
    {
        const CoordinateWGS84 &start = coordinates.at(index - 1);
        const CoordinateWGS84 &end = coordinates.at(index);

        const double start_x =
            (start.longitude_deg - snap_coordinate.longitude_deg) * longitude_scale;
        const double start_y = start.latitude_deg - snap_coordinate.latitude_deg;
        const double end_x =
            (end.longitude_deg - snap_coordinate.longitude_deg) * longitude_scale;
        const double end_y = end.latitude_deg - snap_coordinate.latitude_deg;
        const double delta_x = end_x - start_x;
        const double delta_y = end_y - start_y;
        const double segment_length_squared = delta_x * delta_x + delta_y * delta_y;

        double segment_fraction = 0.0;
        if (segment_length_squared > 0.0)
        {
            segment_fraction = std::clamp(
                -(start_x * delta_x + start_y * delta_y) / segment_length_squared,
                0.0,
                1.0);
        }

        const double projected_x = start_x + segment_fraction * delta_x;
        const double projected_y = start_y + segment_fraction * delta_y;
        const double distance_squared = projected_x * projected_x + projected_y * projected_y;
        if (distance_squared < best_distance_squared)
        {
            best_distance_squared = distance_squared;
            best_distance_along_m = cumulative_length_m
                + segment_fraction * segment_lengths_m.at(index - 1);
        }
        cumulative_length_m += segment_lengths_m.at(index - 1);
    }

    return std::clamp(best_distance_along_m / total_length_m, 0.0, 1.0);
}

void importJunctions(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("junctions"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString elevation_unit = firstUnit(
        units, QStringList{QStringLiteral("elevation"), QStringLiteral("head"), QStringLiteral("level")});

    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value() || !result.id_map.contains(QStringLiteral("junctions"), *source_id))
            continue;

        HydraulicNodeJunction junction;
        junction.id = importedEntityId(row, QStringLiteral("junctions"), *source_id, result);
        junction.uuid = result.id_map.uuidFor(QStringLiteral("junctions"), *source_id);
        junction.metadata.enabled = !row.contains(QStringLiteral("is_active"))
            || row.value(QStringLiteral("is_active")).toInt() != 0;
        junction.elevation_input_type = HydraulicNodeElevationInputType::TotalElevation;

        importWgs84Coordinate(
            row, project_settings, QStringLiteral("junctions"), *source_id,
            junction.coordinate_wgs84, result);
        importLengthValue(
            row, QStringLiteral("elevation"), elevation_unit, junction.elevation_m,
            QStringLiteral("junctions"), *source_id, result);

        result.network.nodes_junctions.append(junction);
    }
}

void importReservoirs(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("reservoirs"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString head_unit = firstUnit(
        units, QStringList{QStringLiteral("head"), QStringLiteral("elevation"), QStringLiteral("level")});

    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value() || !result.id_map.contains(QStringLiteral("reservoirs"), *source_id))
            continue;

        HydraulicNodeReservoir reservoir;
        reservoir.id = importedEntityId(row, QStringLiteral("reservoirs"), *source_id, result);
        reservoir.uuid = result.id_map.uuidFor(QStringLiteral("reservoirs"), *source_id);
        reservoir.metadata.enabled = !row.contains(QStringLiteral("is_active"))
            || row.value(QStringLiteral("is_active")).toInt() != 0;
        reservoir.head_input_type = HydraulicNodeElevationInputType::TotalHead;

        importWgs84Coordinate(
            row, project_settings, QStringLiteral("reservoirs"), *source_id,
            reservoir.coordinate_wgs84, result);
        importLengthValue(
            row, QStringLiteral("head"), head_unit, reservoir.hydraulic_head_m,
            QStringLiteral("reservoirs"), *source_id, result, true);

        if (row.contains(QStringLiteral("head_pattern_id"))
            && !row.value(QStringLiteral("head_pattern_id")).isNull())
        {
            const std::optional<qint64> pattern_id = integerValue(
                row.value(QStringLiteral("head_pattern_id")));
            const QUuid pattern_uuid = pattern_id.has_value()
                ? result.id_map.uuidFor(QStringLiteral("patterns"), *pattern_id)
                : QUuid();
            if (pattern_id.has_value() && *pattern_id <= 0)
            {
                // EPANET-style zero references mean no pattern.
            }
            else if (!pattern_id.has_value() || pattern_uuid.isNull()
                || !networkHasPattern(result.network, pattern_uuid))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-reservoir-pattern-reference"),
                    QStringLiteral("epanet-js reservoir id %1 references a missing head pattern.")
                        .arg(*source_id),
                    QStringLiteral("reservoirs"),
                    *source_id);
            }
            else
            {
                reservoir.head_pattern_mode = HydraulicTimePatternMode::TimePattern;
                reservoir.head_pattern_uuid = pattern_uuid;
            }
        }

        result.network.nodes_reservoirs.append(reservoir);
    }
}

void importTanks(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("tanks"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString elevation_unit = firstUnit(
        units, QStringList{QStringLiteral("elevation"), QStringLiteral("head"), QStringLiteral("level")});
    const QString initial_level_unit = firstUnit(
        units, QStringList{QStringLiteral("initialLevel"), QStringLiteral("level"), QStringLiteral("elevation")});
    const QString minimum_level_unit = firstUnit(
        units, QStringList{QStringLiteral("minLevel"), QStringLiteral("level"), QStringLiteral("elevation")});
    const QString maximum_level_unit = firstUnit(
        units, QStringList{QStringLiteral("maxLevel"), QStringLiteral("level"), QStringLiteral("elevation")});
    const QString diameter_unit = firstUnit(
        units, QStringList{QStringLiteral("tankDiameter"), QStringLiteral("length"), QStringLiteral("elevation")});
    const QString volume_unit = firstUnit(
        units, QStringList{QStringLiteral("minVolume"), QStringLiteral("volume")});

    constexpr double pi = 3.141592653589793238462643383279502884;

    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value() || !result.id_map.contains(QStringLiteral("tanks"), *source_id))
            continue;

        HydraulicNodeTank tank;
        tank.id = importedEntityId(row, QStringLiteral("tanks"), *source_id, result);
        tank.uuid = result.id_map.uuidFor(QStringLiteral("tanks"), *source_id);
        tank.metadata.enabled = !row.contains(QStringLiteral("is_active"))
            || row.value(QStringLiteral("is_active")).toInt() != 0;
        tank.elevation_input_type = HydraulicNodeTankElevationInputType::BottomElevation;
        tank.geometry_input_type = HydraulicNodeTankGeometryInputType::Cylindrical;

        importWgs84Coordinate(
            row, project_settings, QStringLiteral("tanks"), *source_id,
            tank.coordinate_wgs84, result);
        importLengthValue(
            row, QStringLiteral("elevation"), elevation_unit, tank.bottom_elevation_m,
            QStringLiteral("tanks"), *source_id, result, true);
        importLengthValue(
            row, QStringLiteral("initial_level"), initial_level_unit, tank.water_level_initial_m,
            QStringLiteral("tanks"), *source_id, result, true);
        importLengthValue(
            row, QStringLiteral("min_level"), minimum_level_unit, tank.water_level_minimum_m,
            QStringLiteral("tanks"), *source_id, result, true);
        importLengthValue(
            row, QStringLiteral("max_level"), maximum_level_unit, tank.water_level_maximum_m,
            QStringLiteral("tanks"), *source_id, result, true);
        importLengthValue(
            row, QStringLiteral("diameter"), diameter_unit, tank.diameter_m,
            QStringLiteral("tanks"), *source_id, result);
        importVolumeValue(
            row, QStringLiteral("min_volume"), volume_unit, tank.minimum_volume_m3,
            QStringLiteral("tanks"), *source_id, result);

        if (tank.diameter_m > 0.0)
        {
            tank.cross_section_area_m2 = pi * tank.diameter_m * tank.diameter_m / 4.0;
            tank.volume_at_maximum_level_m3 = tank.minimum_volume_m3
                + tank.cross_section_area_m2
                    * (tank.water_level_maximum_m - tank.water_level_minimum_m);
        }

        if (row.contains(QStringLiteral("volume_curve_id"))
            && !row.value(QStringLiteral("volume_curve_id")).isNull())
        {
            const std::optional<qint64> curve_id = integerValue(
                row.value(QStringLiteral("volume_curve_id")));
            const QUuid curve_uuid = curve_id.has_value()
                ? result.id_map.uuidFor(QStringLiteral("curves"), *curve_id)
                : QUuid();
            if (curve_id.has_value() && *curve_id <= 0)
            {
                // EPANET-style zero references mean no volume curve.
            }
            else if (!curve_id.has_value() || curve_uuid.isNull()
                || !networkHasTankVolumeCurve(result.network, curve_uuid))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-tank-volume-curve-reference"),
                    QStringLiteral("epanet-js tank id %1 references a missing or non-volume curve.")
                        .arg(*source_id),
                    QStringLiteral("tanks"),
                    *source_id);
            }
            else
            {
                tank.geometry_input_type = HydraulicNodeTankGeometryInputType::VolumeCurve;
                tank.volume_curve_uuid = curve_uuid;
            }
        }

        if (row.contains(QStringLiteral("overflow")) && !row.value(QStringLiteral("overflow")).isNull())
            tank.can_overflow = row.value(QStringLiteral("overflow")).toInt() != 0;

        result.network.nodes_tanks.append(tank);
    }
}

void importJunctionDemands(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("junction_demands"));
    if (table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString demand_unit = firstUnit(
        units, QStringList{QStringLiteral("baseDemand"), QStringLiteral("flow")});

    struct SourceDemand
    {
        qint64 junction_id = 0;
        qint64 ordinal = 0;
        QVariantMap row;
    };

    QList<SourceDemand> demands;
    for (const QVariantMap &row : table->rows)
    {
        const std::optional<qint64> junction_id = integerValue(
            row.value(QStringLiteral("junction_id")));
        const std::optional<qint64> ordinal = integerValue(row.value(QStringLiteral("ordinal")));
        if (!junction_id.has_value() || !ordinal.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-junction-demand-reference"),
                QStringLiteral("epanet-js junction_demands contains an invalid junction_id or ordinal."),
                QStringLiteral("junction_demands"));
            continue;
        }
        SourceDemand source;
        source.junction_id = *junction_id;
        source.ordinal = *ordinal;
        source.row = row;
        demands.append(source);
    }

    std::sort(
        demands.begin(), demands.end(),
        [](const SourceDemand &left, const SourceDemand &right)
        {
            if (left.junction_id != right.junction_id)
                return left.junction_id < right.junction_id;
            return left.ordinal < right.ordinal;
        });

    for (const SourceDemand &source : demands)
    {
        const QUuid junction_uuid = result.id_map.uuidFor(
            QStringLiteral("junctions"), source.junction_id);
        HydraulicNodeJunction *junction = junctionByUuid(result.network, junction_uuid);
        if (junction == nullptr)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("missing-junction-demand-reference"),
                QStringLiteral("epanet-js junction demand references missing junction id %1.")
                    .arg(source.junction_id),
                QStringLiteral("junction_demands"),
                source.junction_id);
            continue;
        }

        const std::optional<double> source_base_demand = finiteVariantDouble(
            source.row.value(QStringLiteral("base_demand")));
        const std::optional<double> base_demand = source_base_demand.has_value()
            ? flowToM3PerH(*source_base_demand, demand_unit)
            : std::nullopt;
        if (!base_demand.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unsupported-junction-demand"),
                QStringLiteral("AOWIS cannot convert epanet-js junction %1 demand ordinal %2 with unit '%3'.")
                    .arg(source.junction_id)
                    .arg(source.ordinal)
                    .arg(demand_unit),
                QStringLiteral("junction_demands"),
                source.junction_id);
            continue;
        }

        HydraulicDemand demand;
        demand.base_demand_m3_per_h = *base_demand;

        if (source.row.contains(QStringLiteral("pattern_id"))
            && !source.row.value(QStringLiteral("pattern_id")).isNull())
        {
            const std::optional<qint64> pattern_id = integerValue(
                source.row.value(QStringLiteral("pattern_id")));
            const QUuid pattern_uuid = pattern_id.has_value()
                ? result.id_map.uuidFor(QStringLiteral("patterns"), *pattern_id)
                : QUuid();
            if (pattern_id.has_value() && *pattern_id <= 0)
            {
                // EPANET-style zero references mean no pattern.
            }
            else if (!pattern_id.has_value() || pattern_uuid.isNull()
                || !networkHasPattern(result.network, pattern_uuid))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-demand-pattern-reference"),
                    QStringLiteral("epanet-js junction %1 demand ordinal %2 references a missing pattern.")
                        .arg(source.junction_id)
                        .arg(source.ordinal),
                    QStringLiteral("junction_demands"),
                    source.junction_id);
            }
            else
            {
                demand.pattern_mode = HydraulicTimePatternMode::TimePattern;
                demand.pattern_uuid = pattern_uuid;
            }
        }

        junction->demands.append(demand);
    }
}

void importNodes(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    importJunctions(project, project_settings, result);
    importReservoirs(project, project_settings, result);
    importTanks(project, project_settings, result);
    importJunctionDemands(project, project_settings, result);
}

double approximateDistanceMeters(
    const CoordinateWGS84 &from,
    const CoordinateWGS84 &to)
{
    constexpr double earth_radius_m = 6371008.8;
    constexpr double degrees_to_radians = 0.017453292519943295769236907684886;
    const double latitude_from = from.latitude_deg * degrees_to_radians;
    const double latitude_to = to.latitude_deg * degrees_to_radians;
    const double delta_latitude = (to.latitude_deg - from.latitude_deg) * degrees_to_radians;
    const double delta_longitude = (to.longitude_deg - from.longitude_deg) * degrees_to_radians;
    const double sine_latitude = std::sin(delta_latitude * 0.5);
    const double sine_longitude = std::sin(delta_longitude * 0.5);
    const double haversine = sine_latitude * sine_latitude
        + std::cos(latitude_from) * std::cos(latitude_to)
            * sine_longitude * sine_longitude;
    const double bounded = std::max(0.0, std::min(1.0, haversine));
    return 2.0 * earth_radius_m * std::asin(std::sqrt(bounded));
}

std::optional<QList<CoordinateWGS84>> linkCoordinates(
    const QVariantMap &row,
    qint64 source_id,
    const QString &table_name,
    const QString &entity_name,
    EpanetJsProjectConversionResult &result)
{
    if (!row.contains(QStringLiteral("coords")) || row.value(QStringLiteral("coords")).isNull())
        return QList<CoordinateWGS84>();

    const QString text = row.value(QStringLiteral("coords")).toString().trimmed();
    if (text.isEmpty())
        return QList<CoordinateWGS84>();

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isArray())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-%1-coordinates").arg(entity_name),
            QStringLiteral("epanet-js %1 id %2 has invalid coords JSON.").arg(entity_name).arg(source_id),
            table_name,
            source_id);
        return std::nullopt;
    }

    const QJsonArray points = document.array();
    if (points.size() < 2)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-%1-coordinates").arg(entity_name),
            QStringLiteral("epanet-js %1 id %2 has fewer than two coordinates.").arg(entity_name).arg(source_id),
            table_name,
            source_id);
        return std::nullopt;
    }

    QList<CoordinateWGS84> coordinates;
    for (const QJsonValue &point_value : points)
    {
        double longitude = 0.0;
        double latitude = 0.0;
        bool valid = false;

        if (point_value.isArray())
        {
            const QJsonArray point = point_value.toArray();
            const std::optional<double> x = point.size() >= 2 ? finiteDouble(point.at(0)) : std::nullopt;
            const std::optional<double> y = point.size() >= 2 ? finiteDouble(point.at(1)) : std::nullopt;
            if (x.has_value() && y.has_value())
            {
                longitude = *x;
                latitude = *y;
                valid = true;
            }
        }
        else if (point_value.isObject())
        {
            const QJsonObject point = point_value.toObject();
            const QJsonValue longitude_value = point.contains(QStringLiteral("longitude"))
                ? point.value(QStringLiteral("longitude"))
                : point.contains(QStringLiteral("lon"))
                    ? point.value(QStringLiteral("lon"))
                    : point.value(QStringLiteral("x"));
            const QJsonValue latitude_value = point.contains(QStringLiteral("latitude"))
                ? point.value(QStringLiteral("latitude"))
                : point.contains(QStringLiteral("lat"))
                    ? point.value(QStringLiteral("lat"))
                    : point.value(QStringLiteral("y"));
            const std::optional<double> x = finiteDouble(longitude_value);
            const std::optional<double> y = finiteDouble(latitude_value);
            if (x.has_value() && y.has_value())
            {
                longitude = *x;
                latitude = *y;
                valid = true;
            }
        }

        if (!valid || longitude < -180.0 || longitude > 180.0
            || latitude < -90.0 || latitude > 90.0)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-%1-coordinates").arg(entity_name),
                QStringLiteral("epanet-js %1 id %2 contains an invalid WGS84 coordinate.").arg(entity_name).arg(source_id),
                table_name,
                source_id);
            return std::nullopt;
        }

        CoordinateWGS84 coordinate;
        coordinate.longitude_deg = longitude;
        coordinate.latitude_deg = latitude;
        coordinates.append(coordinate);
    }

    return coordinates;
}

QString inferredPipeLengthUnit(const QJsonObject &units)
{
    const QString explicit_unit = firstUnit(
        units, QStringList{QStringLiteral("length"), QStringLiteral("elevation")});
    if (!explicit_unit.isEmpty())
        return explicit_unit;

    const QString flow_unit = units.value(QStringLiteral("flow")).toString();
    if (flowUnitUsesMetricLength(flow_unit))
        return QStringLiteral("m");
    if (flowUnitUsesFootLength(flow_unit))
        return QStringLiteral("ft");
    return {};
}

QString inferredPipeDiameterUnit(const QJsonObject &units)
{
    const QString explicit_unit = units.value(QStringLiteral("diameter")).toString().trimmed();
    if (!explicit_unit.isEmpty())
        return explicit_unit;

    const QString flow_unit = units.value(QStringLiteral("flow")).toString();
    if (flowUnitUsesMetricLength(flow_unit))
        return QStringLiteral("mm");
    if (flowUnitUsesFootLength(flow_unit))
        return QStringLiteral("in");
    return {};
}

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
    const QString length_unit = inferredPipeLengthUnit(units);
    const QString diameter_unit = inferredPipeDiameterUnit(units);
    const QString roughness_unit = units.value(QStringLiteral("roughness")).toString().trimmed();
    const QString flow_unit = units.value(QStringLiteral("flow")).toString().trimmed();

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
        const HydraulicPipeMaterial *material = pipeMaterialByLabel(
            result.network, source_material);
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


std::optional<double> powerToKw(double value, const QString &unit)
{
    const QString normalized = normalizedUnit(unit);
    if (normalized == QStringLiteral("kw") || normalized == QStringLiteral("kilowatt")
        || normalized == QStringLiteral("kilowatts"))
        return value;
    if (normalized == QStringLiteral("hp") || normalized == QStringLiteral("horsepower"))
        return value * 0.7456998715822702;
    return std::nullopt;
}

QString inferredPowerUnit(const QJsonObject &units)
{
    const QString explicit_unit = units.value(QStringLiteral("power")).toString().trimmed();
    if (!explicit_unit.isEmpty())
        return explicit_unit;

    const QString flow_unit = units.value(QStringLiteral("flow")).toString();
    if (flowUnitUsesMetricLength(flow_unit))
        return QStringLiteral("kW");
    if (flowUnitUsesFootLength(flow_unit))
        return QStringLiteral("hp");
    return {};
}

bool networkHasPumpHeadCurve(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicCurvePumpHead &curve : network.curves_pump_head)
    {
        if (curve.uuid == uuid)
            return true;
    }
    return false;
}

int pumpHeadCurvePointCount(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicCurvePumpHead &curve : network.curves_pump_head)
    {
        if (curve.uuid == uuid)
            return curve.points.size();
    }
    return 0;
}

bool networkHasPumpEfficiencyCurve(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicCurvePumpEfficiency &curve : network.curves_pump_efficiency)
    {
        if (curve.uuid == uuid)
            return true;
    }
    return false;
}

bool networkHasValveHeadlossCurve(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicCurveValveHeadloss &curve : network.curves_valve_headloss)
    {
        if (curve.uuid == uuid)
            return true;
    }
    return false;
}

bool networkHasValveCharacteristicCurve(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicCurveValveCharacteristic &curve : network.curves_valve_characteristic)
    {
        if (curve.uuid == uuid)
            return true;
    }
    return false;
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


void importCustomerPoints(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *points_table = tableByName(
        project, QStringLiteral("customer_points"));
    if (points_table == nullptr)
        return;

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString demand_unit = firstUnit(
        units,
        QStringList{
            QStringLiteral("customerDemand"),
            QStringLiteral("baseDemand"),
            QStringLiteral("flow")});

    struct SourceDemand
    {
        qint64 customer_point_id = 0;
        qint64 ordinal = 0;
        QVariantMap row;
    };

    QMap<qint64, QList<HydraulicDemand>> demands_by_customer_point;
    const EpanetJsTableSnapshot *demands_table = tableByName(
        project, QStringLiteral("customer_point_demands"));
    if (demands_table != nullptr)
    {
        QList<SourceDemand> source_demands;
        for (const QVariantMap &row : demands_table->rows)
        {
            const std::optional<qint64> customer_point_id = integerValue(
                row.value(QStringLiteral("customer_point_id")));
            const std::optional<qint64> ordinal = integerValue(
                row.value(QStringLiteral("ordinal")));
            if (!customer_point_id.has_value() || !ordinal.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-customer-demand-reference"),
                    QStringLiteral("epanet-js customer_point_demands contains an invalid customer_point_id or ordinal."),
                    QStringLiteral("customer_point_demands"));
                continue;
            }
            if (!result.id_map.contains(QStringLiteral("customer_points"), *customer_point_id))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-customer-demand-point-reference"),
                    QStringLiteral("epanet-js customer demand references missing customer point id %1.")
                        .arg(*customer_point_id),
                    QStringLiteral("customer_point_demands"),
                    *customer_point_id);
                continue;
            }

            SourceDemand source;
            source.customer_point_id = *customer_point_id;
            source.ordinal = *ordinal;
            source.row = row;
            source_demands.append(source);
        }

        std::sort(
            source_demands.begin(), source_demands.end(),
            [](const SourceDemand &left, const SourceDemand &right)
            {
                if (left.customer_point_id != right.customer_point_id)
                    return left.customer_point_id < right.customer_point_id;
                return left.ordinal < right.ordinal;
            });

        for (const SourceDemand &source : source_demands)
        {
            const std::optional<double> source_base_demand = finiteVariantDouble(
                source.row.value(QStringLiteral("base_demand")));
            const std::optional<double> base_demand = source_base_demand.has_value()
                ? flowToM3PerH(*source_base_demand, demand_unit)
                : std::nullopt;
            if (!base_demand.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("unsupported-customer-demand"),
                    QStringLiteral("AOWIS cannot convert epanet-js customer point %1 demand ordinal %2 with unit '%3'.")
                        .arg(source.customer_point_id)
                        .arg(source.ordinal)
                        .arg(demand_unit),
                    QStringLiteral("customer_point_demands"),
                    source.customer_point_id);
                continue;
            }

            HydraulicDemand demand;
            demand.base_demand_m3_per_h = *base_demand;

            if (source.row.contains(QStringLiteral("pattern_id"))
                && !source.row.value(QStringLiteral("pattern_id")).isNull())
            {
                const std::optional<qint64> pattern_id = integerValue(
                    source.row.value(QStringLiteral("pattern_id")));
                const QUuid pattern_uuid = pattern_id.has_value()
                    ? result.id_map.uuidFor(QStringLiteral("patterns"), *pattern_id)
                    : QUuid();
                if (pattern_id.has_value() && *pattern_id <= 0)
                {
                    // EPANET-style zero references mean no pattern.
                }
                else if (!pattern_id.has_value() || pattern_uuid.isNull()
                    || !networkHasPattern(result.network, pattern_uuid))
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("missing-customer-demand-pattern-reference"),
                        QStringLiteral("epanet-js customer point %1 demand ordinal %2 references a missing pattern.")
                            .arg(source.customer_point_id)
                            .arg(source.ordinal),
                        QStringLiteral("customer_point_demands"),
                        source.customer_point_id);
                }
                else
                {
                    demand.pattern_mode = HydraulicTimePatternMode::TimePattern;
                    demand.pattern_uuid = pattern_uuid;
                }
            }

            demands_by_customer_point[source.customer_point_id].append(demand);
        }
    }

    for (const QVariantMap &row : points_table->rows)
    {
        const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
        if (!source_id.has_value()
            || !result.id_map.contains(QStringLiteral("customer_points"), *source_id))
        {
            continue;
        }

        HydraulicDemandPoint demand_point;
        demand_point.id = importedEntityId(
            row, QStringLiteral("customer_points"), *source_id, result);
        demand_point.uuid = result.id_map.uuidFor(
            QStringLiteral("customer_points"), *source_id);
        importWgs84Coordinate(
            row, project_settings, QStringLiteral("customer_points"), *source_id,
            demand_point.coordinate_wgs84, result);
        demand_point.demands = demands_by_customer_point.value(*source_id);

        const bool has_pipe_reference = row.contains(QStringLiteral("pipe_id"))
            && !row.value(QStringLiteral("pipe_id")).isNull();
        const bool has_junction_reference = row.contains(QStringLiteral("junction_id"))
            && !row.value(QStringLiteral("junction_id")).isNull();
        const std::optional<qint64> pipe_id = has_pipe_reference
            ? integerValue(row.value(QStringLiteral("pipe_id")))
            : std::nullopt;
        const std::optional<qint64> junction_id = has_junction_reference
            ? integerValue(row.value(QStringLiteral("junction_id")))
            : std::nullopt;

        if (has_pipe_reference && !pipe_id.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-customer-pipe-reference"),
                QStringLiteral("epanet-js customer point id %1 has an invalid pipe_id.")
                    .arg(*source_id),
                QStringLiteral("customer_points"),
                *source_id);
        }
        else if (has_junction_reference && !junction_id.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-customer-junction-reference"),
                QStringLiteral("epanet-js customer point id %1 has an invalid junction_id.")
                    .arg(*source_id),
                QStringLiteral("customer_points"),
                *source_id);
        }
        else if (pipe_id.has_value())
        {
            const QUuid pipe_uuid = result.id_map.uuidFor(
                QStringLiteral("pipes"), *pipe_id);
            const HydraulicLinkPipe *pipe = pipeByUuid(result.network, pipe_uuid);
            if (pipe == nullptr)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-customer-pipe-reference"),
                    QStringLiteral("epanet-js customer point id %1 references missing pipe id %2.")
                        .arg(*source_id)
                        .arg(*pipe_id),
                    QStringLiteral("customer_points"),
                    *source_id);
            }
            else if (!junction_id.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-customer-assigned-junction"),
                    QStringLiteral("epanet-js customer point id %1 is pipe-attached but has no assigned junction.")
                        .arg(*source_id),
                    QStringLiteral("customer_points"),
                    *source_id);
            }
            else
            {
                const QUuid assigned_junction_uuid = result.id_map.uuidFor(
                    QStringLiteral("junctions"), *junction_id);
                if (assigned_junction_uuid.isNull()
                    || junctionByUuid(result.network, assigned_junction_uuid) == nullptr)
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("missing-customer-junction-reference"),
                        QStringLiteral("epanet-js customer point id %1 references missing junction id %2.")
                            .arg(*source_id)
                            .arg(*junction_id),
                        QStringLiteral("customer_points"),
                        *source_id);
                }
                else if (assigned_junction_uuid != pipe->node_uuid_from
                    && assigned_junction_uuid != pipe->node_uuid_to)
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("invalid-customer-assigned-junction"),
                        QStringLiteral("epanet-js customer point id %1 assigns demand to junction id %2, which is not an endpoint of pipe id %3.")
                            .arg(*source_id)
                            .arg(*junction_id)
                            .arg(*pipe_id),
                        QStringLiteral("customer_points"),
                        *source_id);
                }
                else
                {
                    CoordinateWGS84 snap_coordinate = demand_point.coordinate_wgs84;
                    const std::optional<double> snap_x = finiteVariantDouble(
                        row.value(QStringLiteral("snap_x")));
                    const std::optional<double> snap_y = finiteVariantDouble(
                        row.value(QStringLiteral("snap_y")));
                    if (snap_x.has_value() && snap_y.has_value()
                        && *snap_x >= -180.0 && *snap_x <= 180.0
                        && *snap_y >= -90.0 && *snap_y <= 90.0)
                    {
                        snap_coordinate.longitude_deg = *snap_x;
                        snap_coordinate.latitude_deg = *snap_y;
                    }
                    else
                    {
                        appendDiagnostic(
                            result,
                            EpanetJsConversionDiagnosticSeverity::Warning,
                            QStringLiteral("missing-customer-snap-coordinate"),
                            QStringLiteral("epanet-js customer point id %1 has no valid snap coordinate; AOWIS projected the customer point coordinate onto its pipe instead.")
                                .arg(*source_id),
                            QStringLiteral("customer_points"),
                            *source_id);
                    }

                    const std::optional<double> pipe_position = normalizedPipePosition(
                        result.network, *pipe, snap_coordinate);
                    if (!pipe_position.has_value())
                    {
                        appendDiagnostic(
                            result,
                            EpanetJsConversionDiagnosticSeverity::Error,
                            QStringLiteral("invalid-customer-pipe-position"),
                            QStringLiteral("AOWIS could not resolve the attachment position of epanet-js customer point id %1 on pipe id %2.")
                                .arg(*source_id)
                                .arg(*pipe_id),
                            QStringLiteral("customer_points"),
                            *source_id);
                    }
                    else
                    {
                        demand_point.attachment.type = HydraulicDemandPointAttachmentType::Pipe;
                        demand_point.attachment.pipe_uuid = pipe_uuid;
                        demand_point.attachment.pipe_position = *pipe_position;
                        demand_point.attachment.pipe_allocation_mode =
                            HydraulicDemandPointPipeAllocationMode::AssignedJunction;
                        demand_point.attachment.pipe_assigned_junction_uuid =
                            assigned_junction_uuid;
                    }
                }
            }
        }
        else if (junction_id.has_value())
        {
            const QUuid junction_uuid = result.id_map.uuidFor(
                QStringLiteral("junctions"), *junction_id);
            if (junction_uuid.isNull()
                || junctionByUuid(result.network, junction_uuid) == nullptr)
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("missing-customer-junction-reference"),
                    QStringLiteral("epanet-js customer point id %1 references missing junction id %2.")
                        .arg(*source_id)
                        .arg(*junction_id),
                    QStringLiteral("customer_points"),
                    *source_id);
            }
            else
            {
                demand_point.attachment.type = HydraulicDemandPointAttachmentType::Junction;
                demand_point.attachment.junction_uuid = junction_uuid;
            }
        }
        else if (!demand_point.demands.isEmpty())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unattached-customer-demand"),
                QStringLiteral("epanet-js customer point id %1 has demand but no network attachment.")
                    .arg(*source_id),
                QStringLiteral("customer_points"),
                *source_id);
        }

        result.network.demand_points.append(demand_point);
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

    const std::optional<QList<QPair<double, double>>> source_points = curvePoints(points_text);
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
    const QString power_unit = inferredPowerUnit(units);
    const QString head_unit = firstUnit(
        units, QStringList{QStringLiteral("head"), QStringLiteral("elevation")});

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
                if (pattern_uuid.isNull() || !networkHasPattern(result.network, pattern_uuid))
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
                    || !networkHasPumpHeadCurve(result.network, head_curve_uuid))
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
                const int point_count = pumpHeadCurvePointCount(result.network, head_curve_uuid);
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
                    || !networkHasPumpEfficiencyCurve(result.network, curve_uuid))
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
                    || !networkHasPattern(result.network, pattern_uuid))
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
    const QString diameter_unit = inferredPipeDiameterUnit(units);
    const QString pressure_unit = units.value(QStringLiteral("pressure")).toString().trimmed();
    const QString flow_unit = units.value(QStringLiteral("flow")).toString().trimmed();

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
                    || !networkHasValveHeadlossCurve(result.network, curve_uuid))
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
                    || !networkHasValveCharacteristicCurve(result.network, curve_uuid))
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

bool networkHasPump(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicLinkPump &pump : network.links_pumps)
    {
        if (pump.uuid == uuid)
            return true;
    }
    return false;
}

bool networkHasTank(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (tank.uuid == uuid)
            return true;
    }
    return false;
}

QString structuredControlSourceId(const QJsonObject &object, qsizetype index)
{
    const QString id = object.value(QStringLiteral("id")).toString().trimmed();
    if (!id.isEmpty())
        return id;
    return QStringLiteral("control-%1").arg(index + 1);
}

bool importStructuredPumpLevelAction(
    const QJsonObject &action_object,
    const QString &source_control_id,
    const QString &suffix,
    HydraulicControlSimpleType control_type,
    HydraulicControlActionType fallback_action,
    const QUuid &pump_uuid,
    const QUuid &tank_uuid,
    const QString &level_unit,
    EpanetJsProjectConversionResult &result)
{
    const std::optional<double> source_level = finiteDouble(
        action_object.value(QStringLiteral("level")));
    const std::optional<double> level_m = source_level.has_value()
        ? lengthToM(*source_level, level_unit)
        : std::nullopt;
    if (!level_m.has_value())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-level-setting-control-level"),
            QStringLiteral("epanet-js level-setting control '%1' has an invalid %2 level for unit '%3'.")
                .arg(source_control_id, suffix, level_unit),
            QStringLiteral("controls"));
        return false;
    }

    HydraulicControlSimple control;
    control.id = QStringLiteral("%1_%2").arg(source_control_id, suffix.toUpper());
    control.uuid = uuidV5(
        result.network.uuid,
        QStringLiteral("epanet-js/controls/%1/%2")
            .arg(source_control_id, suffix)
            .toUtf8());
    control.type = control_type;
    control.link_uuid = pump_uuid;
    control.trigger_node_uuid = tank_uuid;
    control.trigger_water_level_m = *level_m;
    control.action = fallback_action;

    if (action_object.contains(QStringLiteral("setting"))
        && !action_object.value(QStringLiteral("setting")).isNull())
    {
        const std::optional<double> setting = finiteDouble(
            action_object.value(QStringLiteral("setting")));
        if (!setting.has_value() || *setting < 0.0)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-level-setting-control-setting"),
                QStringLiteral("epanet-js level-setting control '%1' has an invalid %2 pump setting.")
                    .arg(source_control_id, suffix),
                QStringLiteral("controls"));
            return false;
        }
        control.action = HydraulicControlActionType::Setting;
        control.setting.pump_speed_ratio = *setting;
    }

    result.network.controls_simple.append(control);
    return true;
}

void importStructuredControls(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("controls"));
    if (table == nullptr || table->rows.isEmpty())
        return;

    const QString data = table->rows.first().value(QStringLiteral("data")).toString().trimmed();
    if (data.isEmpty())
        return;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isArray())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-controls-json"),
            QStringLiteral("epanet-js controls.data is not a valid JSON array."),
            QStringLiteral("controls"));
        return;
    }

    const QJsonObject units = projectUnitsObject(project_settings);
    const QString level_unit = firstUnit(
        units,
        QStringList{
            QStringLiteral("level"),
            QStringLiteral("initialLevel"),
            QStringLiteral("length"),
            QStringLiteral("elevation")});

    const QJsonArray controls = document.array();
    for (qsizetype index = 0; index < controls.size(); ++index)
    {
        const QJsonValue control_value = controls.at(index);
        if (!control_value.isObject())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-structured-control"),
                QStringLiteral("epanet-js structured control %1 is not an object.")
                    .arg(index + 1),
                QStringLiteral("controls"));
            continue;
        }

        const QJsonObject control_object = control_value.toObject();
        const QString source_control_id = structuredControlSourceId(control_object, index);
        const QString type = control_object.value(QStringLiteral("type"))
            .toString().trimmed().toLower();
        if (type != QStringLiteral("level-setting"))
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unsupported-structured-control-type"),
                QStringLiteral("epanet-js structured control '%1' uses unsupported type '%2'.")
                    .arg(source_control_id, type),
                QStringLiteral("controls"));
            continue;
        }

        const std::optional<qint64> link_id = integerValue(
            control_object.value(QStringLiteral("linkId")).toVariant());
        const std::optional<qint64> tank_id = integerValue(
            control_object.value(QStringLiteral("tankId")).toVariant());
        const QUuid pump_uuid = link_id.has_value()
            ? result.id_map.uuidFor(QStringLiteral("pumps"), *link_id)
            : QUuid();
        const QUuid tank_uuid = tank_id.has_value()
            ? result.id_map.uuidFor(QStringLiteral("tanks"), *tank_id)
            : QUuid();

        if (!link_id.has_value() || pump_uuid.isNull()
            || !networkHasPump(result.network, pump_uuid))
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("missing-level-setting-link-reference"),
                QStringLiteral("epanet-js level-setting control '%1' references a missing or non-pump link.")
                    .arg(source_control_id),
                QStringLiteral("controls"));
            continue;
        }
        if (!tank_id.has_value() || tank_uuid.isNull()
            || !networkHasTank(result.network, tank_uuid))
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("missing-level-setting-tank-reference"),
                QStringLiteral("epanet-js level-setting control '%1' references a missing tank.")
                    .arg(source_control_id),
                QStringLiteral("controls"));
            continue;
        }

        bool imported_action = false;
        const QJsonValue on_value = control_object.value(QStringLiteral("on"));
        if (on_value.isObject())
        {
            imported_action = importStructuredPumpLevelAction(
                on_value.toObject(),
                source_control_id,
                QStringLiteral("on"),
                HydraulicControlSimpleType::LowLevel,
                HydraulicControlActionType::Open,
                pump_uuid,
                tank_uuid,
                level_unit,
                result) || imported_action;
        }

        const QJsonValue off_value = control_object.value(QStringLiteral("off"));
        if (off_value.isObject())
        {
            imported_action = importStructuredPumpLevelAction(
                off_value.toObject(),
                source_control_id,
                QStringLiteral("off"),
                HydraulicControlSimpleType::HighLevel,
                HydraulicControlActionType::Close,
                pump_uuid,
                tank_uuid,
                level_unit,
                result) || imported_action;
        }

        if (!imported_action && !on_value.isObject() && !off_value.isObject())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("empty-level-setting-control"),
                QStringLiteral("epanet-js level-setting control '%1' has neither an on nor off action.")
                    .arg(source_control_id),
                QStringLiteral("controls"));
        }
    }
}

const HydraulicLinkValve *networkValveByUuid(
    const NetworkHydraulic &network,
    const QUuid &uuid)
{
    for (const HydraulicLinkValve &valve : network.links_valves)
    {
        if (valve.uuid == uuid)
            return &valve;
    }
    return nullptr;
}

bool applyRawSimpleControlAction(
    const QString &action_text,
    const QUuid &link_uuid,
    const QJsonObject &project_settings,
    const QString &source_control_id,
    HydraulicControlSimple &control,
    EpanetJsProjectConversionResult &result)
{
    const QString normalized = action_text.trimmed().toUpper();
    if (normalized == QStringLiteral("OPEN"))
    {
        control.action = HydraulicControlActionType::Open;
        return true;
    }
    if (normalized == QStringLiteral("CLOSED") || normalized == QStringLiteral("CLOSE"))
    {
        control.action = HydraulicControlActionType::Close;
        return true;
    }

    bool number_ok = false;
    const double setting = action_text.toDouble(&number_ok);
    if (!number_ok || !std::isfinite(setting) || setting < 0.0)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-raw-simple-control-action"),
            QStringLiteral("epanet-js raw simple control '%1' has unsupported action '%2'.")
                .arg(source_control_id, action_text),
            QStringLiteral("raw_controls"));
        return false;
    }

    control.action = HydraulicControlActionType::Setting;
    if (networkHasPump(result.network, link_uuid))
    {
        control.setting.pump_speed_ratio = setting;
        return true;
    }

    const HydraulicLinkValve *valve = networkValveByUuid(result.network, link_uuid);
    if (valve == nullptr)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("unsupported-raw-simple-control-setting-target"),
            QStringLiteral("epanet-js raw simple control '%1' uses numeric setting %2 on a link type that cannot accept a numeric AOWIS simple-control setting.")
                .arg(source_control_id)
                .arg(setting),
            QStringLiteral("raw_controls"));
        return false;
    }

    const QJsonObject units = projectUnitsObject(project_settings);
    switch (valve->type)
    {
    case HydraulicLinkValveType::PRV:
    case HydraulicLinkValveType::PSV:
    case HydraulicLinkValveType::PBV:
    {
        const QString pressure_unit = firstUnit(
            units, QStringList{QStringLiteral("pressure")});
        const std::optional<double> converted = pressureToHeadM(
            setting, pressure_unit, result.network.options_hydraulic.specific_gravity);
        if (!converted.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unsupported-raw-control-valve-pressure-unit"),
                QStringLiteral("AOWIS cannot convert the pressure setting in epanet-js raw simple control '%1' with unit '%2'.")
                    .arg(source_control_id, pressure_unit),
                QStringLiteral("raw_controls"));
            return false;
        }
        control.setting.valve_pressure_head_m = *converted;
        return true;
    }
    case HydraulicLinkValveType::FCV:
    {
        const QString flow_unit = firstUnit(
            units, QStringList{QStringLiteral("flow")});
        const std::optional<double> converted = flowToM3PerH(setting, flow_unit);
        if (!converted.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unsupported-raw-control-valve-flow-unit"),
                QStringLiteral("AOWIS cannot convert the flow setting in epanet-js raw simple control '%1' with unit '%2'.")
                    .arg(source_control_id, flow_unit),
                QStringLiteral("raw_controls"));
            return false;
        }
        control.setting.valve_flow_m3_per_h = *converted;
        return true;
    }
    case HydraulicLinkValveType::TCV:
        control.setting.valve_loss_coefficient = setting;
        return true;
    case HydraulicLinkValveType::PCV:
        control.setting.valve_position_percent = setting;
        return true;
    case HydraulicLinkValveType::GPV:
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("unsupported-raw-control-gpv-setting"),
            QStringLiteral("epanet-js raw simple control '%1' uses a numeric GPV setting that the AOWIS simple-control model cannot represent faithfully.")
                .arg(source_control_id),
            QStringLiteral("raw_controls"));
        return false;
    }

    return false;
}

std::optional<qint64> rawControlAssetId(
    const QJsonArray &asset_references,
    int placeholder_index,
    bool expected_action_target,
    const QString &source_control_id,
    EpanetJsProjectConversionResult &result)
{
    if (placeholder_index < 0 || placeholder_index >= asset_references.size()
        || !asset_references.at(placeholder_index).isObject())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-raw-control-asset-reference"),
            QStringLiteral("epanet-js raw simple control '%1' references missing asset placeholder %2.")
                .arg(source_control_id)
                .arg(placeholder_index),
            QStringLiteral("raw_controls"));
        return std::nullopt;
    }

    const QJsonObject reference = asset_references.at(placeholder_index).toObject();
    const std::optional<qint64> asset_id = integerValue(
        reference.value(QStringLiteral("assetId")).toVariant());
    if (!asset_id.has_value() || *asset_id <= 0)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-raw-control-asset-reference"),
            QStringLiteral("epanet-js raw simple control '%1' has an invalid asset id at placeholder %2.")
                .arg(source_control_id)
                .arg(placeholder_index),
            QStringLiteral("raw_controls"));
        return std::nullopt;
    }

    const bool is_action_target = reference.value(QStringLiteral("isActionTarget")).toBool(false);
    if (is_action_target != expected_action_target)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-raw-control-asset-role"),
            QStringLiteral("epanet-js raw simple control '%1' has an unexpected asset role at placeholder %2.")
                .arg(source_control_id)
                .arg(placeholder_index),
            QStringLiteral("raw_controls"));
        return std::nullopt;
    }

    return asset_id;
}

bool importRawSimpleLevelControl(
    const QJsonObject &simple_control,
    qsizetype index,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const QString source_control_id = QStringLiteral("raw-simple-%1").arg(index + 1);
    const QString control_template = simple_control.value(QStringLiteral("template"))
        .toString().trimmed();

    const QString number_pattern = QStringLiteral(
        "[-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][-+]?\\d+)?");
    const QRegularExpression expression(
        QStringLiteral(
            "^\\s*LINK\\s+\\{\\{(\\d+)\\}\\}\\s+(OPEN|CLOSED|CLOSE|%1)"
            "\\s+IF\\s+NODE\\s+\\{\\{(\\d+)\\}\\}\\s+(BELOW|ABOVE)\\s+(%1)\\s*$")
            .arg(number_pattern),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(control_template);
    if (!match.hasMatch())
        return false;

    bool link_placeholder_ok = false;
    bool node_placeholder_ok = false;
    const int link_placeholder = match.captured(1).toInt(&link_placeholder_ok);
    const int node_placeholder = match.captured(3).toInt(&node_placeholder_ok);
    if (!link_placeholder_ok || !node_placeholder_ok)
        return false;

    const QJsonArray asset_references = simple_control.value(
        QStringLiteral("assetReferences")).toArray();
    const std::optional<qint64> link_id = rawControlAssetId(
        asset_references, link_placeholder, true, source_control_id, result);
    const std::optional<qint64> node_id = rawControlAssetId(
        asset_references, node_placeholder, false, source_control_id, result);
    if (!link_id.has_value() || !node_id.has_value())
        return true;

    const QUuid link_uuid = result.id_map.linkUuid(*link_id);
    const QUuid node_uuid = result.id_map.nodeUuid(*node_id);
    if (link_uuid.isNull())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("missing-raw-control-link-reference"),
            QStringLiteral("epanet-js raw simple control '%1' references missing or ambiguous link id %2.")
                .arg(source_control_id)
                .arg(*link_id),
            QStringLiteral("raw_controls"));
        return true;
    }
    if (node_uuid.isNull())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("missing-raw-control-node-reference"),
            QStringLiteral("epanet-js raw simple control '%1' references missing or ambiguous node id %2.")
                .arg(source_control_id)
                .arg(*node_id),
            QStringLiteral("raw_controls"));
        return true;
    }

    bool threshold_ok = false;
    const double source_threshold = match.captured(5).toDouble(&threshold_ok);
    if (!threshold_ok || !std::isfinite(source_threshold))
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-raw-control-trigger-value"),
            QStringLiteral("epanet-js raw simple control '%1' has an invalid trigger value.")
                .arg(source_control_id),
            QStringLiteral("raw_controls"));
        return true;
    }

    HydraulicControlSimple control;
    control.id = QStringLiteral("RAW_SIMPLE_%1").arg(index + 1);
    control.uuid = uuidV5(
        result.network.uuid,
        QStringLiteral("epanet-js/raw-controls/simple/%1").arg(index).toUtf8());
    control.type = match.captured(4).compare(
        QStringLiteral("BELOW"), Qt::CaseInsensitive) == 0
        ? HydraulicControlSimpleType::LowLevel
        : HydraulicControlSimpleType::HighLevel;
    control.link_uuid = link_uuid;
    control.trigger_node_uuid = node_uuid;

    const QList<QString> node_tables = result.id_map.nodeTablesForId(*node_id);
    if (node_tables.size() != 1)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("ambiguous-raw-control-node-reference"),
            QStringLiteral("epanet-js raw simple control '%1' cannot determine the node type for id %2.")
                .arg(source_control_id)
                .arg(*node_id),
            QStringLiteral("raw_controls"));
        return true;
    }

    const QJsonObject units = projectUnitsObject(project_settings);
    if (node_tables.first() == QStringLiteral("junctions"))
    {
        const QString pressure_unit = firstUnit(
            units, QStringList{QStringLiteral("pressure")});
        const std::optional<double> threshold = pressureToHeadM(
            source_threshold, pressure_unit, result.network.options_hydraulic.specific_gravity);
        if (!threshold.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unsupported-raw-control-pressure-unit"),
                QStringLiteral("AOWIS cannot convert the junction-pressure trigger in epanet-js raw simple control '%1' with unit '%2'.")
                    .arg(source_control_id, pressure_unit),
                QStringLiteral("raw_controls"));
            return true;
        }
        control.trigger_pressure_head_m = *threshold;
    }
    else
    {
        const QString level_unit = firstUnit(
            units,
            QStringList{QStringLiteral("level"), QStringLiteral("initialLevel"),
                        QStringLiteral("length"), QStringLiteral("elevation")});
        const std::optional<double> threshold = lengthToM(source_threshold, level_unit);
        if (!threshold.has_value())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unsupported-raw-control-level-unit"),
                QStringLiteral("AOWIS cannot convert the tank/reservoir level trigger in epanet-js raw simple control '%1' with unit '%2'.")
                    .arg(source_control_id, level_unit),
                QStringLiteral("raw_controls"));
            return true;
        }
        control.trigger_water_level_m = *threshold;
    }

    if (!applyRawSimpleControlAction(
            match.captured(2), link_uuid, project_settings, source_control_id, control, result))
    {
        return true;
    }

    result.network.controls_simple.append(control);
    return true;
}

void importRawControls(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    const EpanetJsTableSnapshot *table = tableByName(project, QStringLiteral("raw_controls"));
    if (table == nullptr || table->rows.isEmpty())
        return;

    const QString data = table->rows.first().value(QStringLiteral("data")).toString().trimmed();
    if (data.isEmpty())
        return;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("invalid-raw-controls-json"),
            QStringLiteral("epanet-js raw_controls.data is not a valid JSON object."),
            QStringLiteral("raw_controls"));
        return;
    }

    const QJsonObject object = document.object();
    const QJsonArray simple_controls = object.value(QStringLiteral("simple")).toArray();
    for (qsizetype index = 0; index < simple_controls.size(); ++index)
    {
        if (!simple_controls.at(index).isObject())
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("invalid-raw-simple-control"),
                QStringLiteral("epanet-js raw simple control %1 is not an object.")
                    .arg(index + 1),
                QStringLiteral("raw_controls"));
            continue;
        }

        const QJsonObject simple_control = simple_controls.at(index).toObject();
        if (!importRawSimpleLevelControl(
                simple_control, index, project_settings, result))
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("unsupported-raw-simple-control"),
                QStringLiteral("epanet-js raw simple control %1 uses a template that AOWIS does not translate yet: %2")
                    .arg(index + 1)
                    .arg(simple_control.value(QStringLiteral("template")).toString()),
                QStringLiteral("raw_controls"));
        }
    }

    const QJsonArray rules = object.value(QStringLiteral("rules")).toArray();
    if (!rules.isEmpty())
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("unsupported-raw-rules"),
            QStringLiteral("epanet-js project contains %1 raw rule(s); rule-based control translation is not implemented yet.")
                .arg(rules.size()),
            QStringLiteral("raw_controls"));
    }
}

void importControls(
    const EpanetJsProjectSnapshot &project,
    const QJsonObject &project_settings,
    EpanetJsProjectConversionResult &result)
{
    importStructuredControls(project, project_settings, result);
    importRawControls(project, project_settings, result);
}

void registerEntityIds(
    const EpanetJsProjectSnapshot &project,
    EpanetJsProjectConversionResult &result)
{
    for (const QString &table_name : entityTables())
    {
        const EpanetJsTableSnapshot *table = tableByName(project, table_name);
        if (table == nullptr)
            continue;

        if (!tableHasColumn(table, QStringLiteral("id")))
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("missing-id-column"),
                QStringLiteral("epanet-js table '%1' has no id column.").arg(table_name),
                table_name);
            continue;
        }

        QSet<qint64> seen_ids;
        for (const QVariantMap &row : table->rows)
        {
            const std::optional<qint64> source_id = integerValue(row.value(QStringLiteral("id")));
            if (!source_id.has_value())
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("invalid-source-id"),
                    QStringLiteral("epanet-js table '%1' contains a row with a non-integer id.")
                        .arg(table_name),
                    table_name);
                continue;
            }

            if (seen_ids.contains(*source_id))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("duplicate-source-id"),
                    QStringLiteral("epanet-js table '%1' contains duplicate id %2.")
                        .arg(table_name)
                        .arg(*source_id),
                    table_name,
                    *source_id);
                continue;
            }
            seen_ids.insert(*source_id);

            QString registration_error;
            if (!result.id_map.registerId(table_name, *source_id, &registration_error))
            {
                appendDiagnostic(
                    result,
                    EpanetJsConversionDiagnosticSeverity::Error,
                    QStringLiteral("id-map-registration-failed"),
                    registration_error,
                    table_name,
                    *source_id);
            }
        }
    }
}

void validateReferenceDomains(
    const EpanetJsProjectSnapshot &project,
    EpanetJsProjectConversionResult &result)
{
    const QList<QStringList> domains = {nodeTables(), linkTables()};
    const QStringList domain_names = {
        QStringLiteral("node"),
        QStringLiteral("link")
    };

    for (int domain_index = 0; domain_index < domains.size(); ++domain_index)
    {
        QMap<qint64, QStringList> tables_by_id;
        for (const QString &table_name : domains.at(domain_index))
        {
            const EpanetJsTableSnapshot *table = tableByName(project, table_name);
            if (table == nullptr)
                continue;

            for (const QVariantMap &row : table->rows)
            {
                const std::optional<qint64> source_id = integerValue(
                    row.value(QStringLiteral("id")));
                if (!source_id.has_value())
                    continue;
                tables_by_id[*source_id].append(table_name);
            }
        }

        for (QMap<qint64, QStringList>::const_iterator iterator = tables_by_id.cbegin();
             iterator != tables_by_id.cend(); ++iterator)
        {
            QStringList tables = iterator.value();
            tables.removeDuplicates();
            if (tables.size() <= 1)
                continue;

            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("ambiguous-%1-id").arg(domain_names.at(domain_index)),
                QStringLiteral("epanet-js %1 id %2 occurs in multiple tables: %3.")
                    .arg(domain_names.at(domain_index))
                    .arg(iterator.key())
                    .arg(tables.join(QStringLiteral(", "))),
                tables.first(),
                iterator.key());
        }
    }
}

void validateLinkEndpointReferences(
    const EpanetJsProjectSnapshot &project,
    EpanetJsProjectConversionResult &result)
{
    for (const QString &table_name : linkTables())
    {
        const EpanetJsTableSnapshot *table = tableByName(project, table_name);
        if (table == nullptr)
            continue;

        const bool has_start = tableHasColumn(table, QStringLiteral("start_node_id"));
        const bool has_end = tableHasColumn(table, QStringLiteral("end_node_id"));
        if (!has_start || !has_end)
        {
            appendDiagnostic(
                result,
                EpanetJsConversionDiagnosticSeverity::Error,
                QStringLiteral("missing-endpoint-column"),
                QStringLiteral("epanet-js table '%1' is missing start_node_id or end_node_id.")
                    .arg(table_name),
                table_name);
            continue;
        }

        for (const QVariantMap &row : table->rows)
        {
            const std::optional<qint64> link_id = integerValue(row.value(QStringLiteral("id")));
            for (const QString &column_name : {QStringLiteral("start_node_id"),
                                               QStringLiteral("end_node_id")})
            {
                const std::optional<qint64> node_id = integerValue(row.value(column_name));
                if (!node_id.has_value())
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("invalid-node-reference"),
                        QStringLiteral("epanet-js %1 id %2 has a non-integer %3.")
                            .arg(table_name)
                            .arg(link_id.has_value() ? QString::number(*link_id) : QStringLiteral("?"))
                            .arg(column_name),
                        table_name,
                        link_id);
                    continue;
                }

                const QList<QString> matching_tables = result.id_map.nodeTablesForId(*node_id);
                if (matching_tables.isEmpty())
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("missing-node-reference"),
                        QStringLiteral("epanet-js %1 id %2 references missing node id %3 through %4.")
                            .arg(table_name)
                            .arg(link_id.has_value() ? QString::number(*link_id) : QStringLiteral("?"))
                            .arg(*node_id)
                            .arg(column_name),
                        table_name,
                        link_id);
                }
                else if (matching_tables.size() > 1)
                {
                    appendDiagnostic(
                        result,
                        EpanetJsConversionDiagnosticSeverity::Error,
                        QStringLiteral("ambiguous-node-reference"),
                        QStringLiteral("epanet-js node id %1 occurs in multiple node tables: %2.")
                            .arg(*node_id)
                            .arg(matching_tables.join(QStringLiteral(", "))),
                        table_name,
                        link_id);
                }
            }
        }
    }
}
}

EpanetJsProjectIdMap::EpanetJsProjectIdMap(const QUuid &project_uuid)
{
    this->project_uuid = project_uuid;
}

QUuid EpanetJsProjectIdMap::projectUuid() const
{
    return this->project_uuid;
}

bool EpanetJsProjectIdMap::registerId(
    const QString &table_name,
    qint64 source_id,
    QString *error)
{
    if (this->project_uuid.isNull())
    {
        if (error != nullptr)
            *error = QStringLiteral("Cannot register epanet-js ids without a project UUID.");
        return false;
    }
    if (table_name.isEmpty())
    {
        if (error != nullptr)
            *error = QStringLiteral("Cannot register an epanet-js id without a table name.");
        return false;
    }

    QMap<qint64, QUuid> &table_map = this->uuids_by_table[table_name];
    if (table_map.contains(source_id))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("epanet-js table '%1' already contains id %2.")
                .arg(table_name)
                .arg(source_id);
        }
        return false;
    }

    const QByteArray stable_name = table_name.toUtf8()
        + QByteArrayLiteral("/")
        + QByteArray::number(source_id);
    table_map.insert(source_id, uuidV5(this->project_uuid, stable_name));
    return true;
}

bool EpanetJsProjectIdMap::contains(const QString &table_name, qint64 source_id) const
{
    const QMap<QString, QMap<qint64, QUuid>>::const_iterator table_iterator =
        this->uuids_by_table.constFind(table_name);
    if (table_iterator == this->uuids_by_table.cend())
        return false;
    return table_iterator.value().contains(source_id);
}

QUuid EpanetJsProjectIdMap::uuidFor(const QString &table_name, qint64 source_id) const
{
    const QMap<QString, QMap<qint64, QUuid>>::const_iterator table_iterator =
        this->uuids_by_table.constFind(table_name);
    if (table_iterator == this->uuids_by_table.cend())
        return {};
    return table_iterator.value().value(source_id);
}

qsizetype EpanetJsProjectIdMap::size() const
{
    qsizetype count = 0;
    for (const QMap<qint64, QUuid> &table_map : this->uuids_by_table)
        count += table_map.size();
    return count;
}

QList<QString> EpanetJsProjectIdMap::nodeTablesForId(qint64 source_id) const
{
    QList<QString> matches;
    for (const QString &table_name : nodeTables())
    {
        if (contains(table_name, source_id))
            matches.append(table_name);
    }
    return matches;
}

QList<QString> EpanetJsProjectIdMap::linkTablesForId(qint64 source_id) const
{
    QList<QString> matches;
    for (const QString &table_name : linkTables())
    {
        if (contains(table_name, source_id))
            matches.append(table_name);
    }
    return matches;
}

QUuid EpanetJsProjectIdMap::nodeUuid(qint64 source_id) const
{
    const QList<QString> matches = nodeTablesForId(source_id);
    if (matches.size() != 1)
        return {};
    return uuidFor(matches.first(), source_id);
}

QUuid EpanetJsProjectIdMap::linkUuid(qint64 source_id) const
{
    const QList<QString> matches = linkTablesForId(source_id);
    if (matches.size() != 1)
        return {};
    return uuidFor(matches.first(), source_id);
}

bool EpanetJsProjectConversionResult::hasErrors() const
{
    for (const EpanetJsConversionDiagnostic &diagnostic : this->diagnostics)
    {
        if (diagnostic.severity == EpanetJsConversionDiagnosticSeverity::Error)
            return true;
    }
    return false;
}

QString EpanetJsProjectConversionResult::errorSummary() const
{
    QStringList messages;
    for (const EpanetJsConversionDiagnostic &diagnostic : this->diagnostics)
    {
        if (diagnostic.severity == EpanetJsConversionDiagnosticSeverity::Error)
            messages.append(diagnostic.message);
    }
    return messages.join(QStringLiteral("\n"));
}

EpanetJsProjectConversionResult EpanetJsProjectConverter::convert(
    const EpanetJsProjectSnapshot &project)
{
    EpanetJsProjectConversionResult result;

    const QJsonObject settings = projectSettingsObject(project);
    const QString unique_id_text = settings.value(QStringLiteral("uniqueId")).toString();
    const QUuid source_project_uuid(unique_id_text);

    QUuid project_uuid = source_project_uuid;
    if (project_uuid.isNull())
    {
        const QByteArray identity_seed = fallbackProjectIdentitySeed(project);
        project_uuid = uuidV5(aowisEpanetJsNamespace(), identity_seed);
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Warning,
            QStringLiteral("generated-project-uuid"),
            QStringLiteral(
                "The epanet-js project has no valid settings.uniqueId; AOWIS generated a deterministic project UUID from the project contents."));
    }

    result.network.uuid = project_uuid;
    result.network.id = settings.value(QStringLiteral("name")).toString().trimmed();
    if (result.network.id.isEmpty())
        result.network.id = QStringLiteral("epanet-js-project");
    result.id_map = EpanetJsProjectIdMap(project_uuid);

    registerEntityIds(project, result);
    validateReferenceDomains(project, result);
    validateLinkEndpointReferences(project, result);

    mapHeadlossFormula(settings, result);
    importPipeMaterials(project, settings, result);
    importPatterns(project, result);
    importCurves(project, settings, result);
    mapSimulationSettings(project, settings, result);
    importNodes(project, settings, result);
    importPipes(project, settings, result);
    importCustomerPoints(project, settings, result);
    importPumpsAndValves(project, settings, result);
    importControls(project, settings, result);

    if (result.id_map.size() == 0)
    {
        appendDiagnostic(
            result,
            EpanetJsConversionDiagnosticSeverity::Error,
            QStringLiteral("no-convertible-entities"),
            QStringLiteral("The epanet-js project contains no entity ids that AOWIS can convert."));
    }

    result.success = !result.hasErrors();
    return result;
}
