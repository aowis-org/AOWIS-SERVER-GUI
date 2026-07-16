#include "simulation_manager.h"

SimulationManager::SimulationManager(QObject *parent)
    : QObject{parent}
{
    
}

void SimulationManager::run()
{
    //NetworkHydraulic request = DummyNetworks::networkSimple();
    NetworkHydraulic request = DummyNetworks::networkTanks();
    //NetworkHydraulic request = DummyNetworks::networkTanksTimeline();
    
    EpanetWrapper *epanet = new EpanetWrapper(this);
    epanet->run(request);
    //qDebug().noquote() << epanet->reportText();
    
    this->epanet_log = epanet->reportText();
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
