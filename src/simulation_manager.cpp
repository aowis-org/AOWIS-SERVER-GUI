#include "simulation_manager.h"

SimulationManager::SimulationManager(QObject *parent)
    : QObject{parent}
{
    
}

void SimulationManager::run()
{
    // create the network for now here as dummies
    Reservoir reservoir;
    reservoir.id = "R1";
    reservoir.head_m = 30.0;
    
    Junction junction;
    junction.id = "J1";
    junction.elevation_m = 0.0;
    junction.demand_lps = 1.0;
    
    Pipe pipe;
    pipe.id = "P1";
    pipe.node_id_from = reservoir.id;
    pipe.node_id_to = junction.id;
    pipe.length_m = 100.0;
    pipe.diameter_mm = 150.0;
    pipe.roughness_hw = 130.0;
    pipe.minor_loss = 0.0;
    pipe.open = true;
    
    SimulationRequest request;
    request.reservoirs.append(reservoir);
    request.junctions.append(junction);
    request.pipes.append(pipe);
    
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
