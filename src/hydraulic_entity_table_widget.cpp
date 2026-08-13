#include "hydraulic_entity_table_widget.h"

#include <cmath>
#include <optional>

#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QComboBox>
#include <QBrush>
#include <QColor>
#include <QDate>
#include <QFont>
#include <QFrame>
#include <QFontMetrics>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QList>
#include <QModelIndex>
#include <QSortFilterProxyModel>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSet>
#include <QStringList>
#include <QTableView>
#include <QVariant>
#include <QVBoxLayout>
#include <QResizeEvent>

#include "hydraulic_data.h"
#include "network_symbology_values.h"

namespace
{
constexpr int SortRole = Qt::UserRole;
constexpr int EntityUuidRole = Qt::UserRole + 1;

enum class ColumnFilterKind
{
    None,
    Text,
    Choice
};

struct TableColumn
{
    QString title;
    bool simulation_result = false;
    ColumnFilterKind filter_kind = ColumnFilterKind::None;
};

struct TableCell
{
    QString display;
    QVariant sort_value;
    Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter;
    QString tooltip;
};

struct TableRow
{
    QUuid uuid;
    QList<TableCell> cells;
    bool simulation_error = false;
    QString simulation_error_tooltip;
};

QString formatNumber(double value, int decimals, const QString &unit = QString())
{
    if (!std::isfinite(value))
        return QStringLiteral("—");

    QString text = QString::number(value, 'f', decimals);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    if (text == QStringLiteral("-0"))
        text = QStringLiteral("0");

    return text + unit;
}

TableCell emptyCell()
{
    TableCell cell;
    cell.display = QStringLiteral("—");
    return cell;
}

TableCell textCell(const QString &value)
{
    TableCell cell;
    cell.display = value.isEmpty() ? QStringLiteral("—") : value;
    cell.sort_value = value;
    return cell;
}

TableCell numberCell(double value, int decimals, const QString &unit = QString())
{
    TableCell cell;
    if (std::isfinite(value))
    {
        cell.display = formatNumber(value, decimals, unit);
        cell.sort_value = value;
    }
    else
    {
        cell.display = QStringLiteral("—");
    }
    cell.alignment = Qt::AlignRight | Qt::AlignVCenter;
    return cell;
}

TableCell optionalNumberCell(const std::optional<double> &value, int decimals,
                             const QString &unit = QString())
{
    if (!value.has_value())
        return emptyCell();

    return numberCell(value.value(), decimals, unit);
}

TableCell boolCell(bool value, const QString &true_text = QStringLiteral("Yes"),
                   const QString &false_text = QStringLiteral("No"))
{
    TableCell cell;
    cell.display = value ? true_text : false_text;
    cell.sort_value = value;
    cell.alignment = Qt::AlignCenter;
    return cell;
}

TableCell integerCell(int value)
{
    TableCell cell;
    cell.display = QString::number(value);
    cell.sort_value = value;
    cell.alignment = Qt::AlignRight | Qt::AlignVCenter;
    return cell;
}

TableCell dateCell(const std::optional<QDate> &date)
{
    if (!date.has_value())
        return emptyCell();

    TableCell cell;
    cell.display = date->toString(QStringLiteral("yyyy-MM-dd"));
    cell.sort_value = date.value();
    return cell;
}

QString entityModelRoleText(EntityModelRole role)
{
    switch (role)
    {
    case EntityModelRole::Unspecified:
        return QStringLiteral("Unspecified");
    case EntityModelRole::ExistingAsset:
        return QStringLiteral("Existing Asset");
    case EntityModelRole::PlannedAsset:
        return QStringLiteral("Planned Asset");
    case EntityModelRole::VirtualModelElement:
        return QStringLiteral("Virtual / Model-Only");
    case EntityModelRole::BoundaryCondition:
        return QStringLiteral("Boundary Condition");
    case EntityModelRole::TemporaryTesting:
        return QStringLiteral("Temporary / Testing");
    case EntityModelRole::RetiredAsset:
        return QStringLiteral("Retired Asset");
    }

    return QStringLiteral("Unknown");
}

QString elevationInputText(HydraulicNodeElevationInputType input_type)
{
    if (input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        return QStringLiteral("Terrain Elevation + Offset");

    return QStringLiteral("Direct Value");
}

QString tankGeometryInputText(HydraulicNodeTankGeometryInputType input_type)
{
    switch (input_type)
    {
    case HydraulicNodeTankGeometryInputType::Cylindrical:
        return QStringLiteral("Cylindrical");
    case HydraulicNodeTankGeometryInputType::UniformArea:
        return QStringLiteral("Uniform Area");
    case HydraulicNodeTankGeometryInputType::VolumeAtMaximumLevel:
        return QStringLiteral("Maximum Volume");
    case HydraulicNodeTankGeometryInputType::VolumeCurve:
        return QStringLiteral("Volume Curve");
    }

    return QStringLiteral("Unknown");
}

QString patternModeText(HydraulicTimePatternMode pattern_mode)
{
    return pattern_mode == HydraulicTimePatternMode::TimePattern
        ? QStringLiteral("Time Pattern") : QStringLiteral("Constant");
}

QString demandSourceMethodText(HydraulicNodeJunctionDemandSourceMethod source_method)
{
    switch (source_method)
    {
    case HydraulicNodeJunctionDemandSourceMethod::ManualEstimation:
        return QStringLiteral("Manual Estimation");
    case HydraulicNodeJunctionDemandSourceMethod::MeterData:
        return QStringLiteral("Meter Data");
    case HydraulicNodeJunctionDemandSourceMethod::Scenario:
        return QStringLiteral("Scenario");
    }

    return QStringLiteral("Unknown");
}

QString pipeInitialStatusText(HydraulicLinkPipeInitialStatus status)
{
    switch (status)
    {
    case HydraulicLinkPipeInitialStatus::Open:
        return QStringLiteral("Open");
    case HydraulicLinkPipeInitialStatus::Closed:
        return QStringLiteral("Closed");
    case HydraulicLinkPipeInitialStatus::CheckValve:
        return QStringLiteral("Check Valve");
    }

    return QStringLiteral("Unknown");
}

QString pumpDefinitionText(HydraulicLinkPumpDefinitionType definition_type)
{
    switch (definition_type)
    {
    case HydraulicLinkPumpDefinitionType::ConstantPower:
        return QStringLiteral("Constant Power");
    case HydraulicLinkPumpDefinitionType::OnePointCurve:
        return QStringLiteral("1-Point Curve");
    case HydraulicLinkPumpDefinitionType::ThreePointCurve:
        return QStringLiteral("3-Point Curve");
    case HydraulicLinkPumpDefinitionType::Library:
        return QStringLiteral("Library Curve");
    case HydraulicLinkPumpDefinitionType::MultiPointCurve:
        return QStringLiteral("Multi-Point Curve");
    }

    return QStringLiteral("Unknown");
}

QString pumpInitialStatusText(HydraulicLinkPumpInitialStatus status)
{
    return status == HydraulicLinkPumpInitialStatus::On
        ? QStringLiteral("On") : QStringLiteral("Off");
}

QString pumpControlTypeText(HydraulicLinkPumpControlType control_type)
{
    switch (control_type)
    {
    case HydraulicLinkPumpControlType::None:
        return QStringLiteral("None");
    case HydraulicLinkPumpControlType::LevelBased:
        return QStringLiteral("Level-Based");
    case HydraulicLinkPumpControlType::TimeBased:
        return QStringLiteral("Time-Based");
    }

    return QStringLiteral("Unknown");
}

QString pumpEfficiencyInputText(HydraulicLinkPumpEfficiencyInputType input_type)
{
    switch (input_type)
    {
    case HydraulicLinkPumpEfficiencyInputType::Global:
        return QStringLiteral("Global");
    case HydraulicLinkPumpEfficiencyInputType::Constant:
        return QStringLiteral("Constant");
    case HydraulicLinkPumpEfficiencyInputType::Curve:
        return QStringLiteral("Curve");
    }

    return QStringLiteral("Unknown");
}

QString pumpEnergyPriceInputText(HydraulicLinkPumpEnergyPriceInputType input_type)
{
    switch (input_type)
    {
    case HydraulicLinkPumpEnergyPriceInputType::Global:
        return QStringLiteral("Global");
    case HydraulicLinkPumpEnergyPriceInputType::Constant:
        return QStringLiteral("Constant");
    case HydraulicLinkPumpEnergyPriceInputType::Pattern:
        return QStringLiteral("Pattern");
    }

    return QStringLiteral("Unknown");
}

QString valveTypeText(HydraulicLinkValveType type)
{
    switch (type)
    {
    case HydraulicLinkValveType::PRV:
        return QStringLiteral("PRV");
    case HydraulicLinkValveType::PSV:
        return QStringLiteral("PSV");
    case HydraulicLinkValveType::FCV:
        return QStringLiteral("FCV");
    case HydraulicLinkValveType::PBV:
        return QStringLiteral("PBV");
    case HydraulicLinkValveType::TCV:
        return QStringLiteral("TCV");
    case HydraulicLinkValveType::GPV:
        return QStringLiteral("GPV");
    case HydraulicLinkValveType::PCV:
        return QStringLiteral("PCV");
    }

    return QStringLiteral("Unknown");
}

QString valveInitialStatusText(HydraulicLinkValveInitialStatus status)
{
    switch (status)
    {
    case HydraulicLinkValveInitialStatus::Active:
        return QStringLiteral("Active");
    case HydraulicLinkValveInitialStatus::Open:
        return QStringLiteral("Open");
    case HydraulicLinkValveInitialStatus::Closed:
        return QStringLiteral("Closed");
    }

    return QStringLiteral("Unknown");
}

QString pumpStateText(HydraulicSimulationPumpState state)
{
    switch (state)
    {
    case HydraulicSimulationPumpState::CannotSupplyHead:
        return QStringLiteral("Cannot Supply Head");
    case HydraulicSimulationPumpState::Closed:
        return QStringLiteral("Closed");
    case HydraulicSimulationPumpState::Open:
        return QStringLiteral("Open");
    case HydraulicSimulationPumpState::CannotSupplyFlow:
        return QStringLiteral("Cannot Supply Flow");
    }

    return QStringLiteral("Unknown");
}

template<typename EntityType>
QString referencedEntityId(const QList<EntityType> &entities, const QUuid &uuid)
{
    if (uuid.isNull())
        return QString();

    for (const EntityType &entity : entities)
    {
        if (entity.uuid == uuid)
            return entity.id.isEmpty() ? uuid.toString(QUuid::WithoutBraces) : entity.id;
    }

    return uuid.toString(QUuid::WithoutBraces);
}

template<typename ResultType>
const ResultType *simulationResultByUuid(const QList<ResultType> &results, const QUuid &uuid)
{
    for (const ResultType &result : results)
    {
        if (result.uuid == uuid)
            return &result;
    }

    return nullptr;
}

const HydraulicSimulationResultLinkPumpEnergyUsage *pumpEnergyUsageByUuid(
    const QList<HydraulicSimulationResultLinkPumpEnergyUsage> &results, const QUuid &uuid)
{
    for (const HydraulicSimulationResultLinkPumpEnergyUsage &result : results)
    {
        if (result.pump_uuid == uuid)
            return &result;
    }

    return nullptr;
}

QString nodeId(const NetworkHydraulic &network, const QUuid &uuid)
{
    QString id = referencedEntityId(network.nodes_junctions, uuid);
    if (id != uuid.toString(QUuid::WithoutBraces) || uuid.isNull())
        return id;

    id = referencedEntityId(network.nodes_reservoirs, uuid);
    if (id != uuid.toString(QUuid::WithoutBraces))
        return id;

    return referencedEntityId(network.nodes_tanks, uuid);
}

QString patternId(const NetworkHydraulic &network, const QUuid &uuid)
{
    return referencedEntityId(network.patterns_time, uuid);
}

QString tankVolumeCurveId(const NetworkHydraulic &network, const QUuid &uuid)
{
    return referencedEntityId(network.curves_tank_volume, uuid);
}

QString pumpHeadCurveId(const NetworkHydraulic &network, const QUuid &uuid)
{
    return referencedEntityId(network.curves_pump_head, uuid);
}

QString pumpEfficiencyCurveId(const NetworkHydraulic &network, const QUuid &uuid)
{
    return referencedEntityId(network.curves_pump_efficiency, uuid);
}

QString valveSettingCurveId(const NetworkHydraulic &network, HydraulicLinkValveType type,
                            const QUuid &uuid)
{
    if (uuid.isNull())
        return QString();

    if (type == HydraulicLinkValveType::PCV)
        return referencedEntityId(network.curves_valve_characteristic, uuid);

    if (type == HydraulicLinkValveType::GPV)
        return referencedEntityId(network.curves_valve_headloss, uuid);

    QString id = referencedEntityId(network.curves_valve_headloss, uuid);
    if (id != uuid.toString(QUuid::WithoutBraces))
        return id;

    return referencedEntityId(network.curves_valve_characteristic, uuid);
}

QString junctionDemandSummary(const NetworkHydraulic &network,
                              const QList<HydraulicNodeJunctionDemand> &demands)
{
    QStringList parts;
    for (const HydraulicNodeJunctionDemand &demand : demands)
    {
        QString part;
        if (!demand.category_name.isEmpty())
            part += demand.category_name + QStringLiteral(": ");

        part += formatNumber(demand.base_demand_m3_per_h, 3, QStringLiteral(" m³/h"));
        part += QStringLiteral(", ") + patternModeText(demand.pattern_mode);
        if (demand.pattern_mode == HydraulicTimePatternMode::TimePattern)
        {
            const QString id = patternId(network, demand.pattern_uuid);
            if (!id.isEmpty())
                part += QStringLiteral(" ") + id;
        }

        part += QStringLiteral(", ") + demandSourceMethodText(demand.source_method);
        if (!demand.note.isEmpty())
            part += QStringLiteral(", ") + demand.note;

        parts.append(part);
    }

    return parts.join(QStringLiteral(" | "));
}

double junctionBaseDemand(const QList<HydraulicNodeJunctionDemand> &demands)
{
    double total = 0.0;
    for (const HydraulicNodeJunctionDemand &demand : demands)
        total += demand.base_demand_m3_per_h;
    return total;
}

QString twoLineHeaderText(const QString &title)
{
    const qsizetype unit_separator = title.lastIndexOf(QStringLiteral(" ["));
    if (unit_separator > 0)
    {
        return title.left(unit_separator) + QLatin1Char('\n')
            + title.mid(unit_separator + 1);
    }

    const QStringList words = title.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() < 2)
        return title;

    qsizetype best_split = 1;
    qsizetype best_difference = title.size();
    for (qsizetype split = 1; split < words.size(); ++split)
    {
        const qsizetype left_length = words.mid(0, split).join(QLatin1Char(' ')).size();
        const qsizetype right_length = words.mid(split).join(QLatin1Char(' ')).size();
        const qsizetype difference = left_length > right_length
            ? left_length - right_length : right_length - left_length;
        if (difference < best_difference)
        {
            best_difference = difference;
            best_split = split;
        }
    }

    return words.mid(0, best_split).join(QLatin1Char(' ')) + QLatin1Char('\n')
        + words.mid(best_split).join(QLatin1Char(' '));
}

ColumnFilterKind columnFilterKind(const QString &title)
{
    static const QSet<QString> text_columns = {
        QStringLiteral("ID"),
        QStringLiteral("Tag"),
        QStringLiteral("Comment"),
        QStringLiteral("Node 1"),
        QStringLiteral("Node 2"),
        QStringLiteral("Demand Details"),
        QStringLiteral("Material"),
        QStringLiteral("Head Pattern"),
        QStringLiteral("Volume Curve"),
        QStringLiteral("Head Curve"),
        QStringLiteral("Speed Pattern"),
        QStringLiteral("Efficiency Curve"),
        QStringLiteral("Price Pattern"),
        QStringLiteral("Setting Curve")
    };

    static const QSet<QString> choice_columns = {
        QStringLiteral("Enabled"),
        QStringLiteral("Model Role"),
        QStringLiteral("Elevation Input"),
        QStringLiteral("Head Input"),
        QStringLiteral("Head Pattern Mode"),
        QStringLiteral("Geometry Input"),
        QStringLiteral("Can Overflow"),
        QStringLiteral("Initial Status"),
        QStringLiteral("Definition"),
        QStringLiteral("Control Type"),
        QStringLiteral("Efficiency Input"),
        QStringLiteral("Energy Price Input"),
        QStringLiteral("Valve Type"),
        QStringLiteral("Status"),
        QStringLiteral("Operating State"),
        QStringLiteral("Regulating"),
        QStringLiteral("Referenced by Control")
    };

    if (text_columns.contains(title))
        return ColumnFilterKind::Text;
    if (choice_columns.contains(title))
        return ColumnFilterKind::Choice;
    return ColumnFilterKind::None;
}

void configureColumnFilters(QList<TableColumn> &columns)
{
    for (TableColumn &column : columns)
        column.filter_kind = columnFilterKind(column.title);
}

void appendCommonColumns(QList<TableColumn> &columns)
{
    columns.append({QStringLiteral("ID"), false});
    columns.append({QStringLiteral("Enabled"), false});
    columns.append({QStringLiteral("Model Role"), false});
    columns.append({QStringLiteral("Date Added"), false});
    columns.append({QStringLiteral("Installation Date"), false});
    columns.append({QStringLiteral("Tag"), false});
    columns.append({QStringLiteral("Comment"), false});
}

void appendCommonCells(QList<TableCell> &cells, const QString &id,
                       const HydraulicEntityMetadata &metadata)
{
    cells.append(textCell(id));
    cells.append(boolCell(metadata.enabled));
    cells.append(textCell(entityModelRoleText(metadata.model_role)));
    cells.append(dateCell(metadata.date_added));
    cells.append(dateCell(metadata.date_installed));
    cells.append(textCell(metadata.tag));
    cells.append(textCell(metadata.comment));
}

void appendNodePositionColumns(QList<TableColumn> &columns)
{
    columns.append({QStringLiteral("Latitude [°]"), false});
    columns.append({QStringLiteral("Longitude [°]"), false});
}

void appendNodePositionCells(QList<TableCell> &cells, const CoordinateWGS84 &coordinate)
{
    cells.append(numberCell(coordinate.latitude_deg, 6));
    cells.append(numberCell(coordinate.longitude_deg, 6));
}

bool isErrorSeverity(HydraulicSimulationDiagnosticSeverity severity)
{
    return severity == HydraulicSimulationDiagnosticSeverity::Error
        || severity == HydraulicSimulationDiagnosticSeverity::Fatal;
}

QHash<QUuid, QString> simulationErrorTooltips(const HydraulicData *hydraulic_data)
{
    QHash<QUuid, QStringList> messages;
    const std::optional<HydraulicSimulationResultTimeline> &timeline =
        hydraulic_data->simulationResultTimeline();
    if (!timeline.has_value())
        return QHash<QUuid, QString>();

    for (const HydraulicSimulationDiagnostic &diagnostic : timeline->diagnostics)
    {
        if (!isErrorSeverity(diagnostic.severity) || diagnostic.entity.uuid.isNull())
            continue;

        QString message = diagnostic.message;
        if (message.isEmpty())
            message = diagnostic.message_backend;
        if (message.isEmpty())
            message = QStringLiteral("Simulation failed for this entity.");

        messages[diagnostic.entity.uuid].append(message);
    }

    const HydraulicSimulationStatus *status = hydraulic_data->simulationStatus();
    if (status != nullptr && !status->success && !status->entity.uuid.isNull())
    {
        QString message = status->message;
        if (message.isEmpty())
            message = status->message_backend;
        if (message.isEmpty())
            message = QStringLiteral("Simulation failed for this entity.");

        if (!messages[status->entity.uuid].contains(message))
            messages[status->entity.uuid].append(message);
    }

    QHash<QUuid, QString> tooltips;
    QHash<QUuid, QStringList>::const_iterator iterator = messages.constBegin();
    while (iterator != messages.constEnd())
    {
        tooltips.insert(iterator.key(), QStringLiteral("Simulation error:\n")
                                      + iterator.value().join(QStringLiteral("\n")));
        ++iterator;
    }
    return tooltips;
}
}

