#include "simulation_manager.h"

#include "simulation_statistics_dialog.h"
#include "simulation_diagnostics_dialog.h"

#include <aowis/epanet/epanet_runner.h>
#include <aowis/epanet/epanet_result_import.h>
#include <aowis/epanet/epanet_result_run.h>
#include <aowis/epanet/epanet_run_request.h>

#include <QByteArray>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QTemporaryDir>

#include <memory>
#include <utility>

#ifndef Q_OS_WASM
#include <QSaveFile>
#else
#include <emscripten.h>

EM_JS(void, aowisOpenEpanetInpFile, (),
{
    const input = document.createElement("input");
    input.type = "file";
    input.accept = ".inp";
    input.style.display = "none";

    const cleanup = () => {
        input.remove();
    };

    input.addEventListener("change", async () => {
        const file = input.files && input.files.length > 0 ? input.files[0] : null;
        if (!file)
        {
            cleanup();
            return;
        }

        try
        {
            const bytes = new Uint8Array(await file.arrayBuffer());
            const file_name_utf8 = stringToNewUTF8(file.name);
            const contents = _malloc(bytes.length);
            if (bytes.length > 0)
                HEAPU8.set(bytes, contents);

            _aowisReceiveEpanetInpFile(file_name_utf8, contents, bytes.length);

            _free(contents);
            _free(file_name_utf8);
        }
        finally
        {
            cleanup();
        }
    }, { once: true });

    document.body.appendChild(input);
    input.click();
});

