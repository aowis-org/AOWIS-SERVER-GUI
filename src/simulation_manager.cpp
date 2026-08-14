#include "simulation_manager.h"

#include "simulation_statistics_dialog.h"
#include "simulation_diagnostics_dialog.h"

#include <aowis/epanet/epanet_runner.h>
#include <aowis/epanet/epanet_result_run.h>

#include <QByteArray>
#include <QMessageBox>

#include <memory>

#ifndef Q_OS_WASM
#include <QFileDialog>
#include <QSaveFile>
#else
#include <emscripten.h>

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
}

SimulationManager::SimulationManager(HydraulicData *hydraulic_data, QObject *parent)
    : QObject{parent},
    hydraulic_data(hydraulic_data)
{
}

SimulationManager::~SimulationManager()
{
#ifndef Q_OS_WASM
    if (this->simulation_thread && this->simulation_thread->isRunning())
        this->simulation_thread->wait();
#endif
}

void SimulationManager::run()
{
    if (this->simulation_running)
        return;

    this->simulation_running = true;
    emit signalSimulationStarted();
    this->epanet_log.clear();
    if (this->widget_epanet_log)
        this->widget_epanet_log->clear();
    emit signalEpanetLogAvailabilityChanged(false);

    const NetworkHydraulic network_hydraulic = this->hydraulic_data->networkHydraulic();

    std::shared_ptr<EpanetResultRun> run_result = std::make_shared<EpanetResultRun>();
    QThread *thread = QThread::create([network_hydraulic, run_result]()
    {
        EpanetRunner runner;
        *run_result = runner.run(network_hydraulic);
    });
    this->simulation_thread = thread;

    connect(thread, &QThread::finished, this, [this, thread, run_result]()
    {
        if (this->simulation_thread == thread)
            this->simulation_thread = nullptr;

        this->simulation_running = false;
        finishSimulation(*run_result);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void SimulationManager::finishSimulation(const EpanetResultRun &run_result)
{
    this->epanet_log = run_result.report_lines.join('\n');
    if (this->widget_epanet_log)
        this->widget_epanet_log->setPlainText(this->epanet_log);
    emit signalEpanetLogAvailabilityChanged(!this->epanet_log.isEmpty());
    qDebug().noquote() << this->epanet_log;

    this->hydraulic_data->setSimulationResultTimeline(run_result.result_timeline);

    bool has_error_diagnostic = false;
    bool has_warning_diagnostic = false;
    for (const HydraulicSimulationDiagnostic &diagnostic : run_result.result_timeline.diagnostics)
    {
        if (diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Warning)
            has_warning_diagnostic = true;

        if (diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Error
            || diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Fatal)
        {
            has_error_diagnostic = true;
        }
    }

    if (!run_result.result_timeline.status.success
        || has_error_diagnostic
        || has_warning_diagnostic)
    {
        showSimulationDiagnostics();
    }

    if (!run_result.result_timeline.status.success)
    {
        const HydraulicSimulationStatus &status = run_result.result_timeline.status;
        qWarning().noquote() << "EPANET simulation failed:" << status.message;

        if (!status.message_backend.isEmpty())
            qWarning().noquote() << status.message_backend;

        return;
    }

    if (has_error_diagnostic
        || has_warning_diagnostic
        || run_result.result_timeline.validity != HydraulicSimulationResultValidity::Valid)
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
        tr("Simulation completed successfully."));
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
    EpanetRunner runner;
    const EpanetResultInp export_result = runner.retrieveInp(network_hydraulic);

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
