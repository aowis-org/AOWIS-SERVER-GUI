#include "epanet_msx_project.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_msx_exporter.h"
#include "epanet_status_helpers.h"

#include <epanetmsx.h>

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>
#include <QtGlobal>

#include <string>

namespace
{
const QString msxBackendName()
{
    return QStringLiteral("EPANET-MSX");
}

bool cancellationRequested(const std::function<bool()> &cancellation_requested)
{
    return cancellation_requested && cancellation_requested();
}

std::string msxErrorMessage(int code)
{
    std::string buffer(256, '\0');
    MSXgeterror(code, buffer.data(), static_cast<int>(buffer.size() - 1));
    return buffer.c_str();
}

std::string epanetLegacyErrorMessage(int code)
{
    std::string buffer(256, '\0');
    ENgeterror(code, buffer.data(), static_cast<int>(buffer.size() - 1));
    return buffer.c_str();
}

HydraulicSimulationStatus msxSuccessStatus()
{
    HydraulicSimulationStatus status;
    status.success = true;
    status.backend_name = msxBackendName();
    return status;
}

HydraulicSimulationStatus msxAdapterErrorStatus(
    HydraulicSimulationStatusStage stage,
    HydraulicSimulationStatusOperation operation,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &message)
{
    HydraulicSimulationStatus status = makeEpanetStatus(
        stage,
        operation,
        entity_type,
        entity_id,
        entity_uuid,
        message);
    status.backend_name = msxBackendName();
    return status;
}

// backend_error_code/message_backend come from MSXgeterror unless
// from_legacy_epanet is set, in which case the failing call was one of the
// legacy EN_* functions MSX itself is built on (e.g. MSXENopen), whose error
// codes and message text come from ENgeterror instead.
HydraulicSimulationStatus msxErrorStatus(
    int return_code,
    HydraulicSimulationStatusStage stage,
    HydraulicSimulationStatusOperation operation,
    const QString &backend_operation,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &message,
    bool from_legacy_epanet = false)
{
    HydraulicSimulationStatus status = makeEpanetStatus(
        stage,
        operation,
        entity_type,
        entity_id,
        entity_uuid,
        message);
    status.backend_name = msxBackendName();
    status.backend_error_code = return_code;
    status.backend_operation = backend_operation;
    status.message_backend = QString::fromStdString(
        from_legacy_epanet ? epanetLegacyErrorMessage(return_code) : msxErrorMessage(return_code));
    return status;
}

HydraulicSimulationStatus msxSolverErrorStatus(
    int return_code,
    HydraulicSimulationStatusStage stage,
    HydraulicSimulationStatusOperation operation,
    const QString &backend_operation,
    const NetworkHydraulic &network,
    const QString &message,
    bool from_legacy_epanet = false)
{
    return msxErrorStatus(
        return_code,
        stage,
        operation,
        backend_operation,
        HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
        network.id,
        network.uuid,
        message,
        from_legacy_epanet);
}

void appendMsxFailure(
    MultiSpeciesSimulationResultTimeline &timeline,
    const HydraulicSimulationStatus &status,
    HydraulicSimulationStatus &first_failure,
    HydraulicSimulationDiagnosticSeverity severity)
{
    if (status.success)
        return;

    timeline.diagnostics.append(epanetDiagnosticFromStatus(status, severity));
    if (first_failure.success)
        first_failure = status;
}

void failTimeline(
    MultiSpeciesSimulationResultTimeline &timeline,
    const HydraulicSimulationStatus &status,
    MultiSpeciesSimulationResultValidity validity)
{
    timeline.status = status;
    timeline.validity = validity;
    timeline.diagnostics.append(epanetDiagnosticFromStatus(status, HydraulicSimulationDiagnosticSeverity::Fatal));
}

MultiSpeciesSimulationResultValidity failedRuntimeValidity(
    const MultiSpeciesSimulationResultTimeline &timeline,
    const HydraulicSimulationStatus &status)
{
    if (status.stage == HydraulicSimulationStatusStage::CloseQuality
        || status.stage == HydraulicSimulationStatusStage::Cleanup)
    {
        return timeline.results.isEmpty()
            ? MultiSpeciesSimulationResultValidity::Invalid
            : MultiSpeciesSimulationResultValidity::Valid;
    }

    return timeline.results.isEmpty()
        ? MultiSpeciesSimulationResultValidity::Invalid
        : MultiSpeciesSimulationResultValidity::Partial;
}

bool writeTextFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(text.toUtf8());
    return file.error() == QFileDevice::NoError;
}

