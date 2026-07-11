#include "simulation_manager.h"

SimulationManager::SimulationManager(QObject *parent)
    : QObject{parent}
{
    
}

void SimulationManager::run()
{
    //SimulationRequest request = DummyNetworks::networkSimple();
    SimulationRequest request = DummyNetworks::networkTanks();
    
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
    log_dialog->resize(700, 600);
    log_dialog->setModal(false);
    
    QTextBrowser *log_widget = new QTextBrowser(log_dialog);
    
    log_widget->setFont(
        QFontDatabase::systemFont(QFontDatabase::FixedFont)
        );
    
    log_widget->setPlainText(this->epanet_log);
    log_widget->setOpenExternalLinks(true);
    
    QVBoxLayout *layout = new QVBoxLayout(log_dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(log_widget);
    
    log_dialog->show();
    log_dialog->raise();
    log_dialog->activateWindow();
}
