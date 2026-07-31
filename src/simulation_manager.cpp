#include "simulation_manager.h"

#include <aowis/epanet/epanet_runner.h>

#include <QByteArray>
#include <QMessageBox>

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

void SimulationManager::run()
{
    const NetworkHydraulic network_hydraulic = this->hydraulic_data->networkHydraulic();
    
    EpanetRunner runner;
    const EpanetResultRun run_result = runner.run(network_hydraulic);
    
    this->epanet_log = run_result.report_lines.join('\n');
    qDebug().noquote() << this->epanet_log;
    
    if (!run_result.result_timeline.status.success)
    {
        const HydraulicSimulationStatus &status = run_result.result_timeline.status;
        qWarning().noquote() << "EPANET simulation failed:" << status.message;
        
        if (!status.message_backend.isEmpty())
            qWarning().noquote() << status.message_backend;
        
        return;
    }
    
    const HydraulicSimulationResultTimeline &result_timeline = run_result.result_timeline;
    
    // Process or store result_timeline here.
}

void SimulationManager::showEpanetLog()
{
    QWidget *main_window = QApplication::activeWindow();
    
    QDialog *log_dialog = new QDialog(
        main_window,
        Qt::Dialog
        | Qt::WindowTitleHint
        | Qt::WindowCloseButtonHint
        | Qt::WindowMaximizeButtonHint
    );
    
    log_dialog->setAttribute(Qt::WA_DeleteOnClose);
    log_dialog->setWindowTitle(tr("EPANET Log"));
    log_dialog->setModal(false);
    
    QTextBrowser *log_widget = new QTextBrowser(log_dialog);
    
    const QFont fixed_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    
    log_widget->setFont(fixed_font);
    log_widget->setPlainText(this->epanet_log);
    log_widget->setOpenExternalLinks(true);
    log_widget->setLineWrapMode(QTextEdit::NoWrap);
    
    QVBoxLayout *layout = new QVBoxLayout(log_dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(log_widget);
    
    const QFontMetrics font_metrics(fixed_font);
    const QStringList lines = this->epanet_log.split('\n');
    
    int content_width = 0;
    
    for (const QString &line : lines)
    {
        content_width = qMax(
            content_width,
            font_metrics.horizontalAdvance(line)
            );
    }
    
    // Space for the frame, scrollbar and some padding.
    const int width_overhead = 50;
    const int desired_width = content_width + width_overhead;
    
    QScreen *screen = log_dialog->screen();
    
    if (screen == nullptr)
    {
        screen = QApplication::primaryScreen();
    }
    
    const int maximum_width =
        screen != nullptr
            ? static_cast<int>(screen->availableGeometry().width() * 0.9)
            : 1200;
    
    const int dialog_width = qBound(
        500,
        desired_width,
        maximum_width
        );
    
    log_dialog->resize(dialog_width, 600);
    
    log_dialog->show();
    log_dialog->raise();
    log_dialog->activateWindow();
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
        QMessageBox::critical(main_window, tr("EPANET export failed"), details);
        return;
    }

    if (export_result.inp_text.isEmpty())
    {
        const QString message = tr("EPANET generated an empty INP document.");
        qWarning().noquote() << message;
        QMessageBox::critical(main_window, tr("EPANET export failed"), message);
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
        QMessageBox::critical(main_window, tr("EPANET export failed"), tr("Could not open the selected file for writing:\n%1").arg(output_file.errorString()));
        return;
    }

    const qint64 bytes_written = output_file.write(inp_data);
    if (bytes_written != static_cast<qint64>(inp_data.size()))
    {
        const QString error_message = output_file.errorString();
        output_file.cancelWriting();
        QMessageBox::critical(main_window, tr("EPANET export failed"), tr("Could not write the complete INP document:\n%1").arg(error_message));
        return;
    }

    if (!output_file.commit())
        QMessageBox::critical(main_window, tr("EPANET export failed"), tr("Could not finalize the exported INP file:\n%1").arg(output_file.errorString()));
#endif
}