struct IndexedEntity
{
    QString id;
    QUuid uuid;
    int index = 0;
};

struct IndexedSpecies
{
    QString id;
    QUuid uuid;
    int index = 0;
};

template<typename Entity>
HydraulicSimulationStatus resolveNodeIndices(
    const QList<Entity> &entities,
    QList<IndexedEntity> &target,
    HydraulicSimulationStatusEntityType entity_type)
{
    for (const Entity &entity : entities)
    {
        int index = 0;
        const QByteArray id_utf8 = entity.id.toUtf8();
        const int error = ENgetnodeindex(id_utf8.constData(), &index);
        if (error != 0)
        {
            HydraulicSimulationStatus status = msxErrorStatus(
                error,
                HydraulicSimulationStatusStage::ReadResults,
                HydraulicSimulationStatusOperation::ResolveEntity,
                QStringLiteral("ENgetnodeindex"),
                entity_type,
                entity.id,
                entity.uuid,
                QStringLiteral("Failed to resolve an EPANET node index while preparing multi-species result reads"),
                true);
            status.entity.index = index;
            return status;
        }
        target.append(IndexedEntity{entity.id, entity.uuid, index});
    }

    return msxSuccessStatus();
}

template<typename Entity>
HydraulicSimulationStatus resolveLinkIndices(
    const QList<Entity> &entities,
    QList<IndexedEntity> &target,
    HydraulicSimulationStatusEntityType entity_type)
{
    for (const Entity &entity : entities)
    {
        int index = 0;
        const QByteArray id_utf8 = entity.id.toUtf8();
        const int error = ENgetlinkindex(id_utf8.constData(), &index);
        if (error != 0)
        {
            HydraulicSimulationStatus status = msxErrorStatus(
                error,
                HydraulicSimulationStatusStage::ReadResults,
                HydraulicSimulationStatusOperation::ResolveEntity,
                QStringLiteral("ENgetlinkindex"),
                entity_type,
                entity.id,
                entity.uuid,
                QStringLiteral("Failed to resolve an EPANET link index while preparing multi-species result reads"),
                true);
            status.entity.index = index;
            return status;
        }
        target.append(IndexedEntity{entity.id, entity.uuid, index});
    }

    return msxSuccessStatus();
}

HydraulicSimulationStatus readSpeciesValues(
    int msx_entity_type,
    const IndexedEntity &entity,
    HydraulicSimulationStatusEntityType entity_type,
    HydraulicSimulationStatusStage stage,
    HydraulicSimulationStatusOperation operation,
    double simulation_time_s,
    const QList<IndexedSpecies> &species_by_index,
    QList<MultiSpeciesResultValue> &values)
{
    for (const IndexedSpecies &species : species_by_index)
    {
        double value = 0.0;
        const int error = MSXgetqual(msx_entity_type, entity.index, species.index, &value);
        if (error != 0)
        {
            HydraulicSimulationStatus status = msxErrorStatus(
                error,
                stage,
                operation,
                QStringLiteral("MSXgetqual"),
                entity_type,
                entity.id,
                entity.uuid,
                QStringLiteral("Failed to read a multi-species concentration result"));
            status.property = HydraulicSimulationStatusProperty::Quality;
            status.entity.index = entity.index;
            status.details.append(QStringLiteral("Species: %1").arg(species.id));
            status.details.append(QStringLiteral("Species UUID: %1").arg(species.uuid.toString(QUuid::WithoutBraces)));
            status.details.append(QStringLiteral("MSX species index: %1").arg(species.index));
            status.details.append(QStringLiteral("Simulation time: %1 s").arg(simulation_time_s, 0, 'g', 17));
            return status;
        }

        if (!qIsFinite(value))
        {
            HydraulicSimulationStatus status = msxAdapterErrorStatus(
                stage,
                operation,
                entity_type,
                entity.id,
                entity.uuid,
                QStringLiteral("EPANET-MSX returned a non-finite concentration result"));
            status.backend_operation = QStringLiteral("MSXgetqual");
            status.property = HydraulicSimulationStatusProperty::Quality;
            status.entity.index = entity.index;
            status.details.append(QStringLiteral("Species: %1").arg(species.id));
            status.details.append(QStringLiteral("Species UUID: %1").arg(species.uuid.toString(QUuid::WithoutBraces)));
            status.details.append(QStringLiteral("MSX species index: %1").arg(species.index));
            status.details.append(QStringLiteral("Simulation time: %1 s").arg(simulation_time_s, 0, 'g', 17));
            return status;
        }

        values.append(MultiSpeciesResultValue{species.uuid, value});
    }

    return msxSuccessStatus();
}
}