class HydraulicEntityTableModel : public QAbstractTableModel
{
public:
    explicit HydraulicEntityTableModel(HydraulicData *hydraulic_data,
                                       InfrastructureEntity entity_type,
                                       QObject *parent = nullptr)
        : QAbstractTableModel(parent),
          hydraulic_data(hydraulic_data),
          entity_type(entity_type)
    {
        rebuild();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return this->rows.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return this->columns.size();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= this->rows.size()
            || index.column() < 0 || index.column() >= this->columns.size())
        {
            return QVariant();
        }

        const TableRow &row = this->rows.at(index.row());
        const TableCell &cell = row.cells.at(index.column());

        switch (role)
        {
        case Qt::DisplayRole:
            return cell.display;
        case SortRole:
            return cell.sort_value;
        case EntityUuidRole:
            return row.uuid;
        case Qt::TextAlignmentRole:
            return static_cast<int>(cell.alignment);
        case Qt::ToolTipRole:
            if (row.simulation_error && !row.simulation_error_tooltip.isEmpty())
                return row.simulation_error_tooltip;
            return cell.tooltip;
        case Qt::BackgroundRole:
            if (row.simulation_error)
                return QBrush(QColor(210, 45, 45, 70));
            if (this->columns.at(index.column()).simulation_result)
                return QBrush(QColor(70, 135, 210, 25));
            return QVariant();
        case Qt::FontRole:
            if (index.column() == 0)
            {
                QFont font;
                font.setBold(true);
                return font;
            }
            return QVariant();
        default:
            return QVariant();
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override
    {
        if (orientation == Qt::Vertical)
            return QVariant();
        if (section < 0 || section >= this->columns.size())
            return QVariant();

        const TableColumn &column = this->columns.at(section);
        if (role == Qt::DisplayRole)
            return twoLineHeaderText(column.title);
        if (role == Qt::ToolTipRole)
        {
            if (column.simulation_result)
                return column.title + QStringLiteral("\nSimulation result.");
            return column.title;
        }
        if (role == Qt::BackgroundRole && column.simulation_result)
            return QBrush(QColor(70, 135, 210, 55));
        if (role == Qt::FontRole && column.simulation_result)
        {
            QFont font;
            font.setBold(true);
            return font;
        }

        return QVariant();
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::NoItemFlags;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }

    ColumnFilterKind filterKind(int column) const
    {
        if (column < 0 || column >= this->columns.size())
            return ColumnFilterKind::None;
        return this->columns.at(column).filter_kind;
    }

    QString columnTitle(int column) const
    {
        if (column < 0 || column >= this->columns.size())
            return QString();
        return this->columns.at(column).title;
    }

    void rebuild()
    {
        beginResetModel();
        this->columns.clear();
        this->rows.clear();

        const NetworkHydraulic &network = this->hydraulic_data->networkHydraulic();
        const HydraulicSimulationResult *simulation_result =
            this->hydraulic_data->currentSimulationResult();
        QHash<QUuid, InfrastructureEntity> error_entities =
            this->hydraulic_data->simulationErrorEntities();
        const QUuid status_error_uuid = this->hydraulic_data->simulationErrorEntityUuid();
        const InfrastructureEntity status_error_type =
            this->hydraulic_data->simulationErrorEntityType();
        if (!status_error_uuid.isNull() && status_error_type != InfrastructureEntity::Unknown)
            error_entities.insert(status_error_uuid, status_error_type);

        const QHash<QUuid, QString> error_tooltips =
            simulationErrorTooltips(this->hydraulic_data);

        switch (this->entity_type)
        {
        case InfrastructureEntity::Junction:
            buildJunctions(network, simulation_result, error_entities, error_tooltips);
            break;
        case InfrastructureEntity::Reservoir:
            buildReservoirs(network, simulation_result, error_entities, error_tooltips);
            break;
        case InfrastructureEntity::Tank:
            buildTanks(network, simulation_result, error_entities, error_tooltips);
            break;
        case InfrastructureEntity::Pipe:
            buildPipes(network, simulation_result, error_entities, error_tooltips);
            break;
        case InfrastructureEntity::Pump:
            buildPumps(network, simulation_result, error_entities, error_tooltips);
            break;
        case InfrastructureEntity::Valve:
            buildValves(network, simulation_result, error_entities, error_tooltips);
            break;
        default:
            break;
        }

        configureColumnFilters(this->columns);
        endResetModel();
    }

private:
    void finishRow(TableRow &row,
                   const QHash<QUuid, InfrastructureEntity> &error_entities,
                   const QHash<QUuid, QString> &error_tooltips)
    {
        row.simulation_error = error_entities.value(row.uuid, InfrastructureEntity::Unknown)
            == this->entity_type;
        row.simulation_error_tooltip = error_tooltips.value(row.uuid);
        this->rows.append(row);
    }

    void buildJunctions(const NetworkHydraulic &network,
                        const HydraulicSimulationResult *simulation_result,
                        const QHash<QUuid, InfrastructureEntity> &error_entities,
                        const QHash<QUuid, QString> &error_tooltips)
    {
        appendCommonColumns(this->columns);
        appendNodePositionColumns(this->columns);
        this->columns.append({QStringLiteral("Elevation Input"), false});
        this->columns.append({QStringLiteral("Direct Elevation [m]"), false});
        this->columns.append({QStringLiteral("Terrain Elevation [m]"), false});
        this->columns.append({QStringLiteral("Elevation Offset [m]"), false});
        this->columns.append({QStringLiteral("Resolved Elevation [m]"), false});
        this->columns.append({QStringLiteral("Demand Count"), false});
        this->columns.append({QStringLiteral("Base Demand [m³/h]"), false});
        this->columns.append({QStringLiteral("Demand Details"), false});
        this->columns.append({QStringLiteral("Emitter Coefficient"), false});
        this->columns.append({QStringLiteral("Requested Demand [m³/h]"), true});
        this->columns.append({QStringLiteral("Delivered Demand [m³/h]"), true});
        this->columns.append({QStringLiteral("Demand Deficit [m³/h]"), true});
        this->columns.append({QStringLiteral("Total Demand [m³/h]"), true});
        this->columns.append({QStringLiteral("Emitter Flow [m³/h]"), true});
        this->columns.append({QStringLiteral("Leakage Flow [m³/h]"), true});
        this->columns.append({QStringLiteral("Head [m]"), true});
        this->columns.append({QStringLiteral("Pressure Head [m]"), true});
        this->columns.append({QStringLiteral("Referenced by Control"), true});

        for (const HydraulicNodeJunction &junction : network.nodes_junctions)
        {
            TableRow row;
            row.uuid = junction.uuid;
            appendCommonCells(row.cells, junction.id, junction.metadata);
            appendNodePositionCells(row.cells, junction.coordinate_wgs84);
            row.cells.append(textCell(elevationInputText(junction.elevation_input_type)));
            row.cells.append(numberCell(junction.elevation_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(junction.terrain_elevation_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(junction.elevation_offset_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(resolvedSymbologyElevationM(junction), 3, QStringLiteral(" m")));
            row.cells.append(integerCell(junction.demands.size()));
            row.cells.append(numberCell(junctionBaseDemand(junction.demands), 3, QStringLiteral(" m³/h")));
            row.cells.append(textCell(junctionDemandSummary(network, junction.demands)));
            row.cells.append(numberCell(junction.emitter_coefficient_m3_per_h_per_m_exponent, 6));

            const HydraulicSimulationResultNodeJunction *result = simulation_result == nullptr
                ? nullptr : simulationResultByUuid(simulation_result->nodes_junctions, junction.uuid);
            appendJunctionSimulationCells(row.cells, result);
            finishRow(row, error_entities, error_tooltips);
        }
    }

    void appendJunctionSimulationCells(QList<TableCell> &cells,
                                       const HydraulicSimulationResultNodeJunction *result) const
    {
        if (result == nullptr)
        {
            for (int index = 0; index < 9; ++index)
                cells.append(emptyCell());
            return;
        }

        cells.append(numberCell(result->demand_requested_m3_per_h, 3, QStringLiteral(" m³/h")));
        cells.append(numberCell(result->demand_delivered_m3_per_h, 3, QStringLiteral(" m³/h")));
        cells.append(numberCell(result->demand_deficit_m3_per_h, 3, QStringLiteral(" m³/h")));
        cells.append(numberCell(result->total_demand_m3_per_h, 3, QStringLiteral(" m³/h")));
        cells.append(numberCell(result->emitter_flow_m3_per_h, 3, QStringLiteral(" m³/h")));
        cells.append(numberCell(result->leakage_flow_m3_per_h, 3, QStringLiteral(" m³/h")));
        cells.append(numberCell(result->head_m, 3, QStringLiteral(" m")));
        cells.append(numberCell(result->pressure_head_m, 3, QStringLiteral(" m")));
        cells.append(boolCell(result->appears_in_control));
    }

    void buildReservoirs(const NetworkHydraulic &network,
                         const HydraulicSimulationResult *simulation_result,
                         const QHash<QUuid, InfrastructureEntity> &error_entities,
                         const QHash<QUuid, QString> &error_tooltips)
    {
        appendCommonColumns(this->columns);
        appendNodePositionColumns(this->columns);
        this->columns.append({QStringLiteral("Head Input"), false});
        this->columns.append({QStringLiteral("Direct Head [m]"), false});
        this->columns.append({QStringLiteral("Terrain Elevation [m]"), false});
        this->columns.append({QStringLiteral("Head Offset [m]"), false});
        this->columns.append({QStringLiteral("Resolved Head [m]"), false});
        this->columns.append({QStringLiteral("Head Pattern Mode"), false});
        this->columns.append({QStringLiteral("Head Pattern"), false});
        this->columns.append({QStringLiteral("Net Demand [m³/h]"), true});
        this->columns.append({QStringLiteral("Head [m]"), true});
        this->columns.append({QStringLiteral("Pressure Head [m]"), true});
        this->columns.append({QStringLiteral("Referenced by Control"), true});

        for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
        {
            TableRow row;
            row.uuid = reservoir.uuid;
            appendCommonCells(row.cells, reservoir.id, reservoir.metadata);
            appendNodePositionCells(row.cells, reservoir.coordinate_wgs84);
            row.cells.append(textCell(elevationInputText(reservoir.head_input_type)));
            row.cells.append(numberCell(reservoir.head_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(reservoir.terrain_elevation_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(reservoir.head_offset_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(resolvedSymbologyElevationM(reservoir), 3, QStringLiteral(" m")));
            row.cells.append(textCell(patternModeText(reservoir.head_pattern_mode)));
            row.cells.append(textCell(patternId(network, reservoir.head_pattern_uuid)));

            const HydraulicSimulationResultNodeReservoir *result = simulation_result == nullptr
                ? nullptr : simulationResultByUuid(simulation_result->nodes_reservoirs, reservoir.uuid);
            if (result == nullptr)
            {
                for (int index = 0; index < 4; ++index)
                    row.cells.append(emptyCell());
            }
            else
            {
                row.cells.append(numberCell(result->net_demand_m3_per_h, 3, QStringLiteral(" m³/h")));
                row.cells.append(numberCell(result->head_m, 3, QStringLiteral(" m")));
                row.cells.append(numberCell(result->pressure_head_m, 3, QStringLiteral(" m")));
                row.cells.append(boolCell(result->appears_in_control));
            }

            finishRow(row, error_entities, error_tooltips);
        }
    }

    void buildTanks(const NetworkHydraulic &network,
                    const HydraulicSimulationResult *simulation_result,
                    const QHash<QUuid, InfrastructureEntity> &error_entities,
                    const QHash<QUuid, QString> &error_tooltips)
    {
        appendCommonColumns(this->columns);
        appendNodePositionColumns(this->columns);
        this->columns.append({QStringLiteral("Elevation Input"), false});
        this->columns.append({QStringLiteral("Bottom Elevation [m]"), false});
        this->columns.append({QStringLiteral("Terrain Elevation [m]"), false});
        this->columns.append({QStringLiteral("Bottom Offset [m]"), false});
        this->columns.append({QStringLiteral("Resolved Bottom Elevation [m]"), false});
        this->columns.append({QStringLiteral("Initial Level [m]"), false});
        this->columns.append({QStringLiteral("Minimum Level [m]"), false});
        this->columns.append({QStringLiteral("Maximum Level [m]"), false});
        this->columns.append({QStringLiteral("Geometry Input"), false});
        this->columns.append({QStringLiteral("Diameter [m]"), false});
        this->columns.append({QStringLiteral("Cross-Section Area [m²]"), false});
        this->columns.append({QStringLiteral("Maximum Volume [m³]"), false});
        this->columns.append({QStringLiteral("Minimum Volume [m³]"), false});
        this->columns.append({QStringLiteral("Volume Curve"), false});
        this->columns.append({QStringLiteral("Can Overflow"), false});
        this->columns.append({QStringLiteral("Net Demand [m³/h]"), true});
        this->columns.append({QStringLiteral("Head [m]"), true});
        this->columns.append({QStringLiteral("Pressure Head [m]"), true});
        this->columns.append({QStringLiteral("Water Level [m]"), true});
        this->columns.append({QStringLiteral("Volume [m³]"), true});
        this->columns.append({QStringLiteral("Mixing-Zone Volume [m³]"), true});
        this->columns.append({QStringLiteral("Referenced by Control"), true});

        for (const HydraulicNodeTank &tank : network.nodes_tanks)
        {
            TableRow row;
            row.uuid = tank.uuid;
            appendCommonCells(row.cells, tank.id, tank.metadata);
            appendNodePositionCells(row.cells, tank.coordinate_wgs84);
            row.cells.append(textCell(elevationInputText(tank.elevation_input_type)));
            row.cells.append(numberCell(tank.bottom_elevation_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(tank.terrain_elevation_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(tank.bottom_offset_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(resolvedSymbologyElevationM(tank), 3, QStringLiteral(" m")));
            row.cells.append(numberCell(tank.water_level_initial_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(tank.water_level_minimum_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(tank.water_level_maximum_m, 3, QStringLiteral(" m")));
            row.cells.append(textCell(tankGeometryInputText(tank.geometry_input_type)));
            row.cells.append(numberCell(tank.diameter_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(tank.cross_section_area_m2, 3, QStringLiteral(" m²")));
            row.cells.append(numberCell(tank.volume_at_maximum_level_m3, 3, QStringLiteral(" m³")));
            row.cells.append(numberCell(tank.minimum_volume_m3, 3, QStringLiteral(" m³")));
            row.cells.append(textCell(tankVolumeCurveId(network, tank.volume_curve_uuid)));
            row.cells.append(boolCell(tank.can_overflow));

            const HydraulicSimulationResultNodeTank *result = simulation_result == nullptr
                ? nullptr : simulationResultByUuid(simulation_result->nodes_tanks, tank.uuid);
            if (result == nullptr)
            {
                for (int index = 0; index < 7; ++index)
                    row.cells.append(emptyCell());
            }
            else
            {
                row.cells.append(numberCell(result->net_demand_m3_per_h, 3, QStringLiteral(" m³/h")));
                row.cells.append(numberCell(result->head_m, 3, QStringLiteral(" m")));
                row.cells.append(numberCell(result->pressure_head_m, 3, QStringLiteral(" m")));
                row.cells.append(numberCell(result->water_level_m, 3, QStringLiteral(" m")));
                row.cells.append(numberCell(result->volume_m3, 3, QStringLiteral(" m³")));
                row.cells.append(numberCell(result->mixing_zone_volume_m3, 3, QStringLiteral(" m³")));
                row.cells.append(boolCell(result->appears_in_control));
            }

            finishRow(row, error_entities, error_tooltips);
        }
    }

    void buildPipes(const NetworkHydraulic &network,
                    const HydraulicSimulationResult *simulation_result,
                    const QHash<QUuid, InfrastructureEntity> &error_entities,
                    const QHash<QUuid, QString> &error_tooltips)
    {
        appendCommonColumns(this->columns);
        this->columns.append({QStringLiteral("Node 1"), false});
        this->columns.append({QStringLiteral("Node 2"), false});
        this->columns.append({QStringLiteral("Vertices"), false});
        this->columns.append({QStringLiteral("Calculated Length [m]"), false});
        this->columns.append({QStringLiteral("Measured Length [m]"), false});
        this->columns.append({QStringLiteral("Effective Length [m]"), false});
        this->columns.append({QStringLiteral("Initial Status"), false});
        this->columns.append({QStringLiteral("Diameter [mm]"), false});
        this->columns.append({QStringLiteral("Material"), false});
        this->columns.append({QStringLiteral("Roughness HW"), false});
        this->columns.append({QStringLiteral("Roughness DW [mm]"), false});
        this->columns.append({QStringLiteral("Roughness CM"), false});
        this->columns.append({QStringLiteral("Minor Loss"), false});
        this->columns.append({QStringLiteral("Leak Area [mm²/100m]"), false});
        this->columns.append({QStringLiteral("Leak Expansion [mm²/m head]"), false});
        this->columns.append({QStringLiteral("Flow [m³/h]"), true});
        this->columns.append({QStringLiteral("Leakage Flow [m³/h]"), true});
        this->columns.append({QStringLiteral("Velocity [m/s]"), true});
        this->columns.append({QStringLiteral("Head Loss [m]"), true});
        this->columns.append({QStringLiteral("Unit Head Loss [m/km]"), true});
        this->columns.append({QStringLiteral("Friction Factor"), true});
        this->columns.append({QStringLiteral("Status"), true});
        this->columns.append({QStringLiteral("Effective Roughness HW"), true});
        this->columns.append({QStringLiteral("Effective Roughness DW [mm]"), true});
        this->columns.append({QStringLiteral("Effective Roughness CM"), true});
        this->columns.append({QStringLiteral("Referenced by Control"), true});

        for (const HydraulicLinkPipe &pipe : network.links_pipes)
        {
            TableRow row;
            row.uuid = pipe.uuid;
            appendCommonCells(row.cells, pipe.id, pipe.metadata);
            row.cells.append(textCell(nodeId(network, pipe.node_uuid_from)));
            row.cells.append(textCell(nodeId(network, pipe.node_uuid_to)));
            row.cells.append(integerCell(pipe.vertices.size()));
            row.cells.append(numberCell(pipe.length_calculated_m, 3, QStringLiteral(" m")));
            row.cells.append(optionalNumberCell(pipe.length_measured_m, 3, QStringLiteral(" m")));
            row.cells.append(numberCell(pipe.length_measured_m.value_or(pipe.length_calculated_m), 3,
                                        QStringLiteral(" m")));
            row.cells.append(textCell(pipeInitialStatusText(pipe.initial_status)));
            row.cells.append(numberCell(pipe.diameter_mm, 3, QStringLiteral(" mm")));
            row.cells.append(textCell(pipe.material_id));
            row.cells.append(numberCell(pipe.roughness_hw, 3));
            row.cells.append(numberCell(pipe.roughness_dw_mm, 6, QStringLiteral(" mm")));
            row.cells.append(numberCell(pipe.roughness_cm, 6));
            row.cells.append(numberCell(pipe.minor_loss, 6));
            row.cells.append(numberCell(pipe.leak_area_mm2_per_100m, 6, QStringLiteral(" mm²/100m")));
            row.cells.append(numberCell(pipe.leak_expansion_mm2_per_m_head, 6,
                                        QStringLiteral(" mm²/m head")));

            const HydraulicSimulationResultLinkPipe *result = simulation_result == nullptr
                ? nullptr : simulationResultByUuid(simulation_result->links_pipes, pipe.uuid);
            if (result == nullptr)
            {
                for (int index = 0; index < 11; ++index)
                    row.cells.append(emptyCell());
            }
            else
            {
                row.cells.append(numberCell(result->flow_m3_per_h, 3, QStringLiteral(" m³/h")));
                row.cells.append(numberCell(result->leakage_flow_m3_per_h, 3, QStringLiteral(" m³/h")));
                row.cells.append(numberCell(result->velocity_m_per_s, 3, QStringLiteral(" m/s")));
                row.cells.append(numberCell(result->head_loss_m, 3, QStringLiteral(" m")));
                row.cells.append(numberCell(result->unit_head_loss_m_per_km, 3, QStringLiteral(" m/km")));
                row.cells.append(numberCell(result->friction_factor, 6));
                row.cells.append(boolCell(result->open, QStringLiteral("Open"), QStringLiteral("Closed")));
                row.cells.append(optionalNumberCell(result->roughness_hw, 3));
                row.cells.append(optionalNumberCell(result->roughness_dw_mm, 6, QStringLiteral(" mm")));
                row.cells.append(optionalNumberCell(result->roughness_cm, 6));
                row.cells.append(boolCell(result->appears_in_control));
            }

            finishRow(row, error_entities, error_tooltips);
        }
    }

    void buildPumps(const NetworkHydraulic &network,
                    const HydraulicSimulationResult *simulation_result,
                    const QHash<QUuid, InfrastructureEntity> &error_entities,
                    const QHash<QUuid, QString> &error_tooltips)
    {
        appendCommonColumns(this->columns);
        this->columns.append({QStringLiteral("Node 1"), false});
        this->columns.append({QStringLiteral("Node 2"), false});
        this->columns.append({QStringLiteral("Vertices"), false});
        this->columns.append({QStringLiteral("Definition"), false});
        this->columns.append({QStringLiteral("Constant Power [kW]"), false});
        this->columns.append({QStringLiteral("Head Curve"), false});
        this->columns.append({QStringLiteral("Initial Speed"), false});
        this->columns.append({QStringLiteral("Initial Status"), false});
        this->columns.append({QStringLiteral("Speed Pattern"), false});
        this->columns.append({QStringLiteral("Control Type"), false});
        this->columns.append({QStringLiteral("Efficiency Input"), false});
        this->columns.append({QStringLiteral("Constant Efficiency [%]"), false});
        this->columns.append({QStringLiteral("Efficiency Curve"), false});
        this->columns.append({QStringLiteral("Energy Price Input"), false});
        this->columns.append({QStringLiteral("Energy Price [/kWh]"), false});
        this->columns.append({QStringLiteral("Price Pattern"), false});
        this->columns.append({QStringLiteral("Flow [m³/h]"), true});
        this->columns.append({QStringLiteral("Velocity [m/s]"), true});
        this->columns.append({QStringLiteral("Head Gain [m]"), true});
        this->columns.append({QStringLiteral("Status"), true});
        this->columns.append({QStringLiteral("Operating State"), true});
        this->columns.append({QStringLiteral("Speed"), true});
        this->columns.append({QStringLiteral("Efficiency [%]"), true});
        this->columns.append({QStringLiteral("Power [kW]"), true});
        this->columns.append({QStringLiteral("Referenced by Control"), true});
        this->columns.append({QStringLiteral("Time Online [%]"), true});
        this->columns.append({QStringLiteral("Average Efficiency [%]"), true});
        this->columns.append({QStringLiteral("Average Specific Power"), true});
        this->columns.append({QStringLiteral("Average Power [kW]"), true});
        this->columns.append({QStringLiteral("Peak Power [kW]"), true});
        this->columns.append({QStringLiteral("Average Cost / Day"), true});

        const HydraulicSimulationResult *final_result = nullptr;
        const std::optional<HydraulicSimulationResultTimeline> &timeline =
            this->hydraulic_data->simulationResultTimeline();
        if (this->hydraulic_data->hasSimulationResults() && timeline.has_value()
            && !timeline->results.isEmpty())
        {
            final_result = &timeline->results.constLast();
        }

        for (const HydraulicLinkPump &pump : network.links_pumps)
        {
            TableRow row;
            row.uuid = pump.uuid;
            appendCommonCells(row.cells, pump.id, pump.metadata);
            row.cells.append(textCell(nodeId(network, pump.node_uuid_from)));
            row.cells.append(textCell(nodeId(network, pump.node_uuid_to)));
            row.cells.append(integerCell(pump.vertices.size()));
            row.cells.append(textCell(pumpDefinitionText(pump.definition_type)));
            row.cells.append(numberCell(pump.constant_power_kw, 3, QStringLiteral(" kW")));
            row.cells.append(textCell(pumpHeadCurveId(network, pump.head_curve_uuid)));
            row.cells.append(numberCell(pump.initial_speed, 4));
            row.cells.append(textCell(pumpInitialStatusText(pump.initial_status)));
            row.cells.append(textCell(patternId(network, pump.speed_pattern_uuid)));
            row.cells.append(textCell(pumpControlTypeText(pump.control_type)));
            row.cells.append(textCell(pumpEfficiencyInputText(pump.efficiency_input_type)));
            row.cells.append(numberCell(pump.constant_efficiency_percent, 3, QStringLiteral(" %")));
            row.cells.append(textCell(pumpEfficiencyCurveId(network, pump.efficiency_curve_uuid)));
            row.cells.append(textCell(pumpEnergyPriceInputText(pump.energy_price_input_type)));
            row.cells.append(numberCell(pump.energy_price_per_kw_h, 6));
            row.cells.append(textCell(patternId(network, pump.price_pattern_uuid)));

            const HydraulicSimulationResultLinkPump *result = simulation_result == nullptr
                ? nullptr : simulationResultByUuid(simulation_result->links_pumps, pump.uuid);
            if (result == nullptr)
            {
                for (int index = 0; index < 9; ++index)
                    row.cells.append(emptyCell());
            }
            else
            {
                row.cells.append(numberCell(result->flow_m3_per_h, 3, QStringLiteral(" m³/h")));
                row.cells.append(numberCell(result->velocity_m_per_s, 3, QStringLiteral(" m/s")));
                row.cells.append(numberCell(result->head_gain_m, 3, QStringLiteral(" m")));
                row.cells.append(boolCell(result->open, QStringLiteral("Open"), QStringLiteral("Closed")));
                row.cells.append(textCell(pumpStateText(result->state)));
                row.cells.append(numberCell(result->speed, 4));
                row.cells.append(numberCell(result->efficiency_percent, 3, QStringLiteral(" %")));
                row.cells.append(numberCell(result->power_kw, 3, QStringLiteral(" kW")));
                row.cells.append(boolCell(result->appears_in_control));
            }

            const HydraulicSimulationResultLinkPumpEnergyUsage *energy_usage = final_result == nullptr
                ? nullptr : pumpEnergyUsageByUuid(final_result->links_pump_energy_usage, pump.uuid);
            if (energy_usage == nullptr)
            {
                for (int index = 0; index < 6; ++index)
                    row.cells.append(emptyCell());
            }
            else
            {
                row.cells.append(numberCell(energy_usage->time_online_percent, 2, QStringLiteral(" %")));
                row.cells.append(numberCell(energy_usage->average_efficiency_percent, 2, QStringLiteral(" %")));
                row.cells.append(numberCell(energy_usage->average_kw_per_flow_unit, 6));
                row.cells.append(numberCell(energy_usage->average_power_kw, 3, QStringLiteral(" kW")));
                row.cells.append(numberCell(energy_usage->peak_power_kw, 3, QStringLiteral(" kW")));
                row.cells.append(numberCell(energy_usage->average_cost_per_day, 4));
            }

            finishRow(row, error_entities, error_tooltips);
        }
    }

    void buildValves(const NetworkHydraulic &network,
                     const HydraulicSimulationResult *simulation_result,
                     const QHash<QUuid, InfrastructureEntity> &error_entities,
                     const QHash<QUuid, QString> &error_tooltips)
    {
        appendCommonColumns(this->columns);
        this->columns.append({QStringLiteral("Node 1"), false});
        this->columns.append({QStringLiteral("Node 2"), false});
        this->columns.append({QStringLiteral("Vertices"), false});
        this->columns.append({QStringLiteral("Valve Type"), false});
        this->columns.append({QStringLiteral("Setting"), false});
        this->columns.append({QStringLiteral("Setting Curve"), false});
        this->columns.append({QStringLiteral("Initial Status"), false});
        this->columns.append({QStringLiteral("Diameter [mm]"), false});
        this->columns.append({QStringLiteral("Minor Loss"), false});
        this->columns.append({QStringLiteral("Flow [m³/h]"), true});
        this->columns.append({QStringLiteral("Velocity [m/s]"), true});
        this->columns.append({QStringLiteral("Head Loss [m]"), true});
        this->columns.append({QStringLiteral("Status"), true});
        this->columns.append({QStringLiteral("Regulating"), true});
        this->columns.append({QStringLiteral("Setting"), true});
        this->columns.append({QStringLiteral("Referenced by Control"), true});

        for (const HydraulicLinkValve &valve : network.links_valves)
        {
            TableRow row;
            row.uuid = valve.uuid;
            appendCommonCells(row.cells, valve.id, valve.metadata);
            row.cells.append(textCell(nodeId(network, valve.node_uuid_from)));
            row.cells.append(textCell(nodeId(network, valve.node_uuid_to)));
            row.cells.append(integerCell(valve.vertices.size()));
            row.cells.append(textCell(valveTypeText(valve.type)));
            row.cells.append(numberCell(valve.setting, 6));
            row.cells.append(textCell(valveSettingCurveId(network, valve.type, valve.setting_curve_uuid)));
            row.cells.append(textCell(valveInitialStatusText(valve.initial_status)));
            row.cells.append(numberCell(valve.diameter_mm, 3, QStringLiteral(" mm")));
            row.cells.append(numberCell(valve.minor_loss, 6));

            const HydraulicSimulationResultLinkValve *result = simulation_result == nullptr
                ? nullptr : simulationResultByUuid(simulation_result->links_valves, valve.uuid);
            if (result == nullptr)
            {
                for (int index = 0; index < 7; ++index)
                    row.cells.append(emptyCell());
            }
            else
            {
                row.cells.append(numberCell(result->flow_m3_per_h, 3, QStringLiteral(" m³/h")));
                row.cells.append(numberCell(result->velocity_m_per_s, 3, QStringLiteral(" m/s")));
                row.cells.append(numberCell(result->head_loss_m, 3, QStringLiteral(" m")));
                row.cells.append(boolCell(result->open, QStringLiteral("Open"), QStringLiteral("Closed")));
                row.cells.append(boolCell(result->active));
                row.cells.append(numberCell(result->setting, 6));
                row.cells.append(boolCell(result->appears_in_control));
            }

            finishRow(row, error_entities, error_tooltips);
        }
    }

    HydraulicData *hydraulic_data;
    InfrastructureEntity entity_type;
    QList<TableColumn> columns;
    QList<TableRow> rows;
};

class HydraulicEntityFilterProxyModel : public QSortFilterProxyModel
{
public:
    explicit HydraulicEntityFilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
    }

    QString columnFilterValue(int column) const
    {
        if (!this->filters.contains(column))
            return QString();
        return this->filters.value(column).second;
    }

    void setColumnFilter(int column, ColumnFilterKind kind, const QString &value)
    {
        const QString trimmed_value = value.trimmed();
        if (trimmed_value.isEmpty())
        {
            if (this->filters.remove(column) == 0)
                return;
            notifyFilterChanged();
            return;
        }

        const QPair<ColumnFilterKind, QString> new_filter = qMakePair(kind, trimmed_value);
        if (this->filters.value(column) == new_filter)
            return;

        this->filters.insert(column, new_filter);
        notifyFilterChanged();
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override
    {
        QHash<int, QPair<ColumnFilterKind, QString>>::const_iterator iterator =
            this->filters.constBegin();
        while (iterator != this->filters.constEnd())
        {
            const QModelIndex index = sourceModel()->index(source_row, iterator.key(),
                                                           source_parent);
            const QString display = sourceModel()->data(index, Qt::DisplayRole).toString();
            const ColumnFilterKind kind = iterator.value().first;
            const QString value = iterator.value().second;

            if (kind == ColumnFilterKind::Text)
            {
                if (!display.contains(value, Qt::CaseInsensitive))
                    return false;
            }
            else if (kind == ColumnFilterKind::Choice)
            {
                if (display.compare(value, Qt::CaseInsensitive) != 0)
                    return false;
            }

            ++iterator;
        }

        return true;
    }

private:
    void notifyFilterChanged()
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
    }

    QHash<int, QPair<ColumnFilterKind, QString>> filters;
};

class HydraulicEntityFilterBar : public QWidget
{
public:
    HydraulicEntityFilterBar(QTableView *table, HydraulicEntityTableModel *model,
                             HydraulicEntityFilterProxyModel *proxy_model,
                             QWidget *parent = nullptr)
        : QWidget(parent),
          table(table),
          model(model),
          proxy_model(proxy_model)
    {
        setFixedHeight(30);
        rebuildControls();

        QHeaderView *header = this->table->horizontalHeader();
        connect(header, &QHeaderView::sectionResized, this,
                [this](int, int, int) { updateControlGeometries(); });
        connect(header, &QHeaderView::sectionMoved, this,
                [this](int, int, int) { updateControlGeometries(); });
        connect(header, &QHeaderView::geometriesChanged, this,
                [this]() { updateControlGeometries(); });
        connect(this->table->horizontalScrollBar(), &QScrollBar::valueChanged, this,
                [this](int) { updateControlGeometries(); });
        connect(this->model, &QAbstractItemModel::modelReset, this,
                [this]()
        {
            refreshChoiceControls();
            updateControlGeometries();
        });
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateControlGeometries();
    }

private:
    void rebuildControls()
    {
        const int column_count = this->model->columnCount();
        this->controls.clear();
        this->controls.reserve(column_count);
        for (int column = 0; column < column_count; ++column)
        {
            const ColumnFilterKind kind = this->model->filterKind(column);
            const QString active_filter = this->proxy_model->columnFilterValue(column);
            QWidget *control = nullptr;

            if (kind == ColumnFilterKind::Text)
            {
                QLineEdit *line_edit = new QLineEdit(this);
                line_edit->setPlaceholderText(QStringLiteral("Filter…"));
                line_edit->setClearButtonEnabled(true);
                line_edit->setToolTip(QStringLiteral("Filter ")
                                      + this->model->columnTitle(column));
                connect(line_edit, &QLineEdit::textChanged, this,
                        [this, column](const QString &text)
                {
                    this->proxy_model->setColumnFilter(column, ColumnFilterKind::Text, text);
                });
                line_edit->setText(active_filter);
                control = line_edit;
            }
            else if (kind == ColumnFilterKind::Choice)
            {
                QComboBox *combo_box = new QComboBox(this);
                combo_box->setToolTip(QStringLiteral("Filter ")
                                      + this->model->columnTitle(column));
                populateChoiceControl(combo_box, column, active_filter);

                connect(combo_box, &QComboBox::currentIndexChanged, this,
                        [this, combo_box, column](int)
                {
                    this->proxy_model->setColumnFilter(
                        column, ColumnFilterKind::Choice,
                        combo_box->currentData().toString());
                });
                control = combo_box;
            }

            this->controls.append(control);
        }

        updateControlGeometries();
    }

    void populateChoiceControl(QComboBox *combo_box, int column,
                               const QString &active_filter)
    {
        combo_box->clear();
        combo_box->addItem(QStringLiteral("All"), QString());

        QSet<QString> values;
        for (int row = 0; row < this->model->rowCount(); ++row)
        {
            const QString value = this->model->index(row, column)
                                      .data(Qt::DisplayRole).toString();
            if (!value.isEmpty() && value != QStringLiteral("—"))
                values.insert(value);
        }

        QStringList sorted_values;
        for (const QString &value : values)
            sorted_values.append(value);
        sorted_values.sort(Qt::CaseInsensitive);
        for (const QString &value : sorted_values)
            combo_box->addItem(value, value);

        const int active_index = combo_box->findData(active_filter);
        if (active_index >= 0)
            combo_box->setCurrentIndex(active_index);
    }

    void refreshChoiceControls()
    {
        const int column_count = qMin(this->controls.size(), this->model->columnCount());
        for (int column = 0; column < column_count; ++column)
        {
            QComboBox *combo_box = qobject_cast<QComboBox *>(this->controls.at(column));
            if (combo_box == nullptr)
                continue;

            const QString active_filter = this->proxy_model->columnFilterValue(column);
            {
                const QSignalBlocker blocker(combo_box);
                populateChoiceControl(combo_box, column, active_filter);
            }

            if (!active_filter.isEmpty() && combo_box->findData(active_filter) < 0)
            {
                this->proxy_model->setColumnFilter(column, ColumnFilterKind::Choice,
                                                   QString());
            }
        }
    }

    void updateControlGeometries()
    {
        QHeaderView *header = this->table->horizontalHeader();
        const int x_offset = header->geometry().x();
        for (int column = 0; column < this->controls.size(); ++column)
        {
            QWidget *control = this->controls.at(column);
            if (control == nullptr)
                continue;

            const int x = x_offset + header->sectionViewportPosition(column);
            const int section_width = header->sectionSize(column);
            control->setGeometry(x + 2, 1, qMax(0, section_width - 4), height() - 2);
            control->setVisible(x + section_width > 0 && x < width());
        }
    }

    QTableView *table;
    HydraulicEntityTableModel *model;
    HydraulicEntityFilterProxyModel *proxy_model;
    QList<QWidget *> controls;
};

HydraulicEntityTableWidget::HydraulicEntityTableWidget(HydraulicData *hydraulic_data,
                                                       InfrastructureEntity entity_type,
                                                       const QString &entity_plural,
                                                       QWidget *parent)
    : QWidget(parent),
      hydraulic_data(hydraulic_data),
      entity_type(entity_type),
      label_help(new QLabel(this)),
      table(new QTableView(this)),
      model(new HydraulicEntityTableModel(hydraulic_data, entity_type, this)),
      proxy_model(new HydraulicEntityFilterProxyModel(this))
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    this->proxy_model->setSourceModel(this->model);
    this->proxy_model->setSortRole(SortRole);
    this->proxy_model->setDynamicSortFilter(true);

    this->table->setModel(this->proxy_model);
    this->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->table->setSelectionMode(QAbstractItemView::SingleSelection);
    this->table->setAlternatingRowColors(true);
    this->table->setSortingEnabled(true);
    this->table->setWordWrap(false);
    this->table->setTextElideMode(Qt::ElideRight);
    this->table->verticalHeader()->setVisible(false);
    this->table->verticalHeader()->setDefaultSectionSize(28);
    this->table->horizontalHeader()->setSectionsClickable(true);
    this->table->horizontalHeader()->setSectionsMovable(true);
    this->table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    this->table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    this->table->horizontalHeader()->setTextElideMode(Qt::ElideNone);
    this->table->horizontalHeader()->setDefaultSectionSize(145);
    this->table->horizontalHeader()->setMinimumSectionSize(70);
    const QFontMetrics header_font_metrics(this->table->horizontalHeader()->font());
    this->table->horizontalHeader()->setFixedHeight(header_font_metrics.lineSpacing() * 2 + 12);
    this->table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    this->table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    this->table->viewport()->setCursor(Qt::PointingHandCursor);

    HydraulicEntityFilterBar *filter_bar = new HydraulicEntityFilterBar(
        this->table, this->model,
        static_cast<HydraulicEntityFilterProxyModel *>(this->proxy_model), this);
    layout->addWidget(filter_bar);
    layout->addWidget(this->table, 1);

    this->label_help->setText(QStringLiteral("<b>Hint:</b> ") + entity_plural
                              + QStringLiteral(" are created in the Map Editor."));
    this->label_help->setTextFormat(Qt::RichText);
    this->label_help->setWordWrap(true);
    this->label_help->setMargin(8);
    this->label_help->setFrameShape(QFrame::StyledPanel);
    this->label_help->setBackgroundRole(QPalette::AlternateBase);
    this->label_help->setAutoFillBackground(true);
    layout->addWidget(this->label_help);

    this->table->sortByColumn(0, Qt::AscendingOrder);
    updateColumnWidths();

    connect(this->model, &QAbstractItemModel::modelReset, this,
            [this]()
    {
        updateColumnWidths();
    });

    connect(this->table, &QTableView::clicked, this,
            [this](const QModelIndex &index)
    {
        openEntity(index);
    });

    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this,
            [this]()
    {
        this->model->rebuild();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkGeometryChanged, this,
            [this](quint64)
    {
        this->model->rebuild();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this,
            [this](InfrastructureEntity changed_type, const QUuid &)
    {
        const bool table_is_node = this->entity_type == InfrastructureEntity::Junction
            || this->entity_type == InfrastructureEntity::Reservoir
            || this->entity_type == InfrastructureEntity::Tank;
        const bool table_is_link = this->entity_type == InfrastructureEntity::Pipe
            || this->entity_type == InfrastructureEntity::Pump
            || this->entity_type == InfrastructureEntity::Valve;

        if ((table_is_node && changed_type == this->entity_type) || table_is_link)
            this->model->rebuild();
    });
    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
            [this](InfrastructureEntity changed_type, const QUuid &)
    {
        if (changed_type == this->entity_type)
            this->model->rebuild();
    });
    connect(this->hydraulic_data, &HydraulicData::signalSimulationResultTimelineChanged, this,
            [this](bool)
    {
        this->model->rebuild();
    });
    connect(this->hydraulic_data, &HydraulicData::signalCurrentSimulationResultChanged, this,
            [this](int)
    {
        this->model->rebuild();
    });
}

void HydraulicEntityTableWidget::updateColumnWidths()
{
    const QFontMetrics font_metrics(this->table->horizontalHeader()->font());
    const int column_count = this->table->model()->columnCount();
    for (int column = 0; column < column_count; ++column)
    {
        const QString header_text = this->table->model()
                                        ->headerData(column, Qt::Horizontal, Qt::DisplayRole)
                                        .toString();
        const QStringList lines = header_text.split(QLatin1Char('\n'));
        int text_width = 0;
        for (const QString &line : lines)
            text_width = qMax(text_width, font_metrics.horizontalAdvance(line));

        this->table->setColumnWidth(column, qMax(110, text_width + 30));
    }

    if (column_count > 0)
        this->table->setColumnWidth(0, qMax(170, this->table->columnWidth(0)));
}

void HydraulicEntityTableWidget::openEntity(const QModelIndex &proxy_index)
{
    if (!proxy_index.isValid())
        return;

    const QModelIndex source_index = this->proxy_model->mapToSource(proxy_index);
    const QUuid uuid = source_index.data(EntityUuidRole).toUuid();
    if (uuid.isNull())
        return;

    this->hydraulic_data->setSelectedUuid(this->entity_type, uuid);
}