EM_JS(void, aowisDownloadTextFile, (const char *filename_utf8, const char *contents_utf8),
{
    if (filename_utf8 === 0 || contents_utf8 === 0)
        return;

    const filename = UTF8ToString(filename_utf8);
    const contents = UTF8ToString(contents_utf8);
    const blob = new Blob([contents], { type: "text/plain;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = filename;
    link.style.display = "none";
    document.body.appendChild(link);
    link.click();
    link.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
});
#endif

#ifdef Q_OS_WASM
namespace
{
QPointer<SimulationManager> pending_epanet_import_manager;
}

extern "C" EMSCRIPTEN_KEEPALIVE void aowisReceiveEpanetInpFile(
    const char *file_name_utf8,
    const char *contents,
    int size)
{
    if (!pending_epanet_import_manager || file_name_utf8 == nullptr || size < 0)
        return;

    const QString file_name = QString::fromUtf8(file_name_utf8);
    const QByteArray file_content(contents, size);
    QMetaObject::invokeMethod(
        pending_epanet_import_manager,
        "importEpanetNetworkContent",
        Qt::DirectConnection,
        Q_ARG(QString, file_name),
        Q_ARG(QByteArray, file_content));
}
#endif

namespace
{
void showAndActivateDialog(QDialog *dialog)
{
    if (dialog == nullptr)
        return;

    if (dialog->isMinimized())
        dialog->setWindowState(dialog->windowState() & ~Qt::WindowMinimized);

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void showMessageBox(
    QWidget *parent,
    QMessageBox::Icon icon,
    const QString &title,
    const QString &text)
{
    QMessageBox *message_box = new QMessageBox(icon, title, text, QMessageBox::Ok, parent);
    message_box->setAttribute(Qt::WA_DeleteOnClose);
    message_box->open();
    message_box->raise();
    message_box->activateWindow();
}

QString diagnosticDetails(const HydraulicSimulationDiagnostic &diagnostic)
{
    QStringList details;
    if (!diagnostic.message.isEmpty())
        details.append(diagnostic.message);
    if (!diagnostic.message_backend.isEmpty())
        details.append(diagnostic.message_backend);
    details.append(diagnostic.details);
    return details.join('\n');
}

QString importFailureDetails(const EpanetResultImport &result)
{
    QStringList details;

    if (!result.status.message.isEmpty())
        details.append(result.status.message);
    if (!result.status.message_backend.isEmpty())
        details.append(result.status.message_backend);
    details.append(result.status.details);

    for (const HydraulicSimulationDiagnostic &diagnostic : result.diagnostics)
    {
        const QString diagnostic_text = diagnosticDetails(diagnostic);
        if (!diagnostic_text.isEmpty() && !details.contains(diagnostic_text))
            details.append(diagnostic_text);
    }

    if (details.isEmpty())
        return QStringLiteral("EPANET could not import the INP project.");

    return details.join(QStringLiteral("\n\n"));
}

void showDetailedMessageBox(
    QWidget *parent,
    QMessageBox::Icon icon,
    const QString &title,
    const QString &text,
    const QString &details)
{
    QMessageBox *message_box = new QMessageBox(icon, title, text, QMessageBox::Ok, parent);
    message_box->setAttribute(Qt::WA_DeleteOnClose);
    if (!details.isEmpty())
        message_box->setDetailedText(details);
    message_box->open();
    message_box->raise();
    message_box->activateWindow();
}

QString exportFailureDetails(const HydraulicSimulationStatus &status)
{
    QStringList details;

    if (!status.message.isEmpty())
        details.append(status.message);

    if (!status.message_backend.isEmpty())
        details.append(status.message_backend);

    for (const QString &detail : status.details)
        details.append(detail);

    if (details.isEmpty())
        return QStringLiteral("EPANET could not generate the INP document.");

    return details.join('\n');
}

QString qualityAnalysisName(WaterQualityAnalysisType analysis)
{
    switch (analysis)
    {
    case WaterQualityAnalysisType::None:
        return QStringLiteral("None");
    case WaterQualityAnalysisType::Chemical:
        return QStringLiteral("Chemical");
    case WaterQualityAnalysisType::WaterAge:
        return QStringLiteral("Water age");
    case WaterQualityAnalysisType::SourceTrace:
        return QStringLiteral("Source trace");
    }

    return QStringLiteral("Unknown quality analysis");
}

QString runReportText(const EpanetResultRun &run_result)
{
    QStringList sections;

    if (!run_result.report_lines.isEmpty())
    {
        sections.append(QStringLiteral("=== Hydraulics ==="));
        sections.append(run_result.report_lines.join('\n'));
    }

    for (const EpanetQualityResult &quality_result : run_result.quality_results)
    {
        if (quality_result.report_lines.isEmpty())
            continue;

        sections.append(QStringLiteral("=== Water quality: %1 ===")
                            .arg(qualityAnalysisName(quality_result.options.analysis)));
        sections.append(quality_result.report_lines.join('\n'));
    }

    return sections.join(QStringLiteral("\n\n"));
}
}

SimulationManager::SimulationManager(HydraulicData *hydraulic_data, QObject *parent)
    : QObject{parent},
    hydraulic_data(hydraulic_data)
{
}

SimulationManager::~SimulationManager()
{
    if (this->simulation_cancellation_flag)
        this->simulation_cancellation_flag->store(true);

#ifndef Q_OS_WASM
    if (this->simulation_thread && this->simulation_thread->isRunning())
        this->simulation_thread->wait();
#endif
}

void SimulationManager::runOrStop(const QList<WaterQualityAnalysisType> &quality_analyses)
{
    if (this->simulation_running)
    {
        stop();
        return;
    }

    run(quality_analyses);
}

void SimulationManager::stop()
{
    if (!this->simulation_running || !this->simulation_cancellation_flag)
        return;

    if (!this->simulation_cancellation_flag->exchange(true))
        emit signalSimulationStopRequested();
}

void SimulationManager::run(const QList<WaterQualityAnalysisType> &quality_analyses)
{
    if (this->simulation_running)
        return;

    const NetworkHydraulic network_hydraulic = this->hydraulic_data->networkHydraulic();

    EpanetRunRequest run_request;
    run_request.network = network_hydraulic;

    for (const WaterQualityAnalysisType analysis : quality_analyses)
    {
        WaterQualitySolverOptions quality_options;
        for (const WaterQualitySolverOptions &stored_options : this->hydraulic_data->simulationQualityRunOptions())
        {
            if (stored_options.analysis == analysis)
            {
                quality_options = stored_options;
                break;
            }
        }

        quality_options.analysis = analysis;
        if (analysis == WaterQualityAnalysisType::Chemical && quality_options.chemical_name.isEmpty())
            quality_options.chemical_name = QStringLiteral("Chlorine");
        else if (analysis == WaterQualityAnalysisType::SourceTrace)
            quality_options.trace_node_uuid = this->hydraulic_data->sourceTraceOriginNodeUuid();
        run_request.quality_runs.append(quality_options);
    }

    this->simulation_running = true;
    emit signalSimulationStarted();
    this->epanet_log.clear();
    if (this->widget_epanet_log)
        this->widget_epanet_log->clear();
    emit signalEpanetLogAvailabilityChanged(false);

    std::shared_ptr<EpanetResultRun> run_result = std::make_shared<EpanetResultRun>();
    const std::shared_ptr<std::atomic_bool> cancellation_flag = std::make_shared<std::atomic_bool>(false);
    this->simulation_cancellation_flag = cancellation_flag;

    QThread *thread = QThread::create([run_request, run_result, cancellation_flag]()
    {
        EpanetRunner runner;
        *run_result = runner.run(run_request, [cancellation_flag]()
        {
            return cancellation_flag->load();
        });
    });
    this->simulation_thread = thread;

    connect(thread, &QThread::finished, this, [this, thread, run_result, cancellation_flag]()
    {
        if (this->simulation_thread == thread)
            this->simulation_thread = nullptr;

        if (this->simulation_cancellation_flag == cancellation_flag)
            this->simulation_cancellation_flag.reset();

        this->simulation_running = false;
        finishSimulation(*run_result);
        emit signalSimulationFinished(run_result->cancelled);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void SimulationManager::finishSimulation(const EpanetResultRun &run_result)
{
    this->epanet_log = runReportText(run_result);
    if (this->widget_epanet_log)
        this->widget_epanet_log->setPlainText(this->epanet_log);
    emit signalEpanetLogAvailabilityChanged(!this->epanet_log.isEmpty());
    qDebug().noquote() << this->epanet_log;

    HydraulicSimulationResultTimeline hydraulic_timeline = run_result.result_timeline;
    if (!run_result.diagnostics.isEmpty())
        hydraulic_timeline.diagnostics = run_result.diagnostics;

    QList<WaterQualitySimulationResultTimeline> quality_timelines;
    for (const EpanetQualityResult &quality_result : run_result.quality_results)
        quality_timelines.append(quality_result.result_timeline);

    this->hydraulic_data->setSimulationResultTimeline(hydraulic_timeline);
    this->hydraulic_data->setWaterQualitySimulationResultTimelines(quality_timelines);

    bool has_error_diagnostic = false;
    bool has_warning_diagnostic = false;
    for (const HydraulicSimulationDiagnostic &diagnostic : run_result.diagnostics)
    {
        if (diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Warning)
            has_warning_diagnostic = true;

        if (diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Error
            || diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Fatal)
        {
            has_error_diagnostic = true;
        }
    }

    if (run_result.cancelled)
        return;

    if (run_result.state == EpanetRunState::Error
        || run_result.state == EpanetRunState::Warning
        || has_error_diagnostic
        || has_warning_diagnostic)
    {
        showSimulationDiagnostics();
    }

    if (run_result.state == EpanetRunState::Error)
    {
        qWarning().noquote() << "EPANET simulation failed:" << run_result.status.message;

        if (!run_result.status.message_backend.isEmpty())
            qWarning().noquote() << run_result.status.message_backend;

        return;
    }

    if (run_result.state == EpanetRunState::Warning
        || has_error_diagnostic
        || has_warning_diagnostic
        || hydraulic_timeline.validity != HydraulicSimulationResultValidity::Valid)
    {
        return;
    }

    QWidget *main_window = qobject_cast<QWidget *>(parent());
    if (main_window == nullptr)
        main_window = QApplication::activeWindow();

    showMessageBox(
        main_window,
        QMessageBox::Information,
        tr("Simulation Complete"),
        tr("Simulation and all configured water-quality analyses completed successfully."));
}

void SimulationManager::showSimulationStatistics()
{
    if (!this->hydraulic_data->hasSimulationResults())
        return;

    if (this->dialog_simulation_statistics)
    {
        showAndActivateDialog(this->dialog_simulation_statistics);
        return;
    }

    QWidget *main_window = qobject_cast<QWidget *>(parent());
    if (main_window == nullptr)
        main_window = QApplication::activeWindow();

    this->dialog_simulation_statistics = new SimulationStatisticsDialog(this->hydraulic_data, main_window);
    showAndActivateDialog(this->dialog_simulation_statistics);
}

void SimulationManager::showSimulationDiagnostics()
{
    if (this->hydraulic_data == nullptr)
        return;

    const std::optional<HydraulicSimulationResultTimeline> &result_timeline = this->hydraulic_data->simulationResultTimeline();
    if (!result_timeline.has_value() || result_timeline->diagnostics.isEmpty())
        return;

    if (this->dialog_simulation_diagnostics)
    {
        showAndActivateDialog(this->dialog_simulation_diagnostics);
        return;
    }

    QWidget *main_window = qobject_cast<QWidget *>(parent());
    if (main_window == nullptr)
        main_window = QApplication::activeWindow();

    this->dialog_simulation_diagnostics = new SimulationDiagnosticsDialog(this->hydraulic_data, main_window);
    showAndActivateDialog(this->dialog_simulation_diagnostics);
}

void SimulationManager::showEpanetLog()
{
    if (this->dialog_epanet_log)
    {
        showAndActivateDialog(this->dialog_epanet_log);
        return;
    }

    QWidget *main_window = qobject_cast<QWidget *>(parent());
    if (main_window == nullptr)
        main_window = QApplication::activeWindow();

    this->dialog_epanet_log = new QDialog(
        main_window,
        Qt::Dialog
        | Qt::WindowTitleHint
        | Qt::WindowCloseButtonHint
        | Qt::WindowMaximizeButtonHint
    );

    this->dialog_epanet_log->setAttribute(Qt::WA_DeleteOnClose);
    this->dialog_epanet_log->setWindowTitle(tr("EPANET Log"));
    this->dialog_epanet_log->setModal(false);

    this->widget_epanet_log = new QTextBrowser(this->dialog_epanet_log);

    const QFont fixed_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    this->widget_epanet_log->setFont(fixed_font);
    this->widget_epanet_log->setPlainText(this->epanet_log);
    this->widget_epanet_log->setOpenExternalLinks(true);
    this->widget_epanet_log->setLineWrapMode(QTextEdit::NoWrap);

    QVBoxLayout *layout = new QVBoxLayout(this->dialog_epanet_log);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(this->widget_epanet_log);

    const QFontMetrics font_metrics(fixed_font);
    const QStringList lines = this->epanet_log.split('\n');

    int content_width = 0;

    for (const QString &line : lines)
        content_width = qMax(content_width, font_metrics.horizontalAdvance(line));

    // Space for the frame, scrollbar and some padding.
    const int width_overhead = 50;
    const int desired_width = content_width + width_overhead;

    QScreen *screen = this->dialog_epanet_log->screen();
    if (screen == nullptr)
        screen = QApplication::primaryScreen();

    const int maximum_width = screen != nullptr
        ? static_cast<int>(screen->availableGeometry().width() * 0.9)
        : 1200;

    const int dialog_width = qBound(500, desired_width, maximum_width);

    this->dialog_epanet_log->resize(dialog_width, 600);
    showAndActivateDialog(this->dialog_epanet_log);
}

void SimulationManager::importEpanetNetwork()
{
#ifdef Q_OS_WASM
    pending_epanet_import_manager = this;
    aowisOpenEpanetInpFile();
#else
    QWidget *main_window = QApplication::activeWindow();
    QPointer<SimulationManager> manager(this);

    QFileDialog::getOpenFileContent(
        tr("EPANET input files (*.inp)"),
        [manager](const QString &file_name, const QByteArray &file_content)
        {
            if (!manager || file_name.isEmpty())
                return;

            QMetaObject::invokeMethod(
                manager,
                "importEpanetNetworkContent",
                Qt::DirectConnection,
                Q_ARG(QString, file_name),
                Q_ARG(QByteArray, file_content));
        },
        main_window);
#endif
}

void SimulationManager::importEpanetNetworkResource(
    const QString &resource_path,
    const QString &file_name)
{
    if (resource_path.isEmpty() || file_name.isEmpty())
        return;

    QWidget *main_window = QApplication::activeWindow();
    QPointer<QWidget> parent_widget(main_window);

    QFile resource_file(resource_path);
    if (!resource_file.open(QIODevice::ReadOnly))
    {
        showMessageBox(
            parent_widget,
            QMessageBox::Critical,
            tr("Example project failed"),
            tr("Could not open the bundled example revision: %1").arg(file_name));
        return;
    }

    const QByteArray file_content = resource_file.readAll();
    resource_file.close();
    importEpanetNetworkContent(file_name, file_content);
}

void SimulationManager::importEpanetNetworkContent(
    const QString &file_name,
    const QByteArray &file_content)
{
    if (file_name.isEmpty())
        return;

    QWidget *main_window = QApplication::activeWindow();
    QPointer<QWidget> parent_widget(main_window);

    QTemporaryDir temporary_directory;
    if (!temporary_directory.isValid())
    {
        showMessageBox(
            parent_widget,
            QMessageBox::Critical,
            tr("EPANET import failed"),
            tr("Could not create temporary storage for the selected INP file."));
        return;
    }

    const QString local_file_name = QFileInfo(file_name).fileName();
    const QString temporary_file_path = temporary_directory.filePath(
        local_file_name.isEmpty() ? QStringLiteral("import.inp") : local_file_name);

    QFile temporary_file(temporary_file_path);
    if (!temporary_file.open(QIODevice::WriteOnly)
        || temporary_file.write(file_content) != file_content.size())
    {
        showMessageBox(
            parent_widget,
            QMessageBox::Critical,
            tr("EPANET import failed"),
            tr("Could not prepare the selected INP file for import."));
        return;
    }
    temporary_file.close();

    EpanetRunner runner;
    EpanetResultImport import_result = runner.importInp(temporary_file_path);
    finishEpanetNetworkImport(std::move(import_result), parent_widget);
}

void SimulationManager::finishEpanetNetworkImport(
    EpanetResultImport import_result,
    QWidget *parent_widget)
{
    if (!import_result.status.success)
    {
        showDetailedMessageBox(
            parent_widget,
            QMessageBox::Critical,
            tr("EPANET import failed"),
            tr("The selected EPANET project could not be imported."),
            importFailureDetails(import_result));
        return;
    }

    const int diagnostic_count = import_result.diagnostics.size();
    const bool complete = import_result.complete;
    const QString diagnostic_text = importFailureDetails(import_result);

    this->hydraulic_data->replaceNetworkHydraulic(
        std::move(import_result.request.network),
        std::move(import_result.request.quality_runs));
    emit signalEpanetNetworkImported();

    if (!complete)
    {
        showDetailedMessageBox(
            parent_widget,
            QMessageBox::Warning,
            tr("Project imported with warnings"),
            tr("The project was imported, but %1 import issue(s) were reported.")
                .arg(diagnostic_count),
            diagnostic_text);
    }
}

void SimulationManager::exportEpanetNetwork()
{
    QWidget *main_window = QApplication::activeWindow();

#ifndef Q_OS_WASM
    QFileDialog dialog(main_window, tr("Export EPANET network"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(QStringList{tr("EPANET input files (*.inp)"), tr("All files (*)")});
    dialog.setDefaultSuffix(QStringLiteral("inp"));
    dialog.selectFile(QStringLiteral("network.inp"));

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QStringList selected_files = dialog.selectedFiles();
    if (selected_files.isEmpty())
        return;

    const QString file_path = selected_files.constFirst();
#endif

    const NetworkHydraulic &network_hydraulic = this->hydraulic_data->networkHydraulic();
    EpanetRunRequest export_request;
    export_request.network = network_hydraulic;
    export_request.quality_runs = this->hydraulic_data->simulationQualityRunOptions();

    EpanetRunner runner;
    const EpanetResultInp export_result = runner.retrieveInp(export_request);

    if (!export_result.status.success)
    {
        const QString details = exportFailureDetails(export_result.status);
        qWarning().noquote() << "EPANET INP export failed:" << details;
        showMessageBox(main_window, QMessageBox::Critical, tr("EPANET export failed"), details);
        return;
    }

    if (export_result.inp_text.isEmpty())
    {
        const QString message = tr("EPANET generated an empty INP document.");
        qWarning().noquote() << message;
        showMessageBox(main_window, QMessageBox::Critical, tr("EPANET export failed"), message);
        return;
    }

    const QByteArray inp_data = export_result.inp_text.toUtf8();

#ifdef Q_OS_WASM
    const QByteArray filename = QByteArrayLiteral("network.inp");
    aowisDownloadTextFile(filename.constData(), inp_data.constData());
#else
    QSaveFile output_file(file_path);
    if (!output_file.open(QIODevice::WriteOnly))
    {
        showMessageBox(main_window, QMessageBox::Critical, tr("EPANET export failed"), tr("Could not open the selected file for writing:\n%1").arg(output_file.errorString()));
        return;
    }

    const qint64 bytes_written = output_file.write(inp_data);
    if (bytes_written != static_cast<qint64>(inp_data.size()))
    {
        const QString error_message = output_file.errorString();
        output_file.cancelWriting();
        showMessageBox(main_window, QMessageBox::Critical, tr("EPANET export failed"), tr("Could not write the complete INP document:\n%1").arg(error_message));
        return;
    }

    if (!output_file.commit())
        showMessageBox(main_window, QMessageBox::Critical, tr("EPANET export failed"), tr("Could not finalize the exported INP file:\n%1").arg(output_file.errorString()));
#endif
}