HydraulicSimulationStatus EpanetMsxProject::run(
    const NetworkHydraulic &network,
    const MultiSpeciesRunOptions &run_options,
    const QString &configured_inp_text,
    const QString &hydraulic_file_path,
    MultiSpeciesSimulationResultTimeline &timeline,
    const std::function<bool()> &cancellation_requested,
    bool &cancelled)
{
    cancelled = false;
    const QDateTime requested_simulation_start_utc = timeline.simulation_start_utc;
    timeline = MultiSpeciesSimulationResultTimeline();
    timeline.simulation_start_utc = requested_simulation_start_utc.isValid()
        ? requested_simulation_start_utc
        : QDateTime::currentDateTimeUtc();

    if (cancellationRequested(cancellation_requested))
    {
        cancelled = true;
        timeline.status = msxSuccessStatus();
        timeline.validity = MultiSpeciesSimulationResultValidity::NotRun;
        return timeline.status;
    }

    const QFileInfo hydraulic_file_info(hydraulic_file_path);
    if (hydraulic_file_path.isEmpty() || !hydraulic_file_info.exists() || !hydraulic_file_info.isFile())
    {
        const HydraulicSimulationStatus status = msxAdapterErrorStatus(
            HydraulicSimulationStatusStage::RunQuality,
            HydraulicSimulationStatusOperation::RunMultiSpecies,
            HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
            network.id,
            network.uuid,
            QStringLiteral("A reusable EPANET hydraulic results file is required for the multi-species run"));
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    if (configured_inp_text.trimmed().isEmpty())
    {
        const HydraulicSimulationStatus status = msxAdapterErrorStatus(
            HydraulicSimulationStatusStage::RunQuality,
            HydraulicSimulationStatusOperation::RunMultiSpecies,
            HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
            network.id,
            network.uuid,
            QStringLiteral("A configured EPANET INP snapshot matching the reusable hydraulic results is required for the multi-species run"));
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    QString msx_text;
    HydraulicSimulationStatus status = retrieveEpanetMsxText(network, run_options, msx_text);
    if (!status.success)
    {
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    QTemporaryDir scratch_dir;
    if (!scratch_dir.isValid())
    {
        status = msxAdapterErrorStatus(
            HydraulicSimulationStatusStage::RunQuality,
            HydraulicSimulationStatusOperation::OpenMultiSpecies,
            HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
            network.id,
            network.uuid,
            QStringLiteral("Failed to create a temporary directory for the multi-species run"));
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    const QString inp_path = scratch_dir.filePath(QStringLiteral("network.inp"));
    const QString msx_path = scratch_dir.filePath(QStringLiteral("network.msx"));
    const QString rpt_path = scratch_dir.filePath(QStringLiteral("network.rpt"));
    const QString out_path = scratch_dir.filePath(QStringLiteral("network.out"));

    if (!writeTextFile(inp_path, configured_inp_text) || !writeTextFile(msx_path, msx_text))
    {
        status = msxAdapterErrorStatus(
            HydraulicSimulationStatusStage::RunQuality,
            HydraulicSimulationStatusOperation::OpenMultiSpecies,
            HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
            network.id,
            network.uuid,
            QStringLiteral("Failed to write the temporary INP/MSX files for the multi-species run"));
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    if (cancellationRequested(cancellation_requested))
    {
        cancelled = true;
        timeline.status = msxSuccessStatus();
        timeline.validity = MultiSpeciesSimulationResultValidity::NotRun;
        return timeline.status;
    }

    // Everything from here on touches MSX's process-global state.
    static QMutex msx_mutex;
    QMutexLocker locker(&msx_mutex);

    const QByteArray inp_path_native = QFile::encodeName(inp_path);
    const QByteArray rpt_path_native = QFile::encodeName(rpt_path);
    const QByteArray out_path_native = QFile::encodeName(out_path);
    std::string msx_path_mutable = QFile::encodeName(msx_path).toStdString();
    std::string hydraulic_path_mutable = QFile::encodeName(hydraulic_file_path).toStdString();

    int error = MSXENopen(inp_path_native.constData(), rpt_path_native.constData(), out_path_native.constData());
    if (error != 0)
    {
        status = msxSolverErrorStatus(
            error,
            HydraulicSimulationStatusStage::RunQuality,
            HydraulicSimulationStatusOperation::OpenMultiSpecies,
            QStringLiteral("MSXENopen"),
            network,
            QStringLiteral("Failed to open the EPANET project for multi-species execution"),
            true);
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    error = MSXopen(msx_path_mutable.data());
    if (error != 0)
    {
        status = msxSolverErrorStatus(
            error,
            HydraulicSimulationStatusStage::RunQuality,
            HydraulicSimulationStatusOperation::OpenMultiSpecies,
            QStringLiteral("MSXopen"),
            network,
            QStringLiteral("Failed to open the multi-species reaction model"));
        MSXENclose();
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    error = MSXusehydfile(hydraulic_path_mutable.data());
    if (error != 0)
    {
        status = msxSolverErrorStatus(
            error,
            HydraulicSimulationStatusStage::RunQuality,
            HydraulicSimulationStatusOperation::RunMultiSpecies,
            QStringLiteral("MSXusehydfile"),
            network,
            QStringLiteral("Failed to load the AOWIS EPANET hydraulic results for the multi-species run"));
        MSXclose();
        MSXENclose();
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    // Resolve species indices from whatever MSXopen actually parsed, rather
    // than re-deriving the run's selection here -- the parsed model is the
    // single source of truth for which species this run actually covers.
    int species_count = 0;
    error = MSXgetcount(MSX_SPECIES, &species_count);
    if (error != 0)
    {
        status = msxSolverErrorStatus(
            error,
            HydraulicSimulationStatusStage::ReadResults,
            HydraulicSimulationStatusOperation::ResolveEntity,
            QStringLiteral("MSXgetcount"),
            network,
            QStringLiteral("Failed to read the multi-species species count"));
        MSXclose();
        MSXENclose();
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    QHash<QString, QUuid> species_uuid_by_id;
    for (const MultiSpeciesSpecies &species : network.multi_species.species)
        species_uuid_by_id.insert(species.id, species.uuid);

    // The complete species set has already been loaded into MSX above. The
    // run option only controls which species we read back into AOWIS results;
    // it must never change the reaction model that MSX solves.
    QList<IndexedSpecies> species_by_index;
    for (int species_index = 1; species_index <= species_count; species_index++)
    {
        int id_len = 0;
        error = MSXgetIDlen(MSX_SPECIES, species_index, &id_len);
        if (error != 0)
        {
            status = msxSolverErrorStatus(
                error,
                HydraulicSimulationStatusStage::ReadResults,
                HydraulicSimulationStatusOperation::ResolveEntity,
                QStringLiteral("MSXgetIDlen"),
                network,
                QStringLiteral("Failed to read a multi-species identifier length"));
            status.details.append(QStringLiteral("MSX species index: %1").arg(species_index));
            MSXclose();
            MSXENclose();
            failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
            return status;
        }

        std::string id_buffer(static_cast<std::size_t>(id_len) + 1, '\0');
        error = MSXgetID(MSX_SPECIES, species_index, id_buffer.data(), id_len + 1);
        if (error != 0)
        {
            status = msxSolverErrorStatus(
                error,
                HydraulicSimulationStatusStage::ReadResults,
                HydraulicSimulationStatusOperation::ResolveEntity,
                QStringLiteral("MSXgetID"),
                network,
                QStringLiteral("Failed to read a multi-species identifier"));
            status.details.append(QStringLiteral("MSX species index: %1").arg(species_index));
            MSXclose();
            MSXENclose();
            failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
            return status;
        }

        const QString species_id = QString::fromStdString(id_buffer.c_str());
        const QUuid species_uuid = species_uuid_by_id.value(species_id);
        if (species_uuid.isNull())
        {
            status = msxAdapterErrorStatus(
                HydraulicSimulationStatusStage::ReadResults,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
                network.id,
                network.uuid,
                QStringLiteral("EPANET-MSX returned a species that cannot be mapped back to the AOWIS model"));
            status.backend_operation = QStringLiteral("MSXgetID");
            status.details.append(QStringLiteral("Species: %1").arg(species_id));
            status.details.append(QStringLiteral("MSX species index: %1").arg(species_index));
            MSXclose();
            MSXENclose();
            failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
            return status;
        }

        if (run_options.species_uuids.isEmpty() || run_options.species_uuids.contains(species_uuid))
            species_by_index.append(IndexedSpecies{species_id, species_uuid, species_index});
    }

    QList<IndexedEntity> node_junctions;
    QList<IndexedEntity> node_reservoirs;
    QList<IndexedEntity> node_tanks;
    QList<IndexedEntity> link_pipes;
    QList<IndexedEntity> link_pumps;
    QList<IndexedEntity> link_valves;

    status = resolveNodeIndices(
        network.nodes_junctions,
        node_junctions,
        HydraulicSimulationStatusEntityType::Junction);
    if (status.success)
    {
        status = resolveNodeIndices(
            network.nodes_reservoirs,
            node_reservoirs,
            HydraulicSimulationStatusEntityType::Reservoir);
    }
    if (status.success)
    {
        status = resolveNodeIndices(
            network.nodes_tanks,
            node_tanks,
            HydraulicSimulationStatusEntityType::Tank);
    }
    if (status.success)
    {
        status = resolveLinkIndices(
            network.links_pipes,
            link_pipes,
            HydraulicSimulationStatusEntityType::Pipe);
    }
    if (status.success)
    {
        status = resolveLinkIndices(
            network.links_pumps,
            link_pumps,
            HydraulicSimulationStatusEntityType::Pump);
    }
    if (status.success)
    {
        status = resolveLinkIndices(
            network.links_valves,
            link_valves,
            HydraulicSimulationStatusEntityType::Valve);
    }

    if (!status.success)
    {
        MSXclose();
        MSXENclose();
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    error = MSXinit(0);
    if (error != 0)
    {
        status = msxSolverErrorStatus(
            error,
            HydraulicSimulationStatusStage::RunQuality,
            HydraulicSimulationStatusOperation::InitializeMultiSpecies,
            QStringLiteral("MSXinit"),
            network,
            QStringLiteral("Failed to initialize multi-species water-quality state"));
        MSXclose();
        MSXENclose();
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    HydraulicSimulationStatus first_failure = msxSuccessStatus();
    bool step_loop_cancelled = false;
    double t = 0.0;
    double tleft = 1.0;
    double previous_t = -1.0;

    while (tleft > 0.0)
    {
        if (cancellationRequested(cancellation_requested))
        {
            step_loop_cancelled = true;
            break;
        }

        error = MSXstep(&t, &tleft);
        if (error != 0)
        {
            status = msxSolverErrorStatus(
                error,
                HydraulicSimulationStatusStage::RunQuality,
                HydraulicSimulationStatusOperation::StepMultiSpecies,
                QStringLiteral("MSXstep"),
                network,
                QStringLiteral("Failed to advance the multi-species timestep"));
            appendMsxFailure(
                timeline,
                status,
                first_failure,
                HydraulicSimulationDiagnosticSeverity::Fatal);
            break;
        }

        if (!qIsFinite(t) || t < 0.0 || !qIsFinite(tleft) || tleft < 0.0)
        {
            status = msxAdapterErrorStatus(
                HydraulicSimulationStatusStage::RunQuality,
                HydraulicSimulationStatusOperation::StepMultiSpecies,
                HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
                network.id,
                network.uuid,
                QStringLiteral("EPANET-MSX returned an invalid simulation time"));
            status.backend_operation = QStringLiteral("MSXstep");
            status.details.append(QStringLiteral("Simulation time: %1").arg(t, 0, 'g', 17));
            status.details.append(QStringLiteral("Time left: %1").arg(tleft, 0, 'g', 17));
            appendMsxFailure(
                timeline,
                status,
                first_failure,
                HydraulicSimulationDiagnosticSeverity::Fatal);
            break;
        }

        if (previous_t >= 0.0 && t <= previous_t)
        {
            status = msxAdapterErrorStatus(
                HydraulicSimulationStatusStage::RunQuality,
                HydraulicSimulationStatusOperation::StepMultiSpecies,
                HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
                network.id,
                network.uuid,
                QStringLiteral("EPANET-MSX simulation time did not advance"));
            status.backend_operation = QStringLiteral("MSXstep");
            status.details.append(QStringLiteral("Previous simulation time: %1").arg(previous_t, 0, 'g', 17));
            status.details.append(QStringLiteral("Current simulation time: %1").arg(t, 0, 'g', 17));
            appendMsxFailure(
                timeline,
                status,
                first_failure,
                HydraulicSimulationDiagnosticSeverity::Fatal);
            break;
        }
        previous_t = t;

        MultiSpeciesSimulationResult result;
        result.time_elapsed_s = static_cast<quint64>(t);
        result.status = msxSuccessStatus();

        bool result_read_failed = false;

        for (const IndexedEntity &entity : node_junctions)
        {
            MultiSpeciesSimulationResultNodeJunction node_result;
            node_result.id = entity.id;
            node_result.uuid = entity.uuid;
            status = readSpeciesValues(
                MSX_NODE,
                entity,
                HydraulicSimulationStatusEntityType::Junction,
                HydraulicSimulationStatusStage::ReadJunctionResults,
                HydraulicSimulationStatusOperation::ReadNodeResult,
                t,
                species_by_index,
                node_result.species_values);
            if (!status.success)
            {
                appendMsxFailure(
                    timeline,
                    status,
                    first_failure,
                    HydraulicSimulationDiagnosticSeverity::Error);
                result_read_failed = true;
                break;
            }
            result.nodes_junctions.append(node_result);
        }

        if (!result_read_failed)
        {
            for (const IndexedEntity &entity : node_reservoirs)
            {
                MultiSpeciesSimulationResultNodeReservoir node_result;
                node_result.id = entity.id;
                node_result.uuid = entity.uuid;
                status = readSpeciesValues(
                    MSX_NODE,
                    entity,
                    HydraulicSimulationStatusEntityType::Reservoir,
                    HydraulicSimulationStatusStage::ReadReservoirResults,
                    HydraulicSimulationStatusOperation::ReadNodeResult,
                    t,
                    species_by_index,
                    node_result.species_values);
                if (!status.success)
                {
                    appendMsxFailure(
                        timeline,
                        status,
                        first_failure,
                        HydraulicSimulationDiagnosticSeverity::Error);
                    result_read_failed = true;
                    break;
                }
                result.nodes_reservoirs.append(node_result);
            }
        }

        if (!result_read_failed)
        {
            for (const IndexedEntity &entity : node_tanks)
            {
                MultiSpeciesSimulationResultNodeTank node_result;
                node_result.id = entity.id;
                node_result.uuid = entity.uuid;
                status = readSpeciesValues(
                    MSX_NODE,
                    entity,
                    HydraulicSimulationStatusEntityType::Tank,
                    HydraulicSimulationStatusStage::ReadTankResults,
                    HydraulicSimulationStatusOperation::ReadNodeResult,
                    t,
                    species_by_index,
                    node_result.species_values);
                if (!status.success)
                {
                    appendMsxFailure(
                        timeline,
                        status,
                        first_failure,
                        HydraulicSimulationDiagnosticSeverity::Error);
                    result_read_failed = true;
                    break;
                }
                result.nodes_tanks.append(node_result);
            }
        }

        if (!result_read_failed)
        {
            for (const IndexedEntity &entity : link_pipes)
            {
                MultiSpeciesSimulationResultLinkPipe link_result;
                link_result.id = entity.id;
                link_result.uuid = entity.uuid;
                status = readSpeciesValues(
                    MSX_LINK,
                    entity,
                    HydraulicSimulationStatusEntityType::Pipe,
                    HydraulicSimulationStatusStage::ReadPipeResults,
                    HydraulicSimulationStatusOperation::ReadLinkResult,
                    t,
                    species_by_index,
                    link_result.species_values);
                if (!status.success)
                {
                    appendMsxFailure(
                        timeline,
                        status,
                        first_failure,
                        HydraulicSimulationDiagnosticSeverity::Error);
                    result_read_failed = true;
                    break;
                }
                result.links_pipes.append(link_result);
            }
        }

        if (!result_read_failed)
        {
            for (const IndexedEntity &entity : link_pumps)
            {
                MultiSpeciesSimulationResultLinkPump link_result;
                link_result.id = entity.id;
                link_result.uuid = entity.uuid;
                status = readSpeciesValues(
                    MSX_LINK,
                    entity,
                    HydraulicSimulationStatusEntityType::Pump,
                    HydraulicSimulationStatusStage::ReadPumpResults,
                    HydraulicSimulationStatusOperation::ReadLinkResult,
                    t,
                    species_by_index,
                    link_result.species_values);
                if (!status.success)
                {
                    appendMsxFailure(
                        timeline,
                        status,
                        first_failure,
                        HydraulicSimulationDiagnosticSeverity::Error);
                    result_read_failed = true;
                    break;
                }
                result.links_pumps.append(link_result);
            }
        }

        if (!result_read_failed)
        {
            for (const IndexedEntity &entity : link_valves)
            {
                MultiSpeciesSimulationResultLinkValve link_result;
                link_result.id = entity.id;
                link_result.uuid = entity.uuid;
                status = readSpeciesValues(
                    MSX_LINK,
                    entity,
                    HydraulicSimulationStatusEntityType::Valve,
                    HydraulicSimulationStatusStage::ReadValveResults,
                    HydraulicSimulationStatusOperation::ReadLinkResult,
                    t,
                    species_by_index,
                    link_result.species_values);
                if (!status.success)
                {
                    appendMsxFailure(
                        timeline,
                        status,
                        first_failure,
                        HydraulicSimulationDiagnosticSeverity::Error);
                    result_read_failed = true;
                    break;
                }
                result.links_valves.append(link_result);
            }
        }

        if (result_read_failed)
            break;

        timeline.results.append(result);

        if (cancellationRequested(cancellation_requested))
        {
            step_loop_cancelled = true;
            break;
        }
    }

    const int msx_close_error = MSXclose();
    if (msx_close_error != 0)
    {
        status = msxSolverErrorStatus(
            msx_close_error,
            HydraulicSimulationStatusStage::CloseQuality,
            HydraulicSimulationStatusOperation::CloseMultiSpecies,
            QStringLiteral("MSXclose"),
            network,
            QStringLiteral("Failed to close the multi-species reaction model"));
        appendMsxFailure(
            timeline,
            status,
            first_failure,
            HydraulicSimulationDiagnosticSeverity::Error);
    }

    const int en_close_error = MSXENclose();
    if (en_close_error != 0)
    {
        status = msxSolverErrorStatus(
            en_close_error,
            HydraulicSimulationStatusStage::CloseQuality,
            HydraulicSimulationStatusOperation::CloseMultiSpecies,
            QStringLiteral("MSXENclose"),
            network,
            QStringLiteral("Failed to close the EPANET project after the multi-species run"),
            true);
        appendMsxFailure(
            timeline,
            status,
            first_failure,
            HydraulicSimulationDiagnosticSeverity::Error);
    }

    if (step_loop_cancelled)
    {
        cancelled = true;
        timeline.status = msxSuccessStatus();
        timeline.validity = timeline.results.isEmpty()
            ? MultiSpeciesSimulationResultValidity::NotRun
            : MultiSpeciesSimulationResultValidity::Partial;
        return timeline.status;
    }

    if (!first_failure.success)
    {
        timeline.status = first_failure;
        timeline.validity = failedRuntimeValidity(timeline, first_failure);
        return first_failure;
    }

    timeline.status = msxSuccessStatus();
    timeline.validity = MultiSpeciesSimulationResultValidity::Valid;
    return timeline.status;
}
